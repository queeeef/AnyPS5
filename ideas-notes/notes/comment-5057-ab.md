Sorry for the slow reply, busy day. Here is the Palworld A/B for this head.

**Result:** no measurable change in this scene. The three heaviest shaders take the same GPU time per call with and without it, and presents per 10 s stay within run-to-run variation.

**Setup**
- Windows 11 Pro 26300, RTX 2080 Ti (NVIDIA 610.74), MinGW-w64 GCC 15.2.0, RelWithDebInfo.
- **Base:** `main` `5d933db02` plus the open PRs Palworld (PPSA23126) needs and local changes. #4072 is not in it, so packed f16 fma takes the existing lowering.
- **With:** the same base plus this PR's two commits (`b5f68bc9d`, `68ee4015b`), applied unchanged.
- **Tests:** with the change, the 43 f16 and packed ALU tests pass on this GPU, `agc_driver_packed_float16_native` with its all-lanes case included.
- **Scene:** title screen, alternating runs (base, with, base, ...), three each, 110 s per run. `APS5_PROFILE_GPU=1`, last four 10 s windows of each run. The window was in the background in all six runs. Per-call times are higher than in a foreground run (about 13 ms for the first shader), the same for both builds.

**GPU time per call** (`[gputime]` by program, ms per dispatch, mean of four windows):

| Run | Shader 1 (1876 code words, wave32) | Shader 2 (5992 words, wave32) | Shader 3 (1368 words, wave64) | Presents / 10 s |
|---|---|---|---|---|
| Base 1 | 21.1 | 8.57 | 3.94 | 192 |
| With 1 | 21.6 | 8.53 | 4.12 | 184 |
| Base 2 | 23.0 | 9.17 | 4.35 | 183 |
| With 2 | 22.3 | 9.00 | 4.21 | 189 |
| Base 3 | 17.4 | 8.66 | 3.84 | 212 |
| With 3 | 23.1 | 9.25 | 4.37 | 159 |

All three are 32x1x1 or 64x1x1 compute shaders dispatched once per frame. The packed ops are about 30% of shader 1's GCN instructions (45 `v_pk_fma_f16`, 51 `v_pk_add_f16`, 60 `v_pk_mul_f16`, 26 `v_pk_min_f16`, 19 `v_pk_max_f16`, 20 `v_fma_mix_f32` of about 950).

**Why it doesn't show here:** what the special-value rules cost is small next to the rest of the lowering.
- **SPIR-V size:** shader 1 recompiles to 207,832 SPIR-V words without this change and 242,434 with it, as the rules stay behind the guard.
- **What the shader is made of:** of about 46,000 instructions, about 8,000 are `OpBitwiseAnd`, 3,600 `OpSelect`, 3,600 `OpBitcast`, 2,800 `OpBitwiseOr` and 2,000 `OpBitFieldUExtract`. There are 480 f32 `OpFMul`.
- **One `v_pk_mul_f16`:** with the guard, it is the native `f16vec2` multiply plus about 10 instructions for the check and a branch. Before, it was the multiply plus about 20 for the rules. The `op_sel` repacking of each operand (two extracts, two masks, a shift and an or) is around it either way, and here is computed twice per op.
- **One plain f32 multiply:** its operands and result are wrapped by the denormal flush and NaN handling, about 25 instructions with a branch.

So in Palworld's heavy shaders the f32 denormal and NaN handling and the operand repacking weigh more than the f16 rules this PR guards. The guard itself works as described. I have no counter for how often lanes take the slow path in this scene.

AI-assisted (Claude Code)
