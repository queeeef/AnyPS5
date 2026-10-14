#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_GUESTALLOCATIONS_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_GUESTALLOCATIONS_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <mutex>
#include <vector>

namespace GuestAllocations {

struct Range {
    std::uint64_t address;
    std::size_t bytes;
    bool readable;
    bool writable;
    std::uint64_t allocationAddress;
    std::size_t allocationBytes;
    bool releasable = true;
    bool gpu = false;
};

using Lease = std::vector<std::shared_ptr<const Range>>;
using Mapped = std::vector<std::weak_ptr<const Range>>;

extern "C" {
void* GuestAllocationsBegin_nid_postfix();
void GuestAllocationsRegisterMainImage_nid_postfix(void* mutation);
#ifdef _WIN32
void GuestAllocationsRegisterImage_nid_postfix(void* mutation, const void* image);
void GuestAllocationsUnregisterImage_nid_postfix(void* mutation, const void* image);
#endif
void GuestAllocationsEnd_nid_postfix(void* mutation) noexcept;
void GuestAllocationsAdd_nid_postfix(void* mutation, void* pointer, std::size_t bytes, bool readable, bool writable, bool gpu);
void GuestAllocationsRequireUnpinned_nid_postfix(void* mutation, const void* pointer, std::size_t bytes);
void GuestAllocationsRequireAvailable_nid_postfix(void* mutation, const void* pointer, std::size_t bytes);
bool GuestAllocationsCovers_nid_postfix(void* mutation, const void* pointer, std::size_t bytes);
bool GuestAllocationsOverlaps_nid_postfix(void* mutation, const void* pointer, std::size_t bytes);
Range GuestAllocationsFind_nid_postfix(void* mutation, const void* pointer);
void GuestAllocationsRemove_nid_postfix(void* mutation, const void* pointer);
void GuestAllocationsProtect_nid_postfix(void* mutation, const void* pointer, std::size_t bytes, bool readable, bool writable, bool gpu, const std::function<void()>& apply);
void GuestAllocationsUnmap_nid_postfix(void* mutation, const void* pointer, std::size_t bytes, const std::function<void(const void*, std::size_t, const void*, bool)>& apply);
Lease GuestAllocationsAcquire_nid_postfix();
Lease GuestAllocationsAcquireAll_nid_postfix();
std::shared_ptr<const Range> GuestAllocationsLookup_nid_postfix(std::uintptr_t address, bool next);
// Changes whenever a mutation ends, so callers can cache facts about guest mappings between changes.
std::uint64_t GuestAllocationsGeneration_nid_postfix();
// The ranges changed after generation `since`, as (generation, begin, end) triples in generation
// order: every range a mutation ending later recorded (the ranges the invalidator is given), so a
// cache of facts about ranges a change did not touch stays valid across it. Returns the generation
// the triples reach; *count receives their number, or SIZE_MAX when the log no longer holds every
// change after `since` or `capacity` triples are too few (the caller then drops its cache).
std::uint64_t GuestAllocationsChangedSince_nid_postfix(std::uint64_t since, std::uint64_t* triples, std::size_t capacity, std::size_t* count);
// A lease of the readable ranges overlapping or touching [begin, end), extended on both sides
// through every neighbor that continues a run with it (contiguous, readable, same writability and
// GPU flag: the run rule of the driver's host imports), in address order like Acquire's: what a run
// lookup around the range reads of a full lease. *complete is false (and the lease empty) when more
// than `limit` ranges would be returned.
Lease GuestAllocationsAcquireAround_nid_postfix(std::uintptr_t begin, std::uintptr_t end, std::size_t limit, bool* complete);
// Precise invalidation for such caches: the callback runs, after the generation changed, for every
// range whose mapping or protection changed (registry mutations and guest heap decommits).
void GuestAllocationsSetInvalidator_nid_postfix(void (*callback)(std::uintptr_t address, std::size_t bytes));
void GuestAllocationsInvalidate_nid_postfix(std::uintptr_t address, std::size_t bytes);
// A mutation of a range that GPU work still leases waits for the lease (RequireUnpinned). The
// waiter, set by the GPU driver, is called on the mutating thread with the registry lock released
// and finishes the GPU work that holds leases; it returns whether it finished any (a round that found
// nothing to wait for counts against the same bound as a plain spin). Without one the mutation spins
// until the lease is dropped by another thread.
void GuestAllocationsSetPinWaiter_nid_postfix(bool (*callback)(std::uintptr_t address, std::size_t bytes));
void GuestAllocationsSetGpuMapObserver_nid_postfix(void (*callback)(const Mapped& ranges, std::uint64_t generation));
void GuestAllocationsNoteGpuMapping_nid_postfix(void* mutation, const void* pointer, std::size_t bytes);
}

class Mutation {
public:
    Mutation() : handle(GuestAllocationsBegin_nid_postfix()) {}
    ~Mutation() { GuestAllocationsEnd_nid_postfix(handle); }
    Mutation(const Mutation&) = delete;
    Mutation& operator=(const Mutation&) = delete;
    void RegisterMainImage() { GuestAllocationsRegisterMainImage_nid_postfix(handle); }
#ifdef _WIN32
    void RegisterImage(const void* image) { GuestAllocationsRegisterImage_nid_postfix(handle, image); }
    void UnregisterImage(const void* image) { GuestAllocationsUnregisterImage_nid_postfix(handle, image); }
#endif
    void Add(void* pointer, std::size_t bytes, bool readable, bool writable, bool gpu) { GuestAllocationsAdd_nid_postfix(handle, pointer, bytes, readable, writable, gpu); }
    void RequireUnpinned(const void* pointer, std::size_t bytes) const { GuestAllocationsRequireUnpinned_nid_postfix(handle, pointer, bytes); }
    void RequireAvailable(const void* pointer, std::size_t bytes) const { GuestAllocationsRequireAvailable_nid_postfix(handle, pointer, bytes); }
    bool Covers(const void* pointer, std::size_t bytes) const { return GuestAllocationsCovers_nid_postfix(handle, pointer, bytes); }
    bool Overlaps(const void* pointer, std::size_t bytes) const { return GuestAllocationsOverlaps_nid_postfix(handle, pointer, bytes); }
    Range Find(const void* pointer) const { return GuestAllocationsFind_nid_postfix(handle, pointer); }
    void Remove(const void* pointer) { GuestAllocationsRemove_nid_postfix(handle, pointer); }
    void Unmap(const void* pointer, std::size_t bytes, const std::function<void(const void*, std::size_t, const void*, bool)>& apply) { GuestAllocationsUnmap_nid_postfix(handle, pointer, bytes, apply); }
    void Protect(const void* pointer, std::size_t bytes, bool readable, bool writable, bool gpu, const std::function<void()>& apply) { GuestAllocationsProtect_nid_postfix(handle, pointer, bytes, readable, writable, gpu, apply); }
    void NoteGpuMapping(const void* pointer, std::size_t bytes) { GuestAllocationsNoteGpuMapping_nid_postfix(handle, pointer, bytes); }

private:
    void* handle;
};

}

#endif
