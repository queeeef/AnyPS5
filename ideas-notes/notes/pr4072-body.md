### What

`v_pk_fma_f16` and `v_pk_fmac_f16` use one native f16vec2 fused multiply-add when the device has `VK_KHR_shader_fma` with `shaderFmaFloat16`. Before, each half was converted to f32 and summed with round-to-odd (#2000).

This extends #3531, which does the same for `v_pk_add_f16` and `v_pk_mul_f16`. #3531 kept the fma on the f32 path because GLSL `Fma` is not required to be fused, and it named `OpFmaKHR` (`VK_KHR_shader_fma`, `shaderFmaFloat16`) as the way to get a guaranteed fused f16 fma. NVIDIA 610.74 on Windows exposes it, with `shaderFmaFloat16`, `shaderFmaFloat32` and `shaderFmaFloat64` all true on an RTX 2080 Ti.

- **`VulkanDevice`:** queries `VkPhysicalDeviceShaderFmaFeaturesKHR` only when the f16 path from #3531 is already enabled (Float16, RTE, preserved denormals, preserved signed zero, inf and NaN). If `shaderFmaFloat16` is supported, it enables the extension and the feature and adds the `FMAKHR` capability and `SPV_KHR_fma`. Graphics shader validation accepts both.
- **Translation:** the packed fma joins #3531's native path as a new IR op, `FPFma16x2`, and the existing NaN, `FP16_OVFL` and clamp rules run on the result bits unchanged.
- **SPIR-V:** `FPFma16x2` is emitted as `OpFmaKHR` on f16vec2, decorated `NoContraction`.
- **Fallback:** without the capability, the f32 round-to-odd path is used as before.
- **Missing header values:** `3rdparty/SPIRV-Headers` predates `SPV_KHR_fma`. `SpirvFma.hpp` defines `OpFmaKHR` (4427) and the `FMAKHR` capability (6030) from the SPIR-V registry, with a technical-debt comment and a TechnicalDebt entry until the submodule is updated.

**Why the results don't change:**
- Vulkan's precision table lists `OpFmaKHR` as "Correctly rounded".
- `OpFmaKHR` is listed among the instructions that must respect `DenormPreserve`. The definition of correctly rounded applies `RoundingModeRTE`, which this module declares for 16-bit floats.
- So for every non-NaN input, the result is the exact `a * b + c` rounded once to f16, round to nearest even, denormals kept. That is the value the round-to-odd f32 sum produces.
- Any NaN the native operation produces is replaced by the NaN the existing rules select from the source bits, as in #3531.

#3531 is merged; this PR is one commit on `main`.

### Tested

Windows 11 Pro 26300, RTX 2080 Ti (NVIDIA 610.74), MinGW-w64 GCC 15.2.0 (WinLibs r7). Configured like the Windows CI job (`-G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`), at `b1393f07` (rebased onto `main` `34a16b7f0`).

- **`agc_driver_packed_float16_native` (#3531's test, extended).** Each FLOAT_MODE compiles the test kernel three ways:
  - for the device, with `OpFmaKHR` when it has `FMAKHR`;
  - for the device without `FMAKHR`, which is #3531's path;
  - without `Float16`, the f32 path.

  Both native builds must give the f32 path's bits for all 8 instructions. Four of them are fma: `v_pk_fma_f16` with op_sel/neg modifiers and with clamp, and `v_pk_fmac_f16`. The inputs are random halves weighted toward zero, denormals, infinities, NaNs and values near overflow. The test also checks that `OpFmaKHR` is emitted exactly when the capability is present. Here: `packed f16 native tests passed (589824 halves, fused f16 fma)`.
- **Hardware oracle:** `agc_driver_float16_fma_rounding`, which checks against values measured on hardware, passes on the `OpFmaKHR` path.
- **The tests catch a double-rounded fma.** With the emitter changed to `OpFmaKHR(round(a * b), 1.0, c)`, three tests fail:
  - `agc_driver_float16_fma_rounding`: `v_pk_fma_f16 ... is 0xf430, expected 0xf431`;
  - `agc_driver_packed_alu`;
  - `agc_driver_packed_float16_native`.
- **Full suite:** `ctest --timeout 120 --parallel 12 -E '^agc_driver_bda_device$'`: 918 tests, 913 passed, 5 skipped (expected per `check_skips.py --job windows`), 0 failed.
  - **Excluded:** `agc_driver_bda_device` was not run. On this machine an earlier run on an older base lost the device, which also resets the display.
- **Repo checks:** `check_test_registration.py`, `check_conventions.py` and `git diff --check` pass.
- **Not tested:** Linux, and devices without `VK_KHR_shader_fma` other than by removing the capability from the target, which the test does.

### Performance

Palworld (PPSA23126) title screen, idle, RTX 2080 Ti, `APS5_PROFILE_GPU`, shader cache off. Four runs alternated with and without the `FMAKHR` capability (a local switch, not in this PR), three 10 s windows each:

| GPU time per frame | `OpFmaKHR` (run 1 / run 2) | f32 path (run 1 / run 2) |
|---|---|---|
| Compute shader `0x1bc1…` | 7.3 / 7.4 ms | 9.2 / 9.4 ms |
| Compute shader `0x1a01…` | 18.1 / 17.2 ms | 17.9 / 18.7 ms |
| Presents / 10 s | 169–174 | 161–177 |

- **`0x1bc1…`** takes about 2 ms (21%) less per frame in both pairs.
- **`0x1a01…`** shows no consistent change. In both shaders, most of what is left per packed instruction is the NaN rule on the result bits, as in #3531 (see #4042).
- **The frame rate** differs between runs by more than this change. The title screen's content varies, so another shader took 0 to 4.6 ms per frame depending on the run, and no frame-rate gain is claimed.

### Checklist
- [x] Based on current `main`; no other open PR implements the same functions
- [x] One topic per PR; follow-ups go in a new PR
- [x] No comments in code except [technical debt](https://github.com/boykopovar/AnyPS5/blob/main/docs/dev/CONVENTIONS.md)
- [x] Unimplemented paths throw (`NotImplemented_nid_no_patch`); silent stubs are listed in [TechnicalDebt](https://github.com/boykopovar/AnyPS5/blob/main/docs/dev/TechnicalDebt.md#silent-stubs)
- [x] No notes, investigation `.md` files or images added to the repository (attach them to this PR)
- [x] New third-party code is a submodule built from source
- [x] Generic behaviour, not specific to one title
- [x] Depends on: none (#3531 is merged)
- [x] AI-assisted: yes (Claude Code)

🤖 Generated with [Claude Code](https://claude.com/claude-code)

