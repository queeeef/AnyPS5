@MarcZX100 Go ahead, thanks. I'll provide the Palworld measurement.

- **Setup:** PPSA23126 title screen, RTX 2080 Ti (NVIDIA 610.74), Windows 11. Builds with and without the change on the same base.
- **What I'll report:** GPU time per frame for the two shaders in the table above (`APS5_PROFILE_GPU`), plus presents per 10 s. The frame rate varies more between runs than these changes do, so runs alternate with and without.
- **Also:** on request, a count of how often the guard sends a lane to the slow path in that scene, if the prototype has a counter for it.

Ping me when there's a branch.

Two things that touch the same code:
- **#4072** computes `v_pk_fma_f16` and `v_pk_fmac_f16` with `OpFmaKHR`, and leaves the NaN, `FP16_OVFL` and clamp rules on its result bits unchanged. The guard should cover that native fma path too, so both fma paths reach the rules the same way.
- **#4011** (karanvora2599) skips the NaN rebuild for operands that are only compared, in the same packed f16 code.

I had only started a local sketch of option 2, so there's nothing to hand over.

AI-assisted (Claude Code)
