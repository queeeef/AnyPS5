Proposal: evaluate the packed f16 special-value rules only on lanes that can need them

### Problem

Every packed f16 instruction applies RDNA's special-value rules on both halves:
- which input NaN is returned, and its quieting in IEEE mode;
- the default NaN `0xfe00` for an invalid operation (`inf * 0`, `inf - inf`);
- `FP16_OVFL`;
- for min/max, the NaN operand selection.

These rules are exact and tested against hardware. They are also most of the instructions the recompiler emits for packed f16 math, although they only change a result when an input is NaN, or when the result is NaN or infinite.

IR values per instruction (`APS5_LOCAL_IR_HISTOGRAM`, a local counter), for the two most expensive shaders of Palworld (PPSA23126) on its title screen:

| Instruction | Count in the two shaders | IR values per instruction |
|---|---|---|
| `v_pk_fma_f16` | 482 | 206–227 |
| `v_pk_fmac_f16` | 72 | 227 |
| `v_pk_min_f16`, `v_pk_max_f16` | 682 | 129–134 |
| `v_pk_add_f16`, `v_pk_mul_f16` | 566 | 103–107 |
| `v_min3_f16`, `v_max3_f16` | 1220 | 97–100 |

With native f16 math (#3531, and a follow-up using `OpFmaKHR` from `VK_KHR_shader_fma` for the fma) the arithmetic itself is one instruction. Almost everything left is the rules. #3531 already measured this on an RTX 4060: "Most of what's left in each instruction is the NaN rule on the result bits."

### Measurement

**Setup:**
- RTX 2080 Ti, NVIDIA 610.74, Windows 11.
- Palworld title screen, idle, `APS5_PROFILE_GPU`, shader cache off.
- Three 10 s windows per build, same scene.

**Builds:**
- **Exact (today):** current packed f16 translation, with #3531 and #3590.
- **+ native fma:** `v_pk_fma_f16` and `v_pk_fmac_f16` as native f16x2 math.
- **No special-value rules:** like the previous build, and the whole block that applies the special-value rules in `packedFloat16` is skipped (with the clamp inside it). This is **not correct**: it is only the upper bound of what the rules cost.

| Build | Presents / 10 s | GPU time / 10 s | Shader `0x1a01…` per frame | Shader `0x1bc1…` per frame |
|---|---|---|---|---|
| Exact (today) | 181–184 | 6365 ms | 16.5 ms | 8.4 ms |
| + native fma | 188–191 | 6155 ms | 15.8 ms | 6.9 ms |
| No special-value rules (upper bound) | 223–226 | 5413 ms | 10.4 ms | 3.8 ms |

These two compute shaders take 25 of the 35 ms of GPU time per frame. Without the rules they take 14 ms, and the frame rate goes up by 22% with every other cost unchanged. The scene is GPU-bound (93% busy at 1860 MHz), so the saving shows up directly as frame rate.

### Proposal

Keep the rules exactly as they are, but run them only for the lanes that can need them.

For each packed f16 instruction:
1. **Fast path.** Compute the result as today: native f16x2 where available, the f32 path otherwise. Clamp stays here.
2. **Guard.** Compute one boolean for both halves: "an input half is NaN, or the result half is NaN, or `FP16_OVFL` is set and the result half is infinite". With SWAR this is a few instructions on the packed words, for example for the NaN part:
   ```
   anyNan = (((a & 0x7fff7fff) + 0x03ff03ff) | ((b & 0x7fff7fff) + 0x03ff03ff) | ((c & 0x7fff7fff) + 0x03ff03ff) | ((r & 0x7fff7fff) + 0x03ff03ff)) & 0x80008000
   ```
   A 15-bit magnitude above `0x7c00` sets bit 15 of its half, and no carry crosses into the other half.
3. **Slow path.** If the guard is set, run today's rule code unchanged, in a branch: `if (guard) { result = rules(result) }`. A thread whose inputs and result are ordinary numbers never runs it.

**Why the result is bit-identical:** the rules return the fast-path result unchanged whenever the guard is clear. The rule code is today's code, so the existing hardware-oracle tests cover it, and #3531's differential test already generates NaN, infinity, denormal and overflow inputs on every lane.

**Expected cost per packed instruction:** the arithmetic plus about 10 guard instructions, instead of 100–227 IR values today. Most of the upper-bound gain above should remain; the prototype will measure how much.

### Pros

- **Exact.** It adds no new assumption about hardware or drivers, unlike approximations or dropping rules behind a setting.
- **Large and general saving.** Palworld is an Unreal Engine title, and any title whose shaders are compiled with 16-bit types runs these instructions. Its title screen alone would gain up to 22% frame rate. In other scenes and titles, the saving scales with how much of the frame is f16 math.
- **The same pattern applies beyond packed math:**
  - scalar f16 min3/max3/min/max, about 60–100 IR values each and 1924 instructions in the same two shaders;
  - scalar `v_fma_f16`, `v_rcp_f16`;
  - the f32 NaN rules in `nanResultF32`.
- **Complements the native-math PRs** (#3531, `OpFmaKHR`). Those remove the arithmetic cost; this removes most of what they leave.
- **Small to review.** The rule code moves into a branch and isn't rewritten.

### Cons and mitigations

- **Divergence.**
  - **Risk:** if one thread of a wave has a NaN, that wave runs both paths and costs today's price plus the guard.
  - **Mitigation:** NaN and infinity are rare in rendering math, and usually a bug in the title when they occur. Even in the worst case, a wave with NaNs on every instruction, the cost only rises by the guard, about 10 instructions per packed instruction. The prototype will count how often the guard fires in Palworld, so this is measured, not assumed.
- **Control flow inside a translated block.**
  - **Risk:** the translator creates one IR block per RDNA block.
  - **Mitigation:** the slow path contains no subgroup operations, derivatives, memory accesses or image sampling, so its position doesn't matter for wave or quad semantics, and execution reconverges at the merge before the next instruction. The SPIR-V backend already emits branches with an `OpPhi` inside one IR instruction (`SpirvBdaRead.cpp`, `SpirvImageEmitter.cpp`), so the branch can live entirely in the backend.
- **Code size and compile time.**
  - **Risk:** the module gains the guard and a branch per instruction, so SPIR-V size and driver compile time change.
  - **Mitigation:** the slow path is the code emitted today. The prototype will report SPIR-V words and pipeline compile time.
- **Register pressure.**
  - **Risk:** the driver allocates registers for the largest path, so occupancy may not improve.
  - **Mitigation:** the saving comes from instructions not executed, not from occupancy. The upper-bound measurement shows that saving.
- **One title, one GPU.**
  - **Risk:** the numbers come from one scene on one NVIDIA GPU.
  - **Mitigation:** the prototype PR will repeat #3531's per-instruction chain timing on at least NVIDIA and lavapipe, and the Palworld scene measurement.

### Implementation options

1. **A backend lowering.** One IR opcode per rule family, carrying the operand bits, the fast result and the mode flags as immediates. The SPIR-V backend emits the guard, the branch and the existing rule code. This keeps the IR straight-line, and the branch stays where the backend already makes branches. **I'd start here.**
2. **A generic "rare select".** The IR marks `Select(cond, fast, slow)` as rarely taken, and the backend sinks the values used only by `slow` into a branch. This is more general, so the f32 rules would come for free, but it's a larger change to the backend.
3. **Splitting the translated block.** A conditional block inside the translation. This touches the translator's block model, and I would avoid it.

### Plan

1. A prototype PR for packed add, mul and fma with option 1, including:
   - a differential test against the current rules on the NaN, infinity and overflow rows #3531 generates;
   - a test that forces every lane through the slow path;
   - a counter of how often the guard fires, behind a profiling variable;
   - #3531-style chain timings and the Palworld measurement.
2. If accepted, packed min/max, then the scalar f16 instructions.

### Questions

- Is option 1 acceptable, or would you rather have the generic option 2?
- Is per-thread control flow inside an instruction acceptable for the recompiler's exactness rules, given that the rule code itself does not change?

AI-assisted (Claude Code)
