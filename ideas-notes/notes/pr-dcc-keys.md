### What

Fixes #4552. `textureClearKeys` reads a sampled texture's DCC keys through a raw pointer, past the flush hook, so GPU writes the recorder holds for the key range are not in the bytes yet. A title that clears a target's keys with a kernel and samples the target in the same batch had its keys read from the memory's previous contents. In Palworld those were float data from a reused transient heap, so the keys read as mixed and the texels were read as stored.

- **Wait only when the keys don't decode.** That is, when they are neither uncompressed nor a clear the format can fill. Then `textureClearKeys` calls `WaitForKeyWriters`, the function `ProvedKeys` already uses for storage images, and reads the keys again.
- **Known values first.** `WaitForKeyWriters` answers from the pending fills and stores it knows, and otherwise waits for the batch that writes the keys.
- **Keys that decode are used as before**, with no wait.

### Tested

Windows 11 Pro 26300, RTX 2080 Ti (NVIDIA 610.74), MinGW-w64 GCC 15.2.0 (WinLibs r7), Release, `BUILD_TESTING=ON`, at `main` `d592d54e1`.

- **New case:** `RunSampledKeysBehindKernel` in `agc_driver_dcc_keys_first_write`.
  - The key memory holds `00 00 80 3f` (1.0f), and the sampled texture's keys first read as stored, as before.
  - The test then records the existing `0000` clear kernel over both halves of the keys, without waiting, and reads the keys again.
  - They read `Clear0000`, and after the batch the key bytes are `0x00`.
  - Without the change the test fails with `a sampled texture whose keys a recorded kernel stores 0000 over read uncompressed before the kernel ran`.
- **Full suite:** `ctest --timeout 120 --parallel 12 -E '^agc_driver_bda_device$'`: 970 tests passed, 7 of them skipped (expected per `check_skips.py --job windows`). `agc_driver_bda_device` was not run: on this machine an older base lost the device in it, which also resets the display.
- **Repo checks:** `check_conventions.py`, `check_test_registration.py` and `git diff --check` pass.

**Palworld (PPSA23126):**
- **Base:** the same build with and without the change, with the open PRs the title needs (#2648) and local changes.
- **Character creation and world:** the textures reported as read as stored drop from 19 to 30 per run to 2. Those two are listed in the issue and are separate cases.
- **Main menu, idle, two runs each:**

| | Presents / 10 s | Frame time | Textures read as stored |
|---|---|---|---|
| With | 195.8 / 193.0 | 51.1 / 51.8 ms | 0 |
| Without | 185.5 / 185.5 | 53.9 / 53.9 ms | 0 |

The menu has no texture this change applies to, so the difference there is run-to-run variation, not a cost. The change does not restore the colours of Palworld's world, which have another cause.

Not tested: Linux.

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
