Rebased onto `main` `34a16b7f0`. #3531 is merged, so this is one commit and **Depends on** is none. The full Windows suite passes: 918 tests, 913 passed, 5 expected skips.

The six PRs in the overlap table don't need to be dependencies. I trial-merged each with this branch. Each conflicts only on lines next to this PR's, and none of them is something this PR builds on:

| PR | Conflict | What both sides add |
|---|---|---|
| #3593 | `docs/dev/TechnicalDebt.md` | neighbouring entries |
| #3763, #4061 | `ShaderValidation.cpp` | another allowed extension on the same `Require` line |
| #3988, #4011, #4085 | `VulkanDevice.cpp` | device feature blocks at the same place |

Whichever merges second needs a rebase of those lines. If one of them merges first, I'll rebase this one.

AI-assisted (Claude Code)
