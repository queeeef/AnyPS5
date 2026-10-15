#include "VulkanTestDevice.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Synchronization/SynchronizationStatistics.hpp"
#include "prx/libSceAgcDriver/Submit/include/Acb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// A small DMA_DATA copy of a buffer a dispatch recorded just before it still writes: the copy
// carries the dispatch's result. The GPU copies it in queue order (no CPU read of the source, which
// would wait for the dispatch); APS5_NO_GPU_SMALL_DMA=1 keeps the CPU read and must give the same.

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Stores an LCG run for the user data's iteration count through the buffer descriptor it points at.
alignas(256) std::array<std::uint32_t, 15> SpinCode{0xf4080200u, 0xfa000000u, 0xbe840381u, 0x9304ff04u, 0x0019660du, 0x8004ff04u, 0x3c6ef35fu, 0x80828102u, 0xbf078002u, 0xbf85fff9u, 0xbf8cc07fu, 0x7e000204u, 0xe0700000u, 0x80020000u, 0xbf810000u};

constexpr std::size_t BlockBytes = 65536;
constexpr std::size_t DescriptorOffset = 0x000;
constexpr std::size_t SourceOffset = 0x100;
constexpr std::size_t DestinationOffset = 0x200;
constexpr std::size_t DoneOffset = 0x300;
constexpr std::uint32_t CopyBytes = 16;

std::uint32_t Low(std::uint64_t address) {
    return static_cast<std::uint32_t>(address);
}

std::uint32_t High(std::uint64_t address) {
    return static_cast<std::uint32_t>(address >> 32u);
}

void SetShaderRegister(std::vector<std::uint32_t>& words, std::uint32_t offset, std::uint32_t value) {
    words.insert(words.end(), {0xc0017600u, offset, value});
}

void Dispatch(std::vector<std::uint32_t>& words, const void* code, std::initializer_list<std::uint32_t> userData) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(code));
    SetShaderRegister(words, 0x20c, static_cast<std::uint32_t>(address >> 8u));
    SetShaderRegister(words, 0x20d, static_cast<std::uint32_t>(address >> 40u) & 0xffu);
    for (const std::uint32_t offset : {0x207u, 0x208u, 0x209u}) SetShaderRegister(words, offset, 1);
    SetShaderRegister(words, 0x212, 0);
    SetShaderRegister(words, 0x213, static_cast<std::uint32_t>(userData.size()) << 1u);
    std::uint32_t offset = 0x240;
    for (const auto value : userData) SetShaderRegister(words, offset++, value);
    words.insert(words.end(), {0xc0031500u, 1, 1, 1, 0x8041u});
}

// DMA_DATA, memory to memory.
void DmaCopy(std::vector<std::uint32_t>& words, std::uint64_t source, std::uint64_t destination, std::uint32_t bytes) {
    words.insert(words.end(), {0xc0055000u, 0u, Low(source), High(source), Low(destination), High(destination), bytes});
}

void WriteData(std::vector<std::uint32_t>& words, std::uint64_t target, std::uint32_t value) {
    words.insert(words.end(), {0xc0033700u, 0x00100200u, Low(target), High(target), value});
}

}

int main() {
    try {
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        device.reset();
#ifdef _WIN32
        auto* block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        auto* block = static_cast<std::uint8_t*>(std::aligned_alloc(BlockBytes, BlockBytes));
#endif
        Require(block != nullptr, "small DMA: cannot allocate the guest block");
        std::memset(block, 0, BlockBytes);
        std::memset(block + DestinationOffset, 0xee, CopyBytes);
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
        const auto base = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block));
        const std::array<std::uint32_t, 4> descriptor{Low(base + SourceOffset), High(base + SourceOffset) & 0xffffu, CopyBytes, 0x31016facu};
        std::memcpy(block + DescriptorOffset, descriptor.data(), sizeof(descriptor));
        const bool gpu = std::getenv("APS5_NO_GPU_SMALL_DMA") == nullptr;
        const auto onGpuBefore = AgcDriver::DriverDetail::smallDmaOnGpu.load();
        const auto onCpuBefore = AgcDriver::DriverDetail::smallDmaOnCpu.load();
        std::vector<std::uint32_t> words;
        Dispatch(words, SpinCode.data(), {Low(base + DescriptorOffset), High(base + DescriptorOffset), 1u << 22u});
        DmaCopy(words, base + SourceOffset, base + DestinationOffset, CopyBytes);
        WriteData(words, base + DoneOffset, 1);
        Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
        Require(sceAgcDriverSubmitAcb(0x20, &packet) == 0, "small DMA: submit failed");
        const auto start = std::chrono::steady_clock::now();
        while (*reinterpret_cast<volatile std::uint32_t*>(block + DoneOffset) == 0) {
            Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(10), "small DMA: the submission did not finish");
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        AgcDriverWaitIdle_nid_postfix();
        const auto* source = block + SourceOffset;
        Require(std::any_of(source, source + 4, [](std::uint8_t byte) { return byte != 0; }), "small DMA: the dispatch stored nothing");
        Require(std::memcmp(block + DestinationOffset, source, CopyBytes) == 0, "small DMA: the copy does not hold what the dispatch stored before it");
        const auto onGpu = AgcDriver::DriverDetail::smallDmaOnGpu.load() - onGpuBefore;
        const auto onCpu = AgcDriver::DriverDetail::smallDmaOnCpu.load() - onCpuBefore;
        if (gpu) Require(onGpu == 1 && onCpu == 0, "small DMA: the copy over the dispatch's pending store was not made on the GPU (" + std::to_string(onGpu) + " on the GPU, " + std::to_string(onCpu) + " read by the CPU)");
        else Require(onGpu == 0 && onCpu == 1, "small DMA: with APS5_NO_GPU_SMALL_DMA=1 the copy was not read by the CPU");
        std::cout << "small DMA after dispatch: ok (" << (gpu ? "on the GPU" : "read by the CPU") << ")\n";
        AgcDriverShutdown_nid_postfix();
        GuestAllocations::Mutation().Remove(block);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try {
            AgcDriverShutdown_nid_postfix();
        } catch (const std::exception& shutdownError) {
            std::cerr << shutdownError.what() << '\n';
        }
        return 1;
    }
}
