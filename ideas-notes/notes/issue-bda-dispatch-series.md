### Scene

PPSA23126 (Palworld), open world after loading a save. Windows 11 Pro 26300, NVIDIA GeForce RTX 2080 Ti (driver 610.74). Measured with `APS5_PROFILE_DRAW=1` counters on a play branch: `main` at 5d933db02 plus open PRs and local workarounds the title needs to reach the world. `main` alone does not reach the scene.

### What is slow

In the world the game runs at about 1.5 fps.
- **GPU:** busy about 350–390 ms per present.
- **Dispatch worker (queue 0x0):** holds the GPU mutex 7.6–8.3 s of every 10 s. Most of that is the dispatch device call, 0.65–1.1 ms per dispatch, about 4,000–4,500 dispatches per 10 s.
- **Address-based builds:** most of the device call goes to dispatch builds whose shader uses a BDA page table. `noteReusable` never caches these (`usesBda`, `HoldsLease()`), so each one is built again on every dispatch.
  - In one world session, 3,200 of 4,100 dispatch builds per 10 s were address-based.
  - Every build maps the whole registered address space. In this scene that is 25,000–38,000 ranges.

Per dispatch in the world, with the read notes of #PR1 already removed (8,546 address-based and 13,454 other dispatches; the rows overlap, so they do not add up to the total):

| Address-based | µs | Other | µs |
|---|---|---|---|
| resources (whole build) | 849 | resources (whole build) | 455 |
| A locked (bda): stage A under the mutex | 318 | hook waits: GPU waits inside the call | 272 |
| B bda+other: BDA table and fault buffer | 267 | B upload | 230 |
| B upload | 241 | revalidate | 206 |
| address bindings (bda): lease and mirrors | 158 | | |
| B images | 139 | | |

The BDA table cache never serves a table by the address space's serial (`space tables 0` on the `[address]` line). Every table is hashed and compared against the held ones, and 22% of them miss.

### Plan

One change per PR, each measured on its own counter, ordered by measured cost:

1. **#PR1: note an address space's in-place reads once per batch.** `MarkGpuWrites` appended the whole space to the batch's read list on every use, about 50 M range notes per 10 s. Exact: the batch tracks the same ranges.
2. **Serve the BDA table by the address space's serial, and pool the fault buffers.** A build whose own regions and write intervals match an earlier one then binds the same table without the merge, hash and compare. Target: the 267 µs row.
3. **Reusable address-based templates.** Requires all of:
   - the address space's serial is unchanged;
   - writable mirrors are refreshed per use, as for a cached space today;
   - each use gets its own fault buffer, checked at its completion;
   - the cache holds the space so the pin waiter can drop it.

   Target: most of the 318 µs locked stage A and the uploads.
4. **Templates keyed without read-only buffer addresses, with those buffers rebound per use**, as draws already do (`ContentKey(..., movableBuffers)`, `MovedReadOnlyBuffers`), extended to address-based builds after 3.
5. **The dispatch cache in `Dispatch.cpp`**, keyed by user-data words: about 7% hits in the world. The lowest measured cost (recompile phase about 0.5 s per 50,000 world dispatches).

FPS will not move much until the dispatch worker stops holding the GPU mutex for most of each frame. The GPU time per frame is not covered by this series. It is about 370 ms; in an earlier capture about 240 ms of it was draws, and the most expensive single programs were compute shaders of over 200,000 SPIR-V words.

AI-assisted (Claude Code).
