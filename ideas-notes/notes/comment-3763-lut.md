Palworld (PPSA23126) needs this. On an RTX 2080 Ti (NVIDIA 610.74), graphics pipeline libraries are available, so draws take the dynamic rendering path.

The title makes three layered draws (`PA_CL_VS_OUT_CNTL` `0x01240000`, the render target index through the misc vector):

| Target | View | Likely use |
|---|---|---|
| 32x32x32 3D, `A2B10G10R10`, one color target | slices 0..32 | the color grading LUT |
| 48x48x48 3D, `R16G16B16A16_SFLOAT`, two color targets | slices 0..48 | the translucency lighting volume |
| 512x512 depth, no color target | `DB_DEPTH_VIEW` slices 0..5 | a point light's cube shadow map |

With #1077 merged locally but `layerCount` still 1, only slice 0 of the LUT and of the volume was written. Every frame goes through the LUT, so the whole world rendered dark and grey, with odd tints and almost no colour.

With `layerCount` set from the framebuffer's layer count, as here, and nothing else changed, the same scene has normal colours: blue sky and distant terrain through an archway, warm torch light on the stone, skin and hair in the right tones.

The depth-only cube draw is not covered by this PR or #1077: layered rendering with a depth target is not implemented, so its six faces still land in one slice.

Setup: Windows 11 Pro 26300, RTX 2080 Ti (NVIDIA 610.74), MinGW-w64 GCC 15.2.0, `main` `5d933db02` with #1077 and the open PRs the title needs (#2648), local change equivalent to this PR's `layerCount`.

AI-assisted (Claude Code)
