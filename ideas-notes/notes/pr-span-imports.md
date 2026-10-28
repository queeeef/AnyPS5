### What

Fixes #4260: a buffer that crosses registered guest ranges outside a whole import window has no host import, so it is copied on the CPU at every upload. In Palworld that is 15–56 GB per 10 s at character creation.

Two commits, one per cause in the issue:

- **`fix(libc)`: alias over several section runs.**
  - Before: `WindowsMappings::MapAlias` refused a range whose pages map offsets that do not continue, so a span import over neighbouring direct-memory ranges failed on Windows.
  - Now: a placeholder is reserved over the whole range and split into one view per run of section offsets, the way `WindowsMappings` maps the guest's own views. `UnmapAlias` unmaps each view.
  - A range made of one run is mapped as before.
- **`perf(agc-driver)`: span imports for crossing regions.**
  - **When:** `UploadFinish` makes a span only for a region that `runCovers` refuses (it crosses ranges outside a whole window). The span covers the whole contiguous ranges the region touches, and grows over the imports it overlaps so that two spans never retire each other. Complete windows keep #3425's behaviour.
  - **Lifetime:** a span stays valid while each of its ranges is still the registered one (`refreshImports`). `HostImportFor` finds it after a registry change. A retiring span publishes its shadowed units over every registered range it covers (`registeredCovers`), not only over a window's run.
  - **#3158's bound:**
    - The bytes spans import count against a budget per 10 s: `APS5_HOST_IMPORT_SPAN_MIB`, default 1 GiB. `SetHostImportSpanBudget` sets it for tests.
    - Past the budget, a crossing region is copied as today, so a heap that grows in small steps can re-import spans at most up to the budget.
    - A failed span is not retried while its first range stays registered.

### Tested

Windows 11 Pro 26300, RTX 2080 Ti (NVIDIA 610.74), MinGW-w64 GCC 15.2.0 (WinLibs r7), Release, `BUILD_TESTING=ON`, at `main` `34a16b7f0`.

- **New tests:**
  - **`importWindowCrossingTests` (`agc_driver_graphics`).** With an import window of two ranges and three ranges registered, a view crossing the window edge:
    - is copied, with the right bytes, while the span is over the budget;
    - is bound in place in one import over the three ranges;
    - makes that span the import of the window inside it;
    - is not imported twice by a second upload;
    - after one range is removed, has no span, and its window is imported on its own again.
  - **`CheckReadWriteAliasOfReorderedDirectPages` (`guest_memory`).** Two direct-memory pages mapped in reverse order get one alias. Writes go through in both directions, and the alias unmaps.
  - **Without the fix:** RESULT_WITHOUT
- **Existing tests:** `importWindowTests`, `importSpanTests` and `importCrossingTests` pass unchanged: a view crossing a window edge still gets no import from `HostImportFor` alone. Suite: SUITE_RESULT
- **Repo checks:** `check_conventions.py` and `git diff --check` pass.

### Performance

Palworld (PPSA23126) at character creation, RTX 2080 Ti, `APS5_PROFILE_DRAW`. One build; a local switch that turns both changes off gives the "without" runs. The base is `main` plus the open PRs the title needs to reach character creation (#2648).

PERF_TABLE

Not tested: Linux (the span path is shared, the alias is Windows-only), and Ratchet & Clank, whose growth #3158 bounds.

### Checklist
- [x] Based on current `main`; no other open PR implements the same functions
- [x] One topic per PR; follow-ups go in a new PR
- [x] No comments in code except [technical debt](https://github.com/boykopovar/AnyPS5/blob/main/docs/dev/CONVENTIONS.md)
- [x] Unimplemented paths throw (`NotImplemented_nid_no_patch`); silent stubs are listed in [TechnicalDebt](https://github.com/boykopovar/AnyPS5/blob/main/docs/dev/TechnicalDebt.md#silent-stubs)
- [x] No notes, investigation `.md` files or images added to the repository (attach them to this PR)
- [x] New third-party code is a submodule built from source
- [x] Generic behaviour, not specific to one title
- [x] Depends on: none
- [x] AI-assisted: yes (Claude Code)

🤖 Generated with [Claude Code](https://claude.com/claude-code)
