# Palworld (PPSA23126) on AnyPS5: issue list

**Title:** Palworld, PPSA23126, content version 01.102.999 (in-game build string v1.0.5.102999)
**Host:** Windows 11 Pro 26300, i5-13600K, 64 GB RAM, RTX 2080 Ti (NVIDIA 610.74, subgroup 32), 4 GB page file
**Toolchain:** WinLibs MinGW-w64 GCC 15.2.0 r7, Ninja, RelWithDebInfo
**Base:** `main` at `a11c8f7f7`. Relinked with `--windows --to-intel --registry`, libraries from the `libs` target, MinGW runtime DLLs beside them.
**Integration branch** (local, `palworld/integration`) = `main` plus, in order:
`blawesom:verify/3461` (#3461, kqueue/kevent/file events), local socket-descriptor workaround (item 2), #3739 (includes #3391), #3633, #1077, #2904, #425, #3546, local stencil-plane workaround (item 7), local rect-list trim (item 9).

**Result:** with the integration branch the title reaches the main menu (about 13 FPS), the world list, world settings and the character creation screen. It crashes in title code about 2 minutes after character creation opens, while the screen runs at 0.07-3 FPS (item 10). Without the open PRs and local changes, `main` stops before the first frame.

No game files are shared, only log lines, register values and NIDs. AI-assisted (Claude Code).

---

## Start-up stops, in the order they are hit

| # | Stop on `main` `a11c8f7f7` | Status | What gets past it |
|---|---|---|---|
| 1 | `FAIL: unresolved ELF import cG3t15OK4Ow from libkernel.prx` (`sceKernelAddFileEvent`). `kevent` (`RW-GEfpnsqg`) and `sceKernelDeleteFileEvent` (`UmXngHKB6is`) are also absent. Importer: `libdiscord_partner_sdk.prx`. | Tracked in #2648 / #3461 | #3461 branch (`blawesom:verify/3461`, `35b3628eb`) cherry-picks cleanly onto `a11c8f7f7`. All imports then bind. |
| 2 | `unhandled exception 0xc0000005 on thread 'RtcNetworkThread'`, `rip eossdk-ps5-shipping.prx.guest.prx+0xdea2be`, write past a stack `fd_set`: `FD_SET` on libkernel socket `0x10000005` (`rcx`/`r12` = `0x10000005`, `rsi` = `0x400000` = fd >> 6). | Reported in #1794 (comment 6098818206), no PR | Local workaround only (not proposed as is): `GuestSockets::FirstDescriptor` = `0x300`, and `select` polls libkernel sockets through libkernel `poll`. With it the EOS SDK runs and the crash is gone. Needs a real design decision (see the questions in #1794). |
| 3 | `ShaderRecompiler::Recompile: shader info collection failed: packed pixel ancillary input has an unsupported live use (used by SelectU32 ? ?)`, pixel shader `0x1b40e70000`, 3708 code words, wave64. Fatal (`terminate`). | #2548 | #3739 (with #3391): `agc_shader_replay` of the dumped request goes from the error above to `recompiled: 236192 SPIR-V words`. #3391 alone not tested separately. Reproduces offline with `APS5_DUMP_SHADERS=1`. |
| 4 | `AGC graphics: color views of several array slices are unsupported` | #2102 | #3633 |
| 5 | `AGC graphics: mipmapped or DCC 3D color targets are unsupported`. Register values (local diagnostic): slot 0, 48x48x48 volume, `maxMip 0`, **DCC on**, view slices 0-48, format 12 number 7 (11-11-10 float), `CB_COLOR_INFO 0x10040730 ATTRIB2 0x000bc02f ATTRIB3 0x4ec6c02f VIEW 0x00060000`, `PA_CL_VS_OUT_CNTL 0x01240000` (render target index export on). A layered draw into a DCC volume. | #2102 | #1077. #1077 and #3633 conflict in `DecodeColorBuffer` (both rewrite the slice checks); resolved by taking #1077's volume branch and #3633's 2D-array branch. |
| 6 | `compute shader 0x1441185800: AGC graphics: storage image access to depth/stencil surface 0x30264b0000 is not implemented`. With #2904 the message reads `(storage=159x90 guest format=7 mip=0 dimension=2 tile=4)`: the view has the surface's own extent, so #2904's reused-memory path does not apply. | #1409 | #425. #425 and #2904 conflict in `cachedStorageTexture`; resolved by keeping #425's seeding behind #2904's `DepthSurfaceAt(context, words, viewed)` match. |
| 7 | `compute shader 0x144106ea00: AGC graphics: storage access to a depth surface's stencil plane is not implemented` during presentation (after #425). | #2076 (PR, conflicting, 2370 commits behind `main`) | Local workaround only: fall back to an ordinary storage image over guest memory. Stencil data read by that shader is wrong. |
| 8 | Character creation screen: `Shader 0x1843cb4000 could not be prepared after registration: ... opcode=ReadLane reason=v_readlane_b32 of lane 63 is outside the 32-lane host subgroup that runs this wave64 program` (vertex, stage 1, hash `0x104216608768`; a second one `0x104217952256` at `0x1843dfc000`). Fatal at first use. The frame rate drops to about 0.5 FPS just before, while the screen's shaders compile. | #2783 | #3546 (merged cleanly on top of the rest). Confirmed: with it the stop is gone, the screen opens and the next stop is item 9. |
| 9 | Character creation, right after "Finish setting up world and start the game? Yes": `AGC graphics: incomplete rect-list primitive`. Local diagnostic: a non-indexed draw of **4 vertices x 9 instances** (10 instances on another run), rect-list topology, no index buffer, into a 2048x2048 color target (`0x3031e40000`). Thrown during presentation. | No upstream issue found | Local workaround: drop the vertices past the last complete rect (`indexCount -= indexCount % 3`), as the hardware ignores an incomplete trailing primitive. With it the character creation screen renders. Proposed upstream as a fix: `Draw.cpp:995` should trim, not throw. |
| 10 | About 2 minutes into character creation: `FATAL: unhandled exception 0xc0000005 on thread 'Background Worker #2'`, `rip app.exe+0x575bdc0`, `read of 0x48`. Title code: `mov rax,[rbp-0x48]; mov rax,[rax+0x48]` with `rax = 0`, inside a loop over an array of pointers (`r10 = [rax+rcx*8]`, null entries skipped). Call chain from a UE task worker (`app.exe+0x2d81e91`, `+0x2dd26fc`, `+0xa83d64`, `+0x2d86c8f`, `+0x2d7a4b7`, `+0x2d79114`, `+0x1fb7e`, `+0xcb4c3f`, `+0x57dcf8d`, ...). | New, cause unknown | Not resolved. Could be downstream of the local stencil workaround (item 7, wrong stencil data reaches a compute shader) or of a GPU readback; needs a run with #2076 rebased instead of the workaround. |

## Other findings

- **Save data:** `dirNameSearch` reports `hit=1 set=1`, then `mount3 user=268435456 dir=UserOption mode=0x1` returns `0x809f0008` (not found), three times per run. On disk `_sd/SaveDataV1/` holds only `*.WorldOption` directories, so the search result does not match what the mount looks for. The game proceeds without its options save. _To check: does `dirNameSearch` filter by the dir name pattern the title passes?_
- **Performance:** about 13 FPS on the main menu and 12 FPS in world settings (2080 Ti, 1472x828 swapchain). On character creation the frame rate falls to 0.07-3.4 FPS. Every shader before it comes from the disk cache (665 hits, 0 misses); from character creation on the cache reports 150 misses in about 2 minutes, so the screen compiles about 150 new shaders on the render path while it is displayed. Device-local memory use is 6.5 of 10 GB, so this is not memory exhaustion. Sound plays in fragments during the stall.
- **Input focus:** on each modal dialog (the "Could not login" dialog, "Finish setting up world and start the game?") nothing is focused when it opens; the first cross press (or mouse click) only focuses a button and a second press activates it. Mouse clicks on the first dialog did not register at first, then did.
- **UI rendering:** in World Settings the focus frames of rows already passed stay drawn, so every row looks selected.
- **Pad input file:** `APS5_PAD_INPUT_FILE` presses sent while the title is still loading are dropped (documented), so a scripted run cannot know when the menus start reading input; a "ready" line in the log would help automation.
- **Online:** "Not Logged in" and the login dialog are expected without PSN/Epic sign-in; not an issue.
- `sceSysmoduleLoadModule: unknown id: 267` (returns 0, logged only).
- `[gpu] layer/viewport index vertex exports are ignored (PA_CL_VS_OUT_CNTL=0x01240000)` is still printed with #1077, because bit 24 (`VS_OUT_MISC_SIDE_BUS_ENA`) is outside `layerExports`. The layered draw itself is routed.
- `[gpu] texture ... has single/mixed DCC keys ...; its texels are read as stored`: possible visual corruption on those textures.
- `[gpu] storage image ...: guest memory changed while GPU results were pending; keeping the CPU's blocks` (8 times on the way to character creation).
- Relinker v0.1.1 (latest release) cannot convert the title: `FAIL: AMD-only instruction without Intel lowering: MONITORX (offset 0x2d2fcb0)`. `main` lowers it. A new release would help testers on Intel hosts.
- World Select lists no worlds although `_sd/SaveDataV1/` holds two `*.WorldOption` directories from runs that crashed during setup. Probably correct (the world was never finished), noted in case it is not.
- Runtime layout tip for testers: the 36 GB `pal-ps5.pak` does not need copying; a directory junction for `app0/pal` and `app0/sce_sys` works.
