### What

Moves `3rdparty/SPIRV-Tools` from v2025.4 to **v2025.5** and `3rdparty/SPIRV-Headers` from `01e057791` to **`b824a462d`**, the revision v2025.5's `DEPS` pins.

- **Why:** v2025.4 and the current SPIRV-Headers predate `SPV_KHR_fma`. With `ANYPS5_ENABLE_SPIRV_TOOLS=ON`, spirv-val rejects the `FMAKHR` capability (`Invalid capability operand: 6030`), and `spv::OpFmaKHR` / `spv::CapabilityFMAKHR` do not exist. adriadam10 found this in the review of #4072.
- **Why this version:** v2025.5 is the first release with `spirv-val: Add validation for SPV_KHR_fma` (`8f9381775`). `b824a462d` contains the `SPV_KHR_fma` headers update (`9268f3057`). It is the smallest step from the current pins that has both; later releases (v2026.1 to v2026.4) also have them.
- **glslang** is built with `ENABLE_OPT OFF` and keeps its own SPIR-V headers, so it is unaffected.

No source changes. #4072 is rebased onto this and drops its own definitions of the opcode and capability.

### Tested
Windows 11 Pro 26300, MinGW-w64 GCC 15.2.0 (WinLibs r7), Ninja, Release, `BUILD_TESTING=ON`, `ANYPS5_ENABLE_SPIRV_TOOLS=ON` (the Linux CI configuration), on `main` `0214069e3`.

- **Build:** clean.
- **Full suite:** `ctest --timeout 120 --parallel 12 -E '^agc_driver_bda_device$'`: **1102 tests passed**, 0 failed, 8 skipped.
  - `check_skips.py --job windows` flags one of the 8: `guest_video_out_high_frame_rate` (`current display does not provide 120 Hz`). It is a property of this monitor, not of the change.
  - `agc_driver_bda_device` was not run: an older base lost the device in it on this machine, which also resets the display.
- **Repo checks:** `check_conventions.py --base 0214069e3` and `git diff --check` pass.

Not tested: Linux, `ANYPS5_USE_SYSTEM_DEPS`.

### Checklist
- [x] Based on current `main`; no other open PR implements the same functions
- [x] One topic per PR; follow-ups go in a new PR
- [x] No comments in code except [technical debt](https://github.com/boykopovar/AnyPS5/blob/main/docs/dev/CONVENTIONS.md)
- [x] Unimplemented paths throw (`NotImplemented_nid_no_patch`); silent stubs are listed under Silent stubs in their library's [technical debt](https://github.com/boykopovar/AnyPS5/tree/main/docs/dev/debt) file
- [x] No notes, investigation `.md` files or images added to the repository (attach them to this PR)
- [x] New third-party code is a submodule built from source
- [x] Generic behaviour, not specific to one title
- [x] Depends on: none
- [x] Unblocks: #4072
- [x] GPUs tested: NVIDIA GeForce RTX 2080 Ti, driver 610.74, Windows 11 Pro 26300
- [x] AI-assisted: yes (Claude Code)

🤖 Generated with [Claude Code](https://claude.com/claude-code)
