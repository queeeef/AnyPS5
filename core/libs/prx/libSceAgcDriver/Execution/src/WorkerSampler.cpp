#include "prx/libSceAgcDriver/Execution/include/WorkerSampler.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <stop_token>
#include <system_error>
#include <thread>
#include <vector>
#endif

namespace AgcDriver {

#ifdef _WIN32
namespace {

std::jthread processSamplerThread;
std::once_flag processSamplerStarted;
std::mutex workerSamplersMutex;
std::vector<std::jthread> workerSamplerThreads;

std::uint64_t ThreadCpu100ns(HANDLE thread, std::uint64_t& kernel) {
    FILETIME created{}, exited{}, kernelTime{}, userTime{};
    if (!GetThreadTimes(thread, &created, &exited, &kernelTime, &userTime)) return kernel = 0;
    kernel = (static_cast<std::uint64_t>(kernelTime.dwHighDateTime) << 32u) | kernelTime.dwLowDateTime;
    return (static_cast<std::uint64_t>(userTime.dwHighDateTime) << 32u) | userTime.dwLowDateTime;
}

// Debug aid: APS5_SAMPLE_WORKER=<file> samples the calling thread every millisecond and, every 20 s,
// writes "module+offset self inclusive" lines to <file> for offline symbolization with nm.
struct Sampler {
    HANDLE target = nullptr;
    std::string path;
    std::mutex mutex;
    std::map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> counts;
    std::map<std::vector<std::uint64_t>, std::uint64_t> stacks;
    std::uint64_t samples = 0;
    std::uint64_t window = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::uint32_t queue = 0xffffffffu;
    DWORD threadId = 0;
    std::uint64_t lastUser = 0;
    std::uint64_t lastKernel = 0;
    std::chrono::steady_clock::time_point lastWrite = std::chrono::steady_clock::now();

    ~Sampler() noexcept(false) {
        if (target != nullptr && !CloseHandle(target)) throw std::system_error(GetLastError(), std::system_category(), "Closing sampler thread handle");
    }

    void sample() {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        if (SuspendThread(target) == static_cast<DWORD>(-1)) return;
        const bool captured = GetThreadContext(target, &context) != 0;
        std::vector<std::uint64_t> frames;
        if (captured) {
            // Unwinding reads the target's stack while it is suspended. A frame without unwind data is a
            // leaf (return address at RSP) when that address has unwind data, else walked by its RBP chain.
            const auto read = [](std::uint64_t address, std::uint64_t& value) {
                SIZE_T got = 0;
                return address % 8 == 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &got) && got == sizeof(value);
            };
            const auto unwound = [](std::uint64_t address) {
                DWORD64 base = 0;
                return address != 0 && RtlLookupFunctionEntry(address, &base, nullptr) != nullptr;
            };
            for (int depth = 0; depth < 64 && context.Rip != 0; ++depth) {
                frames.push_back(context.Rip);
                DWORD64 imageBase = 0;
                auto* entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
                if (entry != nullptr) {
                    PVOID handler = nullptr;
                    DWORD64 establisher = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry, &context, &handler, &establisher, nullptr);
                    continue;
                }
                std::uint64_t leafReturn = 0;
                if (read(context.Rsp, leafReturn) && unwound(leafReturn)) {
                    context.Rip = leafReturn;
                    context.Rsp += 8;
                    continue;
                }
                std::uint64_t savedFrame = 0;
                std::uint64_t frameReturn = 0;
                if (context.Rbp < context.Rsp || context.Rbp - context.Rsp > (8u << 20u) || !read(context.Rbp, savedFrame) || !read(context.Rbp + 8, frameReturn) || frameReturn == 0) break;
                context.Rip = frameReturn;
                context.Rsp = context.Rbp + 16;
                context.Rbp = savedFrame;
            }
        }
        ResumeThread(target);
        std::lock_guard lock(mutex);
        ++samples;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            auto& count = counts[frames[i]];
            if (i == 0) ++count.first;
            ++count.second;
        }
        if (queue != 0xffffffffu) ++stacks[frames];
    }

    void write() {
        std::lock_guard lock(mutex);
        std::FILE* file = std::fopen(path.c_str(), window == 0 ? "w" : "a");
        if (file == nullptr) return;
        const auto now = std::chrono::steady_clock::now();
        const double wall = std::chrono::duration<double>(now - lastWrite).count();
        lastWrite = now;
        std::uint64_t kernel = 0;
        const auto user = ThreadCpu100ns(target, kernel);
        const double userSeconds = static_cast<double>(user - lastUser) / 1e7;
        const double kernelSeconds = static_cast<double>(kernel - lastKernel) / 1e7;
        lastUser = user;
        lastKernel = kernel;
        std::fprintf(file, "# window %llu at %.0f s: %llu samples; cpu %.2f s user + %.2f s kernel over %.1f s wall\n", static_cast<unsigned long long>(window), std::chrono::duration<double>(now - started).count(), static_cast<unsigned long long>(samples), userSeconds, kernelSeconds, wall);
        if (queue != 0xffffffffu) std::fprintf(stderr, "[local-worker-cpu] queue 0x%x thread %lu window %llu: cpu %.2f s user + %.2f s kernel over %.1f s wall (%.0f%%), %llu samples\n", queue, static_cast<unsigned long>(threadId), static_cast<unsigned long long>(window), userSeconds, kernelSeconds, wall, wall > 0 ? 100.0 * (userSeconds + kernelSeconds) / wall : 0.0, static_cast<unsigned long long>(samples));
        ++window;
        for (const auto& [address, count] : counts) {
            HMODULE module = nullptr;
            char name[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module)) GetModuleFileNameA(module, name, sizeof(name));
            const char* base = std::strrchr(name, '\\');
            std::fprintf(file, "%s 0x%llx %llu %llu\n", base ? base + 1 : name, static_cast<unsigned long long>(address - reinterpret_cast<std::uint64_t>(module)), static_cast<unsigned long long>(count.first), static_cast<unsigned long long>(count.second));
        }
        std::map<std::uint64_t, std::pair<std::string, std::uint64_t>> modules;
        const auto describe = [&](std::uint64_t address) {
            auto found = modules.upper_bound(address);
            if (found != modules.begin()) {
                --found;
                if (address - found->first < found->second.second) {
                    char text[96];
                    std::snprintf(text, sizeof(text), "%s+0x%llx", found->second.first.c_str(), static_cast<unsigned long long>(address - found->first));
                    return std::string(text);
                }
            }
            HMODULE module = nullptr;
            char name[MAX_PATH] = "?";
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module)) {
                char text[32];
                std::snprintf(text, sizeof(text), "?+0x%llx", static_cast<unsigned long long>(address));
                return std::string(text);
            }
            GetModuleFileNameA(module, name, sizeof(name));
            const char* base = std::strrchr(name, '\\');
            MEMORY_BASIC_INFORMATION info{};
            std::uint64_t extent = 1;
            const auto start = reinterpret_cast<std::uint64_t>(module);
            for (auto cursor = start; VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) != 0 && info.AllocationBase == reinterpret_cast<PVOID>(module); cursor += info.RegionSize) extent = cursor + info.RegionSize - start;
            modules[start] = {base ? base + 1 : name, extent};
            char text[96];
            std::snprintf(text, sizeof(text), "%s+0x%llx", base ? base + 1 : name, static_cast<unsigned long long>(address - start));
            return std::string(text);
        };
        for (const auto& [frames, count] : stacks) {
            std::string line = "stack " + std::to_string(count);
            for (const auto frame : frames) line += " " + describe(frame);
            std::fprintf(file, "%s\n", line.c_str());
        }
        std::fclose(file);
        counts.clear();
        stacks.clear();
        samples = 0;
    }
};

// Debug aid: APS5_SAMPLE_THREADS=<file> samples every thread in the process every 2 ms and, every
// 20 s, writes each busy thread's hottest frames ("module+offset self inclusive") to <file>.
struct ProcessSampler {
    std::string path;
    DWORD self = 0;
    std::map<DWORD, Sampler> threads;
    std::uint64_t rounds = 0;

    std::chrono::steady_clock::time_point enumerated{};

    // The system-wide thread snapshot is slow (tens of ms); the thread list is refreshed every 5 s and
    // the rounds in between only sample the known threads.
    void enumerate() {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        const DWORD process = GetCurrentProcessId();
        for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) continue;
            auto& thread = threads[entry.th32ThreadID];
            if (thread.target == nullptr) thread.target = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        }
        CloseHandle(snapshot);
        enumerated = std::chrono::steady_clock::now();
    }

    void sample() {
        if (threads.empty() || std::chrono::steady_clock::now() - enumerated > std::chrono::seconds(5)) enumerate();
        for (auto& [id, thread] : threads) {
            if (thread.target != nullptr) thread.sample();
        }
        ++rounds;
    }

    void write() {
        std::FILE* file = std::fopen(path.c_str(), "w");
        if (file == nullptr) return;
        std::fprintf(file, "# %llu rounds\n", static_cast<unsigned long long>(rounds));
        for (auto& [id, thread] : threads) {
            std::lock_guard lock(thread.mutex);
            std::vector<std::pair<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>>> hot(thread.counts.begin(), thread.counts.end());
            std::sort(hot.begin(), hot.end(), [](const auto& a, const auto& b) { return a.second.second > b.second.second; });
            std::uint64_t leaves = 0;
            for (const auto& [address, count] : hot) leaves += count.first;
            if (leaves == 0) continue;
            std::fprintf(file, "thread %lu samples %llu\n", static_cast<unsigned long>(id), static_cast<unsigned long long>(thread.samples));
            for (std::size_t i = 0; i < hot.size() && i < 40; ++i) {
                const auto address = hot[i].first;
                HMODULE module = nullptr;
                char name[MAX_PATH] = "?";
                if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module)) GetModuleFileNameA(module, name, sizeof(name));
                const char* base = std::strrchr(name, '\\');
                std::fprintf(file, "  %s 0x%llx %llu %llu\n", base ? base + 1 : name, static_cast<unsigned long long>(address - reinterpret_cast<std::uint64_t>(module)), static_cast<unsigned long long>(hot[i].second.first), static_cast<unsigned long long>(hot[i].second.second));
            }
        }
        std::fclose(file);
    }
};

void StartProcessSampler() {
    const char* path = std::getenv("APS5_SAMPLE_THREADS");
    if (path == nullptr) return;
    processSamplerThread = std::jthread([path = std::string(path)](std::stop_token token) {
        auto sampler = std::make_unique<ProcessSampler>();
        sampler->path = path;
        sampler->self = GetCurrentThreadId();
        auto flushed = std::chrono::steady_clock::now();
        while (!token.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            sampler->sample();
            if (std::chrono::steady_clock::now() - flushed > std::chrono::seconds(20)) {
                sampler->write();
                flushed = std::chrono::steady_clock::now();
            }
        }
        sampler->write();
    });
}

}

void StartWorkerSampler(std::uint32_t queue) {
    std::call_once(processSamplerStarted, StartProcessSampler);
    const char* path = std::getenv("APS5_SAMPLE_WORKER");
    if (path == nullptr) return;
    auto sampler = std::make_unique<Sampler>();
    sampler->queue = queue;
    sampler->threadId = GetCurrentThreadId();
    char suffix[64];
    std::snprintf(suffix, sizeof(suffix), "-q%x-t%lu.txt", queue, static_cast<unsigned long>(sampler->threadId));
    sampler->path = std::string(path) + suffix;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &sampler->target, THREAD_ALL_ACCESS, FALSE, 0)) throw std::system_error(GetLastError(), std::system_category(), "Duplicating sampler thread handle");
    sampler->lastUser = ThreadCpu100ns(sampler->target, sampler->lastKernel);
    std::lock_guard lock(workerSamplersMutex);
    workerSamplerThreads.emplace_back([sampler = std::move(sampler)](std::stop_token token) {
        auto flushed = std::chrono::steady_clock::now();
        const HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        while (!token.stop_requested()) {
            LARGE_INTEGER due{};
            due.QuadPart = -10000;
            if (timer != nullptr && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
            sampler->sample();
            if (std::chrono::steady_clock::now() - flushed > std::chrono::seconds(10)) {
                sampler->write();
                flushed = std::chrono::steady_clock::now();
            }
        }
        sampler->write();
        if (timer != nullptr) CloseHandle(timer);
    });
}

void StopWorkerSampler() {
    processSamplerThread.request_stop();
    std::vector<std::jthread> threads;
    {
        std::lock_guard lock(workerSamplersMutex);
        threads.swap(workerSamplerThreads);
    }
    for (auto& thread : threads) thread.request_stop();
    if (processSamplerThread.joinable()) processSamplerThread.join();
    for (auto& thread : threads) {
        if (thread.joinable()) thread.join();
    }
}
#else
void StartWorkerSampler(std::uint32_t) {}
void StopWorkerSampler() {}
#endif

}
