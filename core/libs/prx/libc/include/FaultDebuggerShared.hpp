#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_FAULTDEBUGGERSHARED_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_FAULTDEBUGGERSHARED_HPP

#include <cstdint>
#include <cstdio>

namespace FaultDebugger {

constexpr std::uint64_t Magic = 0x3154464c35535041ull;
constexpr std::uint32_t RingSize = 1u << 20;
constexpr std::uint8_t Disarmed = 0;
constexpr std::uint8_t ArmedReadWrite = 1;
constexpr std::uint8_t ArmedExecuteReadWrite = 2;

struct Header {
    std::uint64_t magic;
    std::uint64_t base;
    std::uint64_t pages;
    std::uint64_t pageBytes;
    std::uint64_t head;
    std::uint64_t tail;
    std::uint64_t handled;
    std::uint32_t overflow;
    std::uint32_t attached;
    std::uint32_t ring[RingSize];
};

inline std::uint8_t* Table(Header* header) {
    return reinterpret_cast<std::uint8_t*>(header + 1);
}

inline void MappingName(char* text, std::size_t size, unsigned long pid) {
    std::snprintf(text, size, "Local\\aps5-faultd-%lu", pid);
}

inline void ReadyName(char* text, std::size_t size, unsigned long pid) {
    std::snprintf(text, size, "Local\\aps5-faultd-ready-%lu", pid);
}

}

#endif
