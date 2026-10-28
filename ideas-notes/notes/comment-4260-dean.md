@oneandonlydean ran `up4/host-import-bind-join` (`778713032`) with #4367 on the Palworld scene, `APS5_TRACE_HOST_IMPORT=1`, `APS5_PROFILE_DRAW=1`.

- **Base:** the same local Palworld branch as in the issue. My local span and alias commits are reverted and replaced by yours and #4367's.
- **Run:** one run to character creation, about 30 s there. It then crashed at the known `app.exe+0x575bdc0`, the #361 red-zone read.

**What it fixes**
- **Joins:** 21 joins (`joins 2 registered ranges` and `joins 3 registered ranges` for binds up to `0x1fe0000`).
- **The 238 MiB region:** no copies in the `0x3020000000` range any more, where the per-frame 238 MiB region was.

**What is still copied:** 116 binds are refused with `a piece lies in a partly registered window`. They are 147 MiB in total, counted once each. The largest:

| Bind | Size |
|---|---|
| `0x301f130000+0x1560000` | 21.4 MiB |
| `0x3127c20000+0x8c0000` | 8.8 MiB |
| `0x3139d40000+0x870000` | 8.4 MiB |
| `0x31a1e20000+0x680000` | 6.5 MiB |
| `0x319fe20000+0x530000` | 5.2 MiB |
| 8 binds of `0x400000` | 4 MiB each |

They are used often enough to dominate the copies:

| 10 s window at character creation | Presents | Copied outside imports |
|---|---|---|
| 1 | 8 | 2.6 GB |
| 2 | 10 | 20.7 GB |
| 3 | 9 | 4.0 GB |
| 4 | 9 | 3.9 GB |

By region over the run: 18.0 GB in `0x3140000000`, 17.5 GB in `0x31a0000000`, 3.6 GB in `0x3190000000`, 2.1 GB in `0x3170000000`.

**For comparison, on the same scene:**
- **Without either change:** 15–56 GB per 10 s, 6–7 presents per 10 s.
- **With my local version:** 0 bytes copied, 9–13 presents per 10 s. It joins any bind whose pieces are all registered, adjacent and of the same access, partly registered windows included. #3158's growth is bounded instead by a budget on the bytes spans import per 10 s (1 GiB).

So in this scene, the partly registered windows carry most of the remaining traffic. If it helps, I can run a variant of your branch that also joins those pieces when every byte of the bind is registered, and report the same numbers.

**Two more numbers from the trace**, without a baseline to compare against:
- 24,053 `host import ... ok` lines;
- 1,117 `retired: the range was mapped again`.

AI-assisted (Claude Code)
