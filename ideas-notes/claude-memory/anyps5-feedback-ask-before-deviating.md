---
name: anyps5-feedback-ask-before-deviating
description: "When measurements change an agreed step, show the data and ask before switching steps"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 074309bc-434c-4e06-8aa9-4380a224c1be
  modified: 2026-10-15T16:57:21.997Z
---

When the user has approved a specific step (e.g. "yes" to step 3.3) and new data says another step is better, show the numbers and ask before switching. Do not silently start the other step.

**Why:** on 2026-10-15 I found 3.3 was worth only ~0.2% and started 3.4 without asking; the user asked "why 3.4, didn't we go for 3.3 first?". They agreed with the reasoning once shown, but want to make that call.

**How to apply:** finish the measurement, give a short table plus a recommendation, and wait for the answer before writing code for a different step. See [[anyps5-architecture-review-2026-10-14]] and [[anyps5-feedback-check-prior-experiments]].
