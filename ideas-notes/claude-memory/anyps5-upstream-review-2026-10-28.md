---
name: anyps5-upstream-review-2026-10-28
description: "State of palworld/play2-h5 vs upstream boykopovar/AnyPS5 main on 2026-10-28; which of our commits are upstream, which upstream work overlaps ours, and the agreed direction"
metadata:
  node_type: memory
  type: project
  originSessionId: 074309bc-434c-4e06-8aa9-4380a224c1be
  modified: 2026-10-28T22:36:35.301Z
---

On 2026-10-28 the user asked whether our optimizations are still better than upstream or whether to rebase. Facts found:
- Branch palworld/play2-h5 (AnyPS5-play2, local only), merge-base with upstream/main = 5d933db02 (2026-10-13). Upstream moved 14,659 commits since; we have 184 (110 non-merge).
- git cherry + subject match: 15 of ours identical upstream, ~25 more (others' PRs) landed reworded; ~15 others' PRs still not upstream; 28 WIP(local) diagnostics; ~20 of our own perf/fix commits not upstream.
- None of our mechanisms exist upstream (excludeBase, GuestAllocationsChangedSince, copyPieces, forgetLog clean set, NoteGpuDmaCopy, RegistryUntouched, uncoveredMisses, draw mirrors, fault debugger). But upstream reworked the same files heavily (GuestBufferMemory.cpp 144 commits/~3.8k lines, ShaderResources 2.8k, VulkanDevice 2.3k, WindowsMappings 40 commits) and did overlapping work: DMA up to 64 KiB on the GPU over pending writes (10-17, = our H10), re-check only imports a registry change can reach (10-26, ~H8), draw snapshot from a containing kept snapshot (10-22), draw finisher pipeline APS5_DRAW_PIPELINE + winpthreads lock/TLS replacement (10-23), pipeline journal built ahead on the next run (10-16, targets the compile hitches), libc private-collect/per-page-state rework.
- Our files GONE upstream: VramBudget (pallp PR, source of our exit crash), PresentationGamma/OutputGamma (Simon PR), FaultDebugger (ours, #361).

**Why:** a mechanical rebase would conflict heavily and duplicate upstream work; whether ours is still faster is unmeasured.
**How to apply:** measure upstream main on Palworld at the fixed spot first (fresh worktree; build notes in [[palworld-anyps5-testing]]), then port only our distinct, measured wins onto main one by one (each its own A/B), which is also the shape the planned issue + PRs need. See [[anyps5-architecture-review-2026-10-14]].
