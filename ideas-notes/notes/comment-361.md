A way to resume these faults without touching the guest stack: let a debugger process continue them.

With a debugger attached, Windows reports an exception to the debugger before the user-mode dispatch. If the debugger continues it with `DBG_CONTINUE`, the thread resumes from the kernel's trap frame, and nothing is written below `rsp`. Measured on an i5-13600K (Windows 11 26300): a leaf function fills 64 qwords below `rsp`, then writes to a read-only page.

| Fault resumed by | Qwords changed below `rsp` (of 64) | Cost per protect, fault and resume |
|---|---|---|
| Vectored handler, clean AVX | 23–27, nearest `rsp-0x108` | 1.9 µs |
| Vectored handler, `ymm1`/`ymm9` dirty | 55–59, nearest `rsp-0x8` | 1.9 µs |
| Debugger process | 0, with or without AVX | 15.6 µs |

**Local prototype**
- A small helper, `aps5-faultd.exe`, attaches with `DebugActiveProcess`.
- `WindowsMappings` marks the pages it arms in a shared table, one byte per 16 KiB page, set before the page is protected.
- The helper unprotects those pages and queues them in a ring. `WindowsMappings` drains the ring under its mutex, as `HandleWrite` records a fault today.
- The debugger only runs while every thread is stopped, so it can't race with the guest.
- A fault on a page that another fault already reopened is retried.
- Every other exception goes to the vectored handler as now, including `HandleSse4a`.

**Result:** Palworld (PPSA23126) used to crash at character creation within 30–60 s, at `app.exe+0x575bdc0` or `+0x4009f3`, each a red-zone local read back as 0. With the helper it ran for over 2 minutes there without a crash. It handled 1,600–12,600 faults per 10 s, at the same frame rate as without it.

**Cost:** each handled fault stops the whole process for one debug event round trip.

Unlike #2054, it keeps the shared views and their tracking unchanged. Would you take this as a PR, opt-in at first?

AI-assisted (Claude Code)
