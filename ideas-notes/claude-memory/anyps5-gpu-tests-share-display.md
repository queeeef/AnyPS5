---
name: anyps5-gpu-tests-share-display
description: "AnyPS5 GPU tests run on the user's only display GPU; a hang resets the driver and blacks out their screen"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 074309bc-434c-4e06-8aa9-4380a224c1be
  modified: 2026-10-11T21:43:53.640Z
---

The RTX 2080 Ti that runs AnyPS5's Vulkan tests also drives the user's display. A test that hangs the GPU (VK_ERROR_DEVICE_LOST, e.g. agc_driver_bda_device on 2026-10-11) triggers a Windows TDR, and the user's screen goes black each time.

**Why:** the user noticed repeated blackouts while I re-ran a failing test in a loop.
**How to apply:** after the first DEVICE_LOST, don't re-run that test. Ask before running more GPU-hanging repros, or exclude them with `ctest -E`. See [[palworld-anyps5-testing]].
