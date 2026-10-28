# AGC driver: per-epoch change sets and a pipelined translator

Status: proposal, 2026-10-14. Measured on Palworld (PPSA23126), in the open world, with the
`palworld/play2-h5` line of builds (H5 → H6Z) in `AnyPS5-play2`.

## 1. Why this document

Since H5 the work has gone trace by trace: find the hottest symbol, add a counter, remove one
local defect. Those fixes are exact and real, but each buys 5–15%. The game runs at about
2.5 fps; 30 fps needs a frame about 12× shorter. The measurements below show why local fixes
cannot close that gap, and name the two architectural changes that can.

## 2. Hardware and what it implies

| Part | Spec | Consequence |
|---|---|---|
| CPU | i5-13600K: 6 performance + 8 efficiency cores, 20 threads | One translator thread uses one of 14 cores. |
| RAM | 64 GB | No memory pressure: working set ~17 GB, no trimming. |
| GPU | RTX 2080 Ti, 11 GB VRAM, PCIe 3.0 (~12 GB/s to host), no Resizable BAR | Every GPU read of a host import crosses PCIe; the CPU cannot map most of VRAM. |

The PS5 has one unified GDDR6 pool. The driver keeps guest memory in host RAM and imports it
into Vulkan (`VK_EXT_external_memory_host`), so unified-memory semantics are emulated on split
memory, at a cost on both sides.

## 3. Where the frame goes now (H6Y/H6Z)

Steady in-world windows, per 10 s, about 25 presents:

- **GPU busy ~150 ms per present** (was ~363 ms before the H6 vertex/index VRAM mirrors).
- **Idle GPU ~220–260 ms per present**, waiting on the CPU.
- **Draw worker (queue 0x0): 85–87% of one core**, by cycle count, with no sampler attached.
  About 5,500 draws and 700 dispatches per second, ~69 µs per draw on average.
- Of that CPU time, 2.1–3.2 s per 10 s is kernel time (real; measured with the sampler off).

What the draw worker spends its time on (H6Y stacks, every sample classified by the first
matching category):

| Kind of work | Share of worker |
|---|---|
| Shader capture and specialization | 19% |
| Guest-allocation and import bookkeeping (`HostImportFor`, address spaces, regions) | 18% |
| Copying guest data (reads, snapshots, uploads) | 17% |
| PM4 packet decode | 8% |
| Waiting on the GPU and locks | 5% |
| Change tracking and template revalidation (collects, `Revalidate`, refreshes) | 7% |
| Building resources (descriptors, BDA tables) | 3% |
| **Recording Vulkan commands** | **4.4%** |
| Other | 18% |

About 95% of the frame-limiting thread is emulation overhead around the Vulkan recording, it
runs per command, and it runs on one core.

## 4. The defects

### D1. One translator thread

Every draw and dispatch of a queue is translated on that queue's worker, start to finish, in
packet order, mostly under `GuestMemory::GpuMutex`. Capture, specialization, template lookup,
import lookup and vertex planning are independent per draw; only the final recording and the
recorder's hazard bookkeeping depend on order. On a 6P+8E CPU, one core does all of it.

### D2. Memory state is re-proven per draw

The game changes memory between submissions, but every draw re-proves its inputs:
write-watch collects over the same buffers (`PrepareDrawBindings`, `fastRevalidate`,
`refreshProved`), import lookups (`HostImportFor` per vertex and index range), snapshot
compares, address-space region lists per build. H6U (import negative cache) and H6Y (collect
memo as merged intervals) each removed one slice of this and each gave about +10%.

### D3. Unified-memory semantics on split memory

GPU side: buffers bound in place from host imports are read over PCIe. Mirroring vertex and
index data into VRAM cut GPU time from ~363 to ~150 ms per present (H6). V# storage buffers and
BDA reads still cross PCIe.

CPU side: exactness for shared guest pages is kept by page protection. A write-watch collect
re-arms a shared alias page with `VirtualProtect`; the game's next write faults through the
fault debugger; the next collect re-arms it. Measured per 10 s on the worker: ~15k re-arms,
~12k debugger-resolved faults (on game threads), ~280k collect calls.

### D4. No frame-to-frame reuse

Each frame re-translates ~2,100 draws that mostly repeat. The draw recipe replay records 0 hits.
Replaying a recorded sequence safely needs cheap proof that its inputs are unchanged, which is D2.

## 5. Exactness rules this design must keep

- No approximation: a cached result is used only when the inputs it was derived from are proven
  unchanged.
- CPU writes to write-watched memory are seen at the next collect epoch. A write that lands
  between two collects of the same epoch is seen by the next epoch (the existing memo rule in
  `GuestMemory.hpp`; it mirrors the ordering real hardware gives a CPU write made while a
  submission runs).
- On Windows, direct host imports are unwatched and use comparisons; separate shared aliases keep
  guest write tracking. A Heap 0 copy of an unwatched range is reused only after a comparison.
- GPU and driver writes stamp their blocks immediately (`MarkWritten`, `StoreOwnBytes`); pending
  recorded writes are visible through the recorder (`PendingWriteOverlaps`).
- Registry changes (map, unmap, protect) bump the allocation generation under the registry lock.
- Unit shadows: a fresh shadow holds newer bytes than its import; readers publish or fall back.

## 6. Design A (D2 first): per-epoch change sets

### Idea

Replace "every consumer walks its own ranges, every draw" with "the driver computes what changed
once per collect epoch, and every consumer compares versions".

### Model

- **Epoch.** Unchanged: collect epochs are bumped at the queue's ordering points (submission
  start, a WAIT_REG_MEM the CPU may have satisfied, a device drain, before reaping completions).
- **Interest set.** Consumers register the guest ranges whose contents they depend on: resource
  templates, draw snapshots, VRAM mirrors, address spaces, cached surfaces. A registration is
  `(range, consumer, version)`. Registrations persist across epochs and are dropped with their
  consumer.
- **Change set.** At the first use in a new epoch, the driver collects the interest set once
  (merged intervals; one walk per merged run, not per consumer), applies comparisons for
  unwatched ranges, and records the changed pages as an interval set tagged with the epoch.
  Driver and GPU stamps keep stamping the tracker immediately, as today.
- **Consumer check.** "Is my range unchanged since version v?" becomes an interval lookup in the
  tracker's generations, with no walk and no syscall, because the walk already happened this
  epoch. A range outside the interest set falls back to today's collect (and is then added).

### Why it is exact

The guarantee is the one the H6Y memo already gives: every page of a checked range was walked in
this epoch, so any CPU write before the epoch's ordering point is stamped. GPU, driver and
registry changes are stamped or versioned immediately and still checked per use. Nothing is
inferred from timing.

### Shared aliases

Collect re-arms only interest pages, once per epoch, instead of on every overlapping collect. A
page the game rewrites every frame stops ping-ponging between re-arm and fault more than once per
epoch. A follow-up can keep frequently written pages disarmed and track them by comparison;
that changes fault semantics, so it is measured separately.

### Expected effect

The tracking, revalidation and part of the bookkeeping categories (roughly 25–35% of the
worker), plus part of the real kernel time. This is an estimate to verify, not a measurement.

### Revision after measuring step 1 (H7)

The worker bumps its collect epoch ~250 times per frame, so collecting an interest set once per
epoch would cost more than the lazy walks it replaces. libc already holds the change set:
`cleanRanges` (shared-view ranges re-armed by a walk and not touched since), and every way a page
can change (write fault, host write, pin, protection change, unmap) goes through `forgetClean`.
Step 1 therefore runs the other way round: libc logs every forgotten range; each thread keeps the
ranges its walks left clean, persistent across epochs, and drains the log at its first collect of
an epoch. The interest registry is the set itself (populated by walks, nothing to register), and
the drain costs O(changes), not O(ranges). Measured beforehand: `collectable()` in libc looked up
every 16 KiB view of a range before each resetting walk, even over clean ranges (~4% of the
worker); that is fixed alongside. Ceiling of step 1: all collect time, ~9% of the worker (libc
`Collect` 7.5% by cycle count plus driver overhead). The larger D2 slices are allocation leases and
region lists (step 3) and snapshot copies.

### Steps (each an A/B on Palworld, each exact on its own)

1. Per-thread clean set kept across epochs, invalidated by libc's forget log (all collect
   callers at once; see the revision above). Done as H7: d825faba1 + 2aac17f2f.
2. Move the VRAM mirrors and `refreshProved` onto it.
3. Version the import map and allocation registry so `HostImportFor`, `AcquireRegistered` and
   address-space hits check versions instead of rebuilding region lists (`dissolveSpace` copies
   ~12k regions ~200 times per 10 s today).
4. Batch shared-alias re-arms per epoch.

## 7. Design B (D1): pipelined translation

### Idea

Split the work per command into a pure **prepare** stage that runs ahead on other cores, and an
ordered **record** stage that stays on the queue worker. The record stage stays authoritative.

### Stages

- **Prepare (parallel, no GPU mutex).** For packets ahead in the same submitted command buffer:
  decode draw state, compute shader keys, look up or build capture and specialization, compute
  resource template keys, plan vertex and index ranges, resolve import lookups. Each result
  carries the versions it was derived from: collect epoch and change-set versions of the ranges
  it read, allocation generation, import map version, and a hash of the register state.
- **Record (serial, queue worker, under the GPU mutex).** Checks the versions (O(1) each with
  Design A). If they all match, it records with the prepared result. If any differ, it redoes
  that step inline, which is today's path.

### Why it is exact

A prepared result is a hint. It is used only when every input it depended on is provably the
same as at record time; otherwise the record stage recomputes it exactly as today. Order-dependent
state (recorder hazards, pending writes, labels, render pass continuity) is only touched in the
record stage.

### What has to be true first

- Design A, so record-stage checks are cheap version compares rather than walks.
- Capture and specialization must not read guest memory the prepare stage cannot version.
  Shader code pages and user-data words read during capture register interest like any consumer.
- The existing dispatch "stage A" (unlocked build) is the precedent; draws do not use it yet.

### Threading on this CPU

Record stays on one performance core. Prepare runs on a small pool (2–4) pinned to the other
performance cores; efficiency cores are left to the game. The pool works only within the current
submission, so it never runs ahead of a guest synchronization point.

### Expected effect

Capture, specialization, decode, template and import lookups (~45–60% of the worker) leave the
critical thread. Combined with Design A, the CPU frame could approach the GPU frame (~150 ms),
about 6–7 fps. This is an estimate to verify, not a measurement.

## 8. What comes after

- **D3, GPU side:** extend the VRAM mirrors from vertex and index data to read-only V# and BDA
  ranges, using the same epoch change sets; keep GPU-produced results resident (unit shadows
  already do this for storage images).
- **D4:** replay recorded draw sequences when Design A proves their inputs unchanged.

## 9. How each step is measured

- Same in-world spot, alternating A/B runs on the same machine; FPS alone is noisy across scenes.
- Per 10 s: presents, GPU busy and idle per present (`[present]`), worker CPU by cycle count with
  the sampler off (`APS5_SAMPLE_WORKER_CPU_ONLY=1`), collect walks by caller
  (`[local-collect-callers]`), arena re-arms and faults (`[local-arena-thread]`), import lookups
  (`[local-import-lookups]`).
- Stacks only with the safe sampler (copies the suspended thread's stack, unwinds after resume).
  It inflates kernel time by charging suspend APCs; use CPU-only mode for kernel numbers.
- Full `agc_*` ctest after each step; the known play2 failures (16, listed in the play2 notes)
  must stay the only ones.

## 10. Risks

- Interest sets grow without bound if consumers never drop them; registrations must die with
  their consumer, with a cap and fallback to direct collects.
- Prepare results that depend on guest memory nobody versions would be unsafe; such inputs must
  either register interest or force the inline path.
- Multi-queue: change sets are per collect epoch, and epochs are per worker thread. A consumer
  shared by queues must check against the epoch of the queue using it.
- Upstream scope: both designs touch core driver structure and need maintainer agreement before
  PRs.

## Appendix: state of the work (2026-10-14)

- Branch `palworld/play2-h5` in `AnyPS5-play2`, all local, unpushed. Functional commits on top
  of H5: chunked VRAM draw-input mirrors with a batch prologue (`APS5_LOCAL_DRAW_MIRROR=1`),
  import negative cache (c04aa0aee), mirror range collects once per epoch (cf81ae799), collect
  memo as merged intervals (251240fd1). The rest are `WIP(local)` diagnostics.
- Upstream branch `perf/agc-dcc-keys-stale-shadows` in `AnyPS5-recipe`: 02f54cc7d + 9b8687aac
  (DCC keys over pending fills), full ctest 1,155 pass.
- Launchers in `run-dev`: `ab-h6.cmd` (mirror only), `ab-h6x.cmd`, `ab-h6y.cmd` (current best,
  with sampler), `ab-h6z.cmd` (current best, CPU-only accounting). H6W–H6Z install their own
  `libc.prx`; the original is at `run-dev\libs\libc.prx.before-arena-counters`.
- Results: H5 21–22 presents per 10 s → H6Y/H6Z 24–28; GPU per present 363 → ~150 ms.
