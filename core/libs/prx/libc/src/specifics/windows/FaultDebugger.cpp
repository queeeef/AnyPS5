#include "prx/libc/include/FaultDebuggerShared.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const auto pid = std::strtoul(argv[1], nullptr, 10);
    char name[64];
    FaultDebugger::MappingName(name, sizeof(name), pid);
    HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (mapping == nullptr) return 3;
    auto* header = static_cast<FaultDebugger::Header*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (header == nullptr || header->magic != FaultDebugger::Magic) return 4;
    auto* table = FaultDebugger::Table(header);
    FaultDebugger::ReadyName(name, sizeof(name), pid);
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    if (ready == nullptr) return 5;
    if (!DebugActiveProcess(pid)) return 6;
    DebugSetProcessKillOnExit(FALSE);
    const auto begin = header->base;
    const auto end = header->base + header->pages * header->pageBytes;
    HANDLE process = nullptr;
    bool attached = false;
    for (;;) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, INFINITE)) return 7;
        DWORD status = DBG_CONTINUE;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            process = event.u.CreateProcessInfo.hProcess;
            if (event.u.CreateProcessInfo.hFile != nullptr) CloseHandle(event.u.CreateProcessInfo.hFile);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if (event.u.LoadDll.hFile != nullptr) CloseHandle(event.u.LoadDll.hFile);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            const auto& record = event.u.Exception.ExceptionRecord;
            status = DBG_EXCEPTION_NOT_HANDLED;
            if (!attached && record.ExceptionCode == EXCEPTION_BREAKPOINT) {
                attached = true;
                header->attached = 1;
                SetEvent(ready);
                status = DBG_CONTINUE;
                break;
            }
            if (!event.u.Exception.dwFirstChance || record.ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record.NumberParameters < 2 || record.ExceptionInformation[0] != 1) break;
            const auto address = static_cast<std::uint64_t>(record.ExceptionInformation[1]);
            if (address < begin || address >= end || process == nullptr) break;
            const auto index = (address - begin) / header->pageBytes;
            const auto state = table[index];
            if (state != FaultDebugger::ArmedReadWrite && state != FaultDebugger::ArmedExecuteReadWrite) {
                MEMORY_BASIC_INFORMATION memory{};
                const auto writable = PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
                if (VirtualQueryEx(process, reinterpret_cast<void*>(address), &memory, sizeof(memory)) != 0 && memory.State == MEM_COMMIT && (memory.Protect & writable) != 0 && (memory.Protect & PAGE_GUARD) == 0) {
                    ++header->retried;
                    status = DBG_CONTINUE;
                }
                break;
            }
            DWORD previous;
            const auto page = reinterpret_cast<void*>(begin + index * header->pageBytes);
            if (!VirtualProtectEx(process, page, header->pageBytes, state == FaultDebugger::ArmedReadWrite ? PAGE_READWRITE : PAGE_EXECUTE_READWRITE, &previous)) break;
            table[index] = FaultDebugger::Disarmed;
            if (header->head - header->tail < FaultDebugger::RingSize) {
                header->ring[header->head % FaultDebugger::RingSize] = static_cast<std::uint32_t>(index);
                ++header->head;
            } else {
                header->overflow = 1;
            }
            ++header->handled;
            status = DBG_CONTINUE;
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
            return 0;
        default:
            break;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
    }
}
