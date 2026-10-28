---
name: anyps5-feedback-check-prior-experiments
description: "Before proposing an AnyPS5 driver change, grep the Palworld testing log for it; on 2026-10-14 I re-proposed and rebuilt H3 (rect-list GPU indirect) which had already failed that day"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 074309bc-434c-4e06-8aa9-4380a224c1be
  modified: 2026-10-14T20:53:42.476Z
---

Before proposing or implementing any driver change, search [[palworld-anyps5-testing]] and [[anyps5-architecture-review-2026-10-14]] for the code path or symptom (e.g. "rect list", "indirect", the function name). On 2026-10-14 I proposed "move indirect rect-list draw arguments GPU-side" as a new idea, the user approved, and I implemented and tested it before noticing it was H3 from that same morning: it caused green artifacts (the CPU argument wait masks a coherence bug) and gave no FPS (the wait moved to other draws). I reverted without shipping a launcher.

**Why:** the user spends real play sessions on each A/B run; repeating a failed experiment wastes them and erodes trust in the analysis.
**How to apply:** at the start of any new step, grep the memory files for the subsystem; cite the earlier result when it exists, and say so before proposing.
