### What

Part 1 of #ISSUE.

An address-based (BDA) build reads the whole leased address space in place. Until now, `ShaderResources::MarkGpuWrites` appended every in-place range of that space to the open batch's read list on every use. In a Palworld world the address spaces hold 25,000–38,000 ranges, which came to about 50 M range notes per 10 s.

- **One list per space.** A published `AddressSpace` builds its in-place ranges once, as an immutable sorted, disjoint list (`GuestBufferMemory::SpaceInPlaceReads`).
- **Kept once per batch.** `Recorder::NotePendingReadSet` keeps each list once per batch, by identity.
  - A different space in the same batch (a new serial) is kept alongside the first, so no read coverage is lost.
  - A list that is not sorted and disjoint throws.
- **Lookup.** `readOverlap` binary-searches each kept list, after the per-range reads.
- **Per-use reads unchanged.** The build's own in-place regions (`OwnInPlaceReads`) are still noted on every use. A build without a cached space (dissolved, or the space cache off) notes `InPlaceReads()` as before.
- **Other consumers.** The flip read check (`readGeneration`, the completed ring) walks the kept lists too.

The batch tracks the same ranges as before; only the duplicates are gone. When a query overlaps both a per-range read and a kept list, `DescribePendingRead` and the hit counters may report the other reader kind. With `APS5_CAPTURE_TRACE`, a space's `buffer-read` events are logged once per batch, not once per use.

Counters for the follow-ups (`APS5_PROFILE_DRAW`):
- `[dispatch] … phase totals by build kind`: every phase, split into address-based and other builds;
- `[address]`: how many BDA tables were merged with the build's own regions;
- `[recorder]`: kept lists and repeated uses of a list already in its batch.

### Tested

Windows 11 Pro 26300, MinGW-w64 GCC 15.2.0 (WinLibs r7), Ninja, Release, `BUILD_TESTING=ON`, `ANYPS5_ENABLE_SPIRV_TOOLS=ON`, on `main` c7601b797.

- **Build:** clean.
- **Full suite:** `ctest --timeout 120 --parallel 12 -E '^agc_driver_bda_device$'`: 1106 passed, 0 failed, 8 skipped, the same 8 as on `main`. `agc_driver_bda_device` was not run: on this machine it has lost the device before, which also resets the display.
- **`agc_driver_recorder_tests` without arguments** (not registered in CTest):
  - The new read-set check passes. It covers repeated notes, a second space in one batch, range ends and gaps, an in-flight batch, cleanup after the batch, and rejection of an overlapping list.
  - The run then stops at `keyProofTests`: `(9) a scan under a pending write was kept`. It stops at the same point on `main` (checked at ea2b2d364).
  - With `keyProofTests` skipped locally, every later check passes.
- **Repo checks:** `check_conventions.py --base upstream/main` and `git diff --check` pass.

**Palworld (PPSA23126), open world.** `main` does not reach the world in this title without local workarounds. So this commit was cherry-picked (cleanly) onto a play branch: `main` 5d933db02 plus open PRs and those workarounds. Each build ran once into the world with `APS5_PROFILE_DRAW=1`. The table covers only the in-world 10-second windows: 6 without this change, 5 with it.

There are no `APS5_TRACE_FRAMES` summary lines: the play branch predates the performance trace.

| In world, per 10 s | Without | With this PR | Change |
|---|---|---|---|
| In-place read ranges noted | ~50 M | ~11 M | −78% |
| `record: marks` per dispatch | ~160 µs | ~31 µs (address-based), ~9 µs (other) | −80% to −95% |
| Device call per dispatch | 690–830 µs (avg ~760) | 600–685 µs (avg ~650) | −15% |
| Driver total per dispatch | ~820 µs | ~700 µs | −14% |
| Dispatches per 10 s | ~4,210 | ~4,560 | +8% |
| GPU mutex held by queue 0x0 | 8.1–8.3 s | 8.1–8.2 s | no change |
| Presents per 10 s | 14–17 (avg 15.3) | 16–17 (avg 16.2) | within run-to-run noise |
| GPU busy per present | 346–362 ms | 347–396 ms | no change |

About 39 M range notes per 10 s are gone. FPS does not change yet:
- the freed ~0.5 s per 10 s goes into processing more of the queued dispatches;
- queue 0x0 still holds the GPU mutex about 8.1 s of every 10 s;
- the GPU is busy about 370 ms per frame.

The overlap queries never ran in this scene (`[recorder] … 0 queries`), so all of the saving is in building and appending the list.

Not tested: Linux, other GPUs, other titles.

### Checklist
- [x] Based on current `main`; no other open PR implements the same functions
- [x] One topic per PR; follow-ups go in a new PR
- [x] No comments in code except [technical debt](https://github.com/boykopovar/AnyPS5/blob/main/docs/dev/CONVENTIONS.md)
- [x] Unimplemented paths throw (`NotImplemented_nid_no_patch`); silent stubs are listed under Silent stubs in their library's [technical debt](https://github.com/boykopovar/AnyPS5/tree/main/docs/dev/debt) file
- [x] No notes, investigation `.md` files or images added to the repository (attach them to this PR)
- [x] New third-party code is a submodule built from source
- [x] Generic behaviour, not specific to one title
- [x] Depends on: none
- [x] Unblocks: none (part 1 of #ISSUE)
- [x] GPUs tested: NVIDIA GeForce RTX 2080 Ti, driver 610.74, Windows 11 Pro 26300
- [x] AI-assisted: yes (Claude Code)

🤖 Generated with [Claude Code](https://claude.com/claude-code)
