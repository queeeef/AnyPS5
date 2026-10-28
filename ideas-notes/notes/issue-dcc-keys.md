AGC: a sampled texture's DCC keys are read before recorded GPU writes over them land (Palworld world: texels read as stored)

**Palworld (PPSA23126), at character creation and in the world.** 19 to 30 textures per run are reported as
```
[gpu] texture 0x… (format 71) has mixed DCC keys at 0x…; its texels are read as stored
```
mostly 2536x1428 and 1268x714 render targets in pairs (history buffers), plus formats 36 and 50.

**The keys are not keys.** A local diagnostic dumped the key bytes when that line is printed. For most of these textures they are float data, not DCC codes:

| Texture | Key bytes (as runs) |
|---|---|
| 2536x1428, `0x3030b60000` | `00 00 80 3f` repeated, i.e. 1.0f |
| 960x1080, `0x30265c0000` | `00 3c ff 7b` repeated, i.e. f16 1.0 and 65504 |
| 1268x714, `0x302f890000` | `ed 37 f7 3b 82 38 …` |

**Address, or timing?**
- **Address: correct.** Of the 15 textures in one run, 10 had also been bound as color targets. For 9 of them the T#'s `META_DATA_ADDRESS` equals the target's `CB_COLOR_DCC_BASE`. The tenth (`0x3032180000`) had last been bound with another base (`0x3033e40000`), a target that reused the memory.
- **Timing: the cause.** For 14 of the 15, the recorder held GPU writes over the key range when the keys were read (`Recorder::SnapshotWriteOverlaps`). The keys are written by GPU work recorded earlier in the same batch, presumably the title's key clears. `textureClearKeys` reads the keys through a raw pointer, past the flush hook, so it reads the memory's previous contents: the data of the transient heap that had the memory before.
- **Storage images are not affected.** `ProvedKeys` calls `WaitForKeyWriters` before it reads the keys. The sampled path does not.

**Proposed fix:** when the keys read do not decode (not uncompressed, not a clear the format can fill), call `WaitForKeyWriters` and read them again. It answers from known fills and stores first, and otherwise waits for the batch that writes the keys. Keys that decode are used as before. With it, the textures read as stored drop from 19 to 30 per run to 2:
- a 3840x2160 surface with 2 mips, whose key range as counted runs past the cleared keys;
- a 9x1 surface with the single-channel clear code `0x10`.

The fix does not restore the world's colours in Palworld: that has another cause, still open.

**Setup**
- Windows 11 Pro 26300, RTX 2080 Ti (NVIDIA 610.74), MinGW-w64 GCC 15.2.0 (WinLibs r7)
- `main` plus the open PRs Palworld needs (#2648) and local changes, the same build with and without the fix

AI-assisted (Claude Code)
