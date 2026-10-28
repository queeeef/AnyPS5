AGC: a buffer that crosses registered ranges outside a whole import window is copied on the CPU at every use (Palworld: 15–56 GB per 10 s)

**Palworld (PPSA23126, 01.102.999), character creation.** Buffers that cross from one registered guest range into the next get no host import, so `GuestBufferMemory::copyRegion` copies them on the CPU at every upload. The `[buffers]` profile line (`APS5_PROFILE_DRAW`) counts them as "outside imports":

| 10 s window at character creation | Copied outside imports |
|---|---|
| 1st | 15.5 GB |
| 2nd | 15.4 GB |
| 3rd | 16.1 GB |
| later in the same run | 38–56 GB |

The scene runs at 6–7 presents per 10 s. Some of the regions, named by a local diagnostic in `UploadFinish`:
- `0x3024fc0000+0xee40000` (238 MiB): copied once per frame, by a `DISPATCH_DIRECT` buffer upload.
- `0x3016970000+0x2f0000` (3 MiB) and its neighbours: the regions in `0x3010000000`–`0x301fffffff` were copied 9076 times in one run, 26.7 GB.
- `0x3137d50000+0x2000000`, `0x3030a40000+0x1f15320`, `0x31a6710000+0x620000` and about 40 more, 256 KiB to 32 MiB.

**Two causes, one after the other:**

1. **No span over the crossing.** Since #3425 (for #3158), adjacent ranges are joined into one import only when they fill a whole aligned window (8 MiB). A region that crosses two ranges whose window is not full, or that crosses a window edge, fails `runCovers` in `UploadFinish` and takes the copy path. `importWindowTests` checks this on purpose ("a view crossing a window edge gets no import"). #3158's reason still holds: a span that is re-imported on every heap growth cost Ratchet & Clank about 1.5 TiB while loading.
2. **On Windows, the span's alias fails.** Direct memory is mapped as shared views of a section, so a span import needs one read-write alias (`GuestArenaMapAlias`). `WindowsMappings::MapAlias` accepts only one contiguous run of offsets in one section. Neighbouring ranges in Palworld map pages at offsets that do not continue, so it refuses with "the range is not one contiguous run of views of a section". Even when cause 1 is fixed locally, 43 of these spans still fail here and stay copied (2–56 GB per 10 s).

**With both fixed** (one import over the ranges a crossing region touches, and an alias made of one view per run of offsets), the same scene copies 0 bytes outside imports and runs at 9–13 presents per 10 s.

**Proposed fix** (a PR follows):
- **Spans only where needed.** A span is made only for a region that crosses ranges, over the whole contiguous ranges it touches (grown over the imports it overlaps so that two spans never retire each other). Complete windows keep #3425's behaviour.
- **#3158's protection is kept by a budget.** The bytes imported by spans count against a budget per 10 s (`APS5_HOST_IMPORT_SPAN_MIB`, default 1 GiB). A title that grows a heap in small steps can re-import spans at most up to the budget, and past it crossing regions are copied as today.
- **The alias is made of placeholders.** It covers several runs of section offsets: a placeholder reserved over the whole span, split into one view per run, as `WindowsMappings` already does for the guest's own views.

**Setup**
- Windows 11 Pro 26300, RTX 2080 Ti (NVIDIA 610.74), i5-13600K
- MinGW-w64 GCC 15.2.0 (WinLibs r7), RelWithDebInfo
- `main` plus the open PRs Palworld needs to reach character creation ([`palworld/integration`](https://github.com/queeeef/AnyPS5/tree/palworld/integration), listed in #2648)

No game files are shared, only log lines. AI-assisted (Claude Code).
