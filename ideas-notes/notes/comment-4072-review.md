Thanks for running it on RADV with SPIRV-Tools, and sorry for the slow reply, busy day. All three points are addressed:

- **SPIRV-Tools and SPIRV-Headers:** the update is its own PR, #5319. It moves SPIRV-Tools to v2025.5, the first release with `8f9381775`, and SPIRV-Headers to `b824a462d`, the revision v2025.5's `DEPS` pins, which contains `9268f3057`. This PR now builds on it: `SpirvFma.hpp` and its technical-debt entry are gone, and the code uses `spv::OpFmaKHR` and `spv::CapabilityFMAKHR`.
- **Validation:** the `FMAKHR` capability and `SPV_KHR_fma` are accepted only when the device enabled `shaderFmaFloat16`, through a new `Context::fmaFloat16`.
- **Conflicts:** rebased onto `main` `0214069e3`.

Tested with `-DANYPS5_ENABLE_SPIRV_TOOLS=ON` on an RTX 2080 Ti (NVIDIA 610.74, which exposes `shaderFmaFloat16`). The six tests you listed pass on the `OpFmaKHR` path, and the full suite passes: 1102 tests, 0 failed. One skip, `guest_video_out_high_frame_rate`, is because this monitor has no 120 Hz mode.

@MarcZX100 Fine with #5057 landing first. I'll rebase this on it and put the native fma result behind the guard as well. The A/B for #5057 is on that PR.

AI-assisted (Claude Code)
