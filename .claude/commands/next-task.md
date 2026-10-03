---
description: Pick the next open task from ROADMAP.md and plan it
---
1. Read `ROADMAP.md`, `docs/FINDINGS.md` and the latest entries of `docs/KNOWN_ISSUES.md`.
2. Find the first unchecked task whose phase prerequisites and gate are satisfied.
   If the previous phase's exit criteria are not confirmed by the user, ask about them first.
3. Load the `cyberpunk-red4ext-modding` skill.
4. In Czech, tell the user: task ID + title, a short plan (files to create/change, APIs used and
   whether each API is verified or `VERIFY`), and the risks. Wait for "ok" before writing code
   unless the task is trivial.
5. Implement, build, then run `/test-plan` for the in-game test.
$ARGUMENTS
