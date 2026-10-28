---
name: anyps5-feedback-verify-with-input
description: "Don't claim a game build works from an idle run; crashes show up only when menus are navigated"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 074309bc-434c-4e06-8aa9-4380a224c1be
  modified: 2026-10-11T17:17:07.130Z
---

An idle run that sits on the first screen proves nothing; the user corrected me sharply when I called a 7-minute idle run "crash-free". Also don't claim a scripted input run worked unless screenshots show it progressed.

**Why:** Palworld's crashes happen only on later screens (character creation), reached through menu input.
**How to apply:** report what was actually exercised ("sat on the login dialog, no input") and verify progress with screenshots. Related: [[palworld-anyps5-testing]].
