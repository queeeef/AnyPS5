#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Inputs = 4;
constexpr std::uint32_t Results = 8;
alignas(256) std::array<std::uint32_t, Threads * Inputs> Input{};
alignas(256) std::array<std::uint32_t, Threads * Results> Output{};

alignas(256) constexpr std::array<std::uint32_t, 50> Code{
    0x34020084, 0x34060085, 0xe0301000, 0x80000401, 0xe0301004, 0x80000501, 0xe0301008, 0x80000601,
    0xbf8c3f70, 0x7e140306, 0x7e160306, 0x7e180306, 0x7e1a0306, 0x7e1c0306, 0x7e1e0306, 0x7e200306,
    0x7e220306, 0xd536000a, 0x20020b04, 0xd536030b, 0x40020b04, 0xd536800c, 0x00020b04, 0xd536000d,
    0x08020b04, 0xd536000e, 0x10020b04, 0xd536000f, 0x18020b04, 0xd5368110, 0x28020b04, 0xd5360011,
    0x00020b04, 0xe0701000, 0x80010a03, 0xe0701004, 0x80010b03, 0xe0701008, 0x80010c03, 0xe070100c,
    0x80010d03, 0xe0701010, 0x80010e03, 0xe0701014, 0x80010f03, 0xe0701018, 0x80011003, 0xe070101c,
    0x80011103, 0xbf810000,
};

constexpr std::uint32_t Rows[32][3] = {
    {0x00003400u, 0x00003800u, 0x00003000u},
    {0x00003e00u, 0x0000b400u, 0x00004000u},
    {0x00007b53u, 0x000070e2u, 0x00007bffu},
    {0x00000400u, 0x00003800u, 0x00000000u},
    {0x00007e00u, 0x00003c00u, 0x00003c00u},
    {0x00007c00u, 0x00003c00u, 0x0000bc00u},
    {0x0000c000u, 0x0000c200u, 0x00004400u},
    {0x00000001u, 0x00003a00u, 0x00008001u},
    {0x00003c00u, 0x00003c00u, 0x0000bc00u},
    {0x00008000u, 0x00003c00u, 0x00000000u},
    {0x00003c00u, 0x00007c00u, 0x0000fc00u},
    {0x00000000u, 0x00007c00u, 0x00003c00u},
    {0x00003555u, 0x00003c01u, 0x00008c00u},
    {0x0000b800u, 0x00003800u, 0x00003400u},
    {0x00003c00u, 0x00000001u, 0x00000001u},
    {0x00005bffu, 0x00005bffu, 0x0000fbffu},
    {0x0000c3d8u, 0x0000b9d0u, 0x00008746u},
    {0x0000a1c7u, 0x00002a6au, 0x0000d922u},
    {0x000085c3u, 0x0000853bu, 0x00003fa7u},
    {0x0000a27au, 0x0000a75cu, 0x000033f4u},
    {0x0000c560u, 0x0000cc5au, 0x00008c0cu},
    {0x0000a3ebu, 0x00002a6fu, 0x0000bfa0u},
    {0x00001bdbu, 0x00003feeu, 0x00007bccu},
    {0x00003ca0u, 0x0000d42eu, 0x00005ddbu},
    {0x0000f6f4u, 0x00004858u, 0x0000d28eu},
    {0x0000c491u, 0x00002cceu, 0x000048f8u},
    {0x00000727u, 0x000078e8u, 0x00007324u},
    {0x0000760bu, 0x00001816u, 0x000045f4u},
    {0x0000112du, 0x0000b741u, 0x000037b7u},
    {0x0000be14u, 0x0000083eu, 0x00003a0au},
    {0x000091bau, 0x000038ddu, 0x000093a5u},
    {0x00009732u, 0x0000150cu, 0x0000b9ccu},
};
constexpr std::uint32_t Expected[32][Results] = {
    {0x00000000u, 0x00000000u, 0x00003400u, 0x00003400u, 0x00003400u, 0x00003400u, 0x00000000u, 0x00003400u},
    {0x000040c0u, 0x00003e80u, 0x00003c00u, 0x00003e80u, 0x00003e80u, 0x00003e80u, 0x00003c00u, 0x00003e80u},
    {0x0000fc00u, 0x0000fc00u, 0x00003c00u, 0x00007c00u, 0x00007c00u, 0x00007c00u, 0x00000000u, 0x00007c00u},
    {0x00008200u, 0x00008200u, 0x00000200u, 0x00000200u, 0x00000200u, 0x00000200u, 0x00000000u, 0x00000200u},
    {0x00007e00u, 0x00007e00u, 0x00000000u, 0x00007e00u, 0x00007e00u, 0x00007e00u, 0x00000000u, 0x00007e00u},
    {0x0000fc00u, 0x0000fc00u, 0x00003c00u, 0x00007c00u, 0x00007c00u, 0x00007c00u, 0x00000000u, 0x00007c00u},
    {0x0000c000u, 0x0000c000u, 0x00003c00u, 0x00004900u, 0x00004900u, 0x00004900u, 0x00003c00u, 0x00004900u},
    {0x00008002u, 0x00008002u, 0x00000000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00000000u, 0x00008000u},
    {0x0000c000u, 0x0000c000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x0000fc00u, 0x0000fc00u, 0x00000000u, 0x00007e00u, 0x00007e00u, 0x00007e00u, 0x00000000u, 0x00007e00u},
    {0x00007e00u, 0x00007e00u, 0x00000000u, 0x00007e00u, 0x00007e00u, 0x00007e00u, 0x00000000u, 0x00007e00u},
    {0x0000b557u, 0x0000b557u, 0x00003555u, 0x00003555u, 0x00003555u, 0x00003555u, 0x00000000u, 0x00003555u},
    {0x00003800u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000002u, 0x00000002u, 0x00000002u, 0x00000002u, 0x00000000u, 0x00000002u},
    {0x0000fc00u, 0x0000fc00u, 0x00000000u, 0x0000cfffu, 0x0000cfffu, 0x0000cfffu, 0x00000000u, 0x0000cfffu},
    {0x0000c1b3u, 0x0000c1b3u, 0x00003c00u, 0x000041b3u, 0x000041b3u, 0x000041b3u, 0x00003c00u, 0x000041b3u},
    {0x0000d922u, 0x0000d922u, 0x00000000u, 0x0000d922u, 0x0000d922u, 0x0000d922u, 0x00000000u, 0x0000d922u},
    {0x00003fa7u, 0x00003fa7u, 0x00003c00u, 0x00003fa7u, 0x00003fa7u, 0x00003fa7u, 0x00003c00u, 0x00003fa7u},
    {0x000033f1u, 0x000033f1u, 0x000033f7u, 0x000033f7u, 0x000033f7u, 0x000033f7u, 0x000033f7u, 0x000033f7u},
    {0x0000d5d9u, 0x0000d5d9u, 0x00003c00u, 0x000055d9u, 0x000055d9u, 0x000055d9u, 0x00003c00u, 0x000055d9u},
    {0x0000bf9fu, 0x0000bfa1u, 0x00000000u, 0x0000bfa1u, 0x0000bfa1u, 0x0000bfa1u, 0x00000000u, 0x0000bfa1u},
    {0x00007bccu, 0x00007bccu, 0x00003c00u, 0x00007bccu, 0x00007bccu, 0x00007bccu, 0x00003c00u, 0x00007bccu},
    {0x00005f10u, 0x00005ca6u, 0x00003c00u, 0x00005ca6u, 0x00005ca6u, 0x00005ca6u, 0x00003c00u, 0x00005ca6u},
    {0x00007c00u, 0x0000fc00u, 0x00000000u, 0x0000fc00u, 0x0000fc00u, 0x0000fc00u, 0x00000000u, 0x0000fc00u},
    {0x00004924u, 0x000048ccu, 0x00003c00u, 0x000048ccu, 0x000048ccu, 0x000048ccu, 0x00003c00u, 0x000048ccu},
    {0x00007323u, 0x00007323u, 0x00003c00u, 0x00007325u, 0x00007325u, 0x00007325u, 0x00003c00u, 0x00007325u},
    {0x0000d16eu, 0x0000d16eu, 0x00003c00u, 0x000052ebu, 0x000052ebu, 0x000052ebu, 0x00000000u, 0x000052ebu},
    {0x000037b8u, 0x000037b6u, 0x000037b6u, 0x000037b6u, 0x000037b6u, 0x000037b6u, 0x000037b8u, 0x000037b6u},
    {0x00003a0au, 0x00003a0au, 0x00003a0au, 0x00003a0au, 0x00003a0au, 0x00003a0au, 0x00003a0au, 0x00003a0au},
    {0x0000902au, 0x00009590u, 0x00000000u, 0x00009590u, 0x00009590u, 0x00009590u, 0x00000000u, 0x00009590u},
    {0x0000b9ccu, 0x0000b9ccu, 0x00000000u, 0x0000b9ccu, 0x0000b9ccu, 0x0000b9ccu, 0x00000000u, 0x0000b9ccu},
};
constexpr const char* Names[Results] = {"neg_src0", "abs_src0_neg_abs_src1", "clamp", "mul2", "mul4", "div2", "neg_abs_src0_clamp_mul2", "plain"};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

bool IsNan16(std::uint32_t bits) {
    return (bits & 0x7fffu) > 0x7c00u;
}

bool SameHalf(std::uint32_t actual, std::uint32_t expected) {
    return actual == expected || (IsNan16(actual) && IsNan16(expected));
}

void Expect(std::uint32_t tid, std::uint32_t actual, std::uint32_t expected, const char* name) {
    Require(SameHalf(actual & 0xffffu, expected & 0xffffu) && (actual >> 16u) == (expected >> 16u), std::string("vop3 fmac f16 modifiers: lane ") + std::to_string(tid) + " " + name + " is " + Hex(actual) + ", expected " + Hex(expected));
}

ShaderRecompiler::RecompileResult Compile(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code) {
    std::vector<std::uint32_t> userData(8, 0u);
    const auto input = BufferDescriptor(Input.data(), static_cast<std::uint32_t>(Input.size() * 4u));
    const auto output = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(Output.size() * 4u));
    std::copy(input.begin(), input.end(), userData.begin());
    std::copy(output.begin(), output.end(), userData.begin() + 4);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    return ShaderRecompiler::Recompile(request);
}

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) std::copy(std::begin(Rows[tid]), std::end(Rows[tid]), &Input[tid * Inputs]);
    Output.fill(0xdeadbeefu);
    const std::span<const std::uint32_t> code(Code);
    const auto result = Compile(device, code);
    device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
}

void Check() {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        for (std::uint32_t index = 0; index < Results; ++index) Expect(tid, Output[tid * Results + index], Expected[tid][index], Names[index]);
    }
}

void CheckUnsettledModifiersRefused(AgcDriver::VulkanDevice& device) {
    const std::array<std::array<std::uint32_t, 2>, 4> forms{{
        {0xd536040au, 0x00020b04u},
        {0xd536000au, 0x80020b04u},
        {0xd536080au, 0x00020b04u},
        {0xd536400au, 0x00020b04u},
    }};
    for (const auto& form : forms) {
        alignas(256) const std::array<std::uint32_t, 3> code{form[0], form[1], 0xbf810000u};
        std::string refusal;
        try {
            static_cast<void>(Compile(device, code));
        } catch (const std::exception& error) {
            refusal = error.what();
        }
        Require(refusal.find("VOP3 source modifiers are not implemented") != std::string::npos, "v_fmac_f16 " + Hex(form[0]) + " " + Hex(form[1]) + " was not refused");
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        Run(*device);
        Check();
        CheckUnsettledModifiersRefused(*device);
        std::puts("vop3 fmac f16 modifiers tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
