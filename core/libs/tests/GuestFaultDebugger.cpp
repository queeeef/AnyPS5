#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <source_location>

extern "C" {
int APS5_VABI sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t, std::size_t, int, std::int64_t*);
int APS5_VABI sceKernelMapDirectMemory(void**, std::size_t, int, int, std::int64_t, std::size_t);
int APS5_VABI sceKernelMunmap(void*, std::size_t);
int APS5_VABI sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
}

namespace {

constexpr std::size_t PageBytes = 0x4000;
constexpr int Qwords = 64;

void Require(bool condition, const char* what, std::source_location location = std::source_location::current()) {
    if (condition) return;
    std::fprintf(stderr, "guest fault debugger test failed at line %u: %s\n", static_cast<unsigned>(location.line()), what);
    std::exit(1);
}

__attribute__((noinline)) int WriteBelowRedZone(volatile std::uint8_t* page, bool dirtyAvx) {
    std::uint64_t* rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));
    for (int i = 1; i <= Qwords; ++i) rsp[-i] = 0x5a5a5a5a00000000ull | static_cast<std::uint64_t>(i);
    if (dirtyAvx) asm volatile("vpcmpeqd %%ymm1, %%ymm1, %%ymm1\n\tvpcmpeqd %%ymm9, %%ymm9, %%ymm9" ::: "xmm1", "xmm9");
    asm volatile("movb $1, (%0)" :: "r"(page) : "memory");
    int changed = 0;
    for (int i = 1; i <= Qwords; ++i) {
        if (rsp[-i] != (0x5a5a5a5a00000000ull | static_cast<std::uint64_t>(i))) ++changed;
    }
    if (dirtyAvx) asm volatile("vzeroupper" ::: "memory");
    return changed;
}

void Arm(std::uintptr_t address) {
    std::array<void*, 8> pages{};
    std::size_t count = pages.size();
    Require(GuestArena::GuestArenaCollectWrites_nid_postfix(address, PageBytes, pages.data(), &count, true), "the shared page could not be collected");
    MEMORY_BASIC_INFORMATION info{};
    Require(VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != 0 && (info.Protect & 0xffu) == PAGE_READONLY, "collecting did not arm the shared page");
}

bool Written(std::uintptr_t address) {
    std::array<void*, 8> pages{};
    std::size_t count = pages.size();
    Require(GuestArena::GuestArenaCollectWrites_nid_postfix(address, PageBytes, pages.data(), &count, false), "the shared page could not be collected");
    return count != 0;
}

}

int main() {
    std::int64_t physical = 0;
    Require(sceKernelAllocateDirectMemory(0, 0x7fffffffffll, PageBytes, 0, 0, &physical) == 0, "no direct memory");
    void* mapped = nullptr;
    Require(sceKernelMapDirectMemory(&mapped, PageBytes, 3, 0, physical, 0) == 0, "the direct memory did not map");
    Require(IsDebuggerPresent() != FALSE, "aps5-faultd did not attach (APS5_FAULT_DEBUGGER is set by the test)");
    auto* page = static_cast<volatile std::uint8_t*>(mapped);
    const auto address = reinterpret_cast<std::uintptr_t>(mapped);
    for (const bool dirtyAvx : {false, true}) {
        Arm(address);
        page[0] = 0;
        Require(Written(address), "a write before arming again was not collected");
        Arm(address);
        const auto changed = WriteBelowRedZone(page, dirtyAvx);
        char text[160];
        std::snprintf(text, sizeof(text), "a write fault resumed by the debugger changed %d of %d qwords below rsp (dirty AVX %d)", changed, Qwords, dirtyAvx ? 1 : 0);
        Require(changed == 0, text);
        Require(page[0] == 1, "the faulting write did not land");
        Require(Written(address), "the write the debugger resumed was not collected");
    }
    Require(sceKernelMunmap(mapped, PageBytes) == 0, "the direct memory did not unmap");
    Require(sceKernelReleaseDirectMemory(physical, PageBytes) == 0, "the direct memory was not released");
    std::puts("guest fault debugger tests passed");
    return 0;
}
