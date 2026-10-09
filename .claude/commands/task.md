---
description: Work on one task from docs/build-plan.md
argument-hint: <task id, e.g. T04>
---

Work on task $ARGUMENTS from `docs/build-plan.md`.

1. Read that task's section and every requirement ID it lists in `docs/requirements.md`.
2. Restate: goal, requirement IDs, acceptance criteria, files you expect to create or change.
3. List anything ambiguous or missing (data, sources, decisions) as numbered questions.
   If any question blocks the work, stop and ask.
4. Propose a step-by-step plan, tests first. Stop and wait for my approval before editing files.
5. After approval: write failing tests, implement, then run `scripts/check.sh`.
6. If physics, units, plugins or reference models changed, run the physics-reviewer subagent and fix its findings.
7. Report: what changed, the actual test output, requirement IDs covered, and open issues.
   Do not call the task done if anything fails.
