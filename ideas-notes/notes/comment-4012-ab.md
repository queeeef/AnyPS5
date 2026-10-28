Measured on the Palworld main menu, idle, with and without this change on the same build.

**Setup**
- Windows 11 Pro 26300, RTX 2080 Ti (NVIDIA 610.74), MinGW-w64 GCC 15.2.0 (WinLibs r7), RelWithDebInfo.
- Base: `main` plus the open PRs the title needs (#2648), this PR, and local changes such as #4072. All runs use the same build.
- The "without" runs set a local environment check that makes `MatchZeroDwordFill` return `std::nullopt`, so only this change is off. `APS5_NO_FILL_HLE` would also turn off the existing pattern fills.
- Four runs, alternated with, without, with, without. Each value is the mean of the last four 10 s windows of a run, from `APS5_PROFILE_DRAW` and `APS5_PROFILE_GPU`.
- Key-refresh waits are counted by a local counter around the `SyncThrough` in `WaitForKeyWriters`.

| | Presents / 10 s | Frame time | Key-refresh waits / frame | GPU idle gaps / present |
|---|---|---|---|---|
| With #4012, run 1 | 202.0 | 49.5 ms | 2.0 | 4.6–5.1 ms |
| Without, run 1 | 175.8 | 56.9 ms | 15.0 | 10.7–11.1 ms |
| With #4012, run 2 | 199.2 | 50.2 ms | 2.0 | 4.9–5.2 ms |
| Without, run 2 | 174.5 | 57.3 ms | 15.1 | 10.2–10.9 ms |

- **Frame time:** about 7 ms (13%) lower with the change, in both pairs.
- **Waits:** about 13 fewer per frame. The ones the change removes are on targets whose keys the zero dword fill kernel clears every frame. Most of the waited time was a 960x540 target: 350 waits per 10 s, about 13 ms each.
- **GPU:** each wait drained the queue. With fewer of them, the GPU's idle time per present halves. The scene is GPU-bound (about 43 ms of GPU time per frame), so that idle time is what the frame rate gains.

**What is left.** About 2 waits per frame remain. They are mostly on a 2536x1428 R8 target whose keys another shader writes, about 25 ms each. That shader is a different kernel and not part of this PR.

AI-assisted (Claude Code)
