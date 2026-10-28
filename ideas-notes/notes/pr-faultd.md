### What

Fixes the crash in #361 when `APS5_FAULT_DEBUGGER=1` is set; it is opt-in for now. Windows dispatches a write fault on the faulting thread's own stack, directly below `rsp`, before the vectored handler runs. So resuming a write-tracking fault on a shared view overwrites the System V red zone of the guest leaf function that took it. A fault that a debugger continues with `DBG_CONTINUE` resumes from the kernel's trap frame instead, and nothing is written below `rsp`.

- **`aps5-faultd.exe`:** a small helper built next to libc. With `APS5_FAULT_DEBUGGER=1` the arena starts it and waits for it to attach (`DebugActiveProcess`).
- **Shared table:** `WindowsMappings` marks each shared page it arms, one byte per 16 KiB page.
  - The byte is set before the page is protected.
  - It is cleared whenever the page is opened (`HandleWrite`, `Pin`, `BeginHostWrite`), reprotected (`SetProtection`, `Commit`) or unmapped.
- **The helper** unprotects an armed page that faults, queues it in a ring and continues the thread. It only runs while every thread of the process is stopped for the debug event, so it never races with the guest.
- **Draining:** `WindowsMappings` drains the ring under its mutex before it reads page state, and records the write as `HandleWrite` does.
- **A fault on a page another fault already reopened** is retried, as two threads can fault on the same page before the first is handled.
- **Every other exception** goes to the vectored handler as now, including `HandleSse4a`. The vectored handler still takes write faults when the helper is absent or did not attach, and says so.

Unlike #2054, it keeps the shared views and their tracking as they are.

### Tested

Windows 11 Pro 26300, i5-13600K, RTX 2080 Ti, MinGW-w64 GCC 15.2.0 (WinLibs r7), Release, `BUILD_TESTING=ON`, at `main` `d592d54e1`.

- **New test: `guest_fault_debugger`** (Windows, `APS5_FAULT_DEBUGGER=1` set by the test).
  - A leaf function fills 64 qwords below `rsp`, dirties `ymm1` and `ymm9` (or not), and writes to a shared direct-memory page armed by `GuestArenaCollectWrites`.
  - With the helper attached, no qword changes, the write lands, and the next collect reports it.
  - Without the helper the same probe changes 55 to 59 qwords with dirty AVX and 23 to 27 without, measured in a standalone program on this machine, as in the #361 comment.
- **Existing tracking tests:** `guest_memory`, `agc_write_tracking`, `agc_shared_write` and `agc_host_import_unmap` pass with and without `APS5_FAULT_DEBUGGER=1`.
- **Full suite:** SUITE_RESULT
- **Repo checks:** `check_conventions.py`, `check_test_registration.py` and `git diff --check` pass.

### Performance

**Cost per fault** (protect, fault and resume, 20,000 cycles):

| Handled by | Cost |
|---|---|
| Vectored handler | 1.9 µs |
| Debugger | 15.6 µs |

Each handled fault stops the process for one debug event round trip.

**Palworld (PPSA23126):**
- **Base:** the same build with and without the switch, with the open PRs the title needs (#2648).
- **Main menu, idle, alternating runs, last four 10 s windows:**

| | Presents / 10 s | Frame time | Write faults / 10 s |
|---|---|---|---|
| Debugger | 198.8 / 181.5 | 50.3 / 55.1 ms | 3324 / 4410 resumed by the debugger, 0 by the handler |
| Vectored handler | 184.8 / 184.0 | 54.1 / 54.3 ms | 4465 / 4154 |

The difference is within run-to-run variation; this scene is GPU-bound.

- **Character creation:** without the switch, the title crashed there within 30 to 60 s, at `app.exe+0x575bdc0` or `+0x4009f3`, each a red-zone local read back as 0. With it, it ran past character creation into the world. There it handled 1,600 to 12,600 faults per 10 s, at the frame rate it had without the switch.

Not tested: Linux (the change is Windows-only).

### Checklist
- [x] Based on current `main`; no other open PR implements the same functions
- [x] One topic per PR; follow-ups go in a new PR
- [x] No comments in code except [technical debt](https://github.com/boykopovar/AnyPS5/blob/main/docs/dev/CONVENTIONS.md)
- [x] Unimplemented paths throw (`NotImplemented_nid_no_patch`); silent stubs are listed in [TechnicalDebt](https://github.com/boykopovar/AnyPS5/blob/main/docs/dev/TechnicalDebt.md#silent-stubs)
- [x] No notes, investigation `.md` files or images added to the repository (attach them to this PR)
- [x] New third-party code is a submodule built from source
- [x] Generic behaviour, not specific to one title
- [x] Depends on: none
- [x] AI-assisted: yes (Claude Code)

🤖 Generated with [Claude Code](https://claude.com/claude-code)
