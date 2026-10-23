# Local coding agents

This document describes the target architecture. It does not claim that the
entire execution loop or a team of separately trained models already exists.

The manager should receive a goal, inspect a project, plan changes, delegate
small implementations, run required checks and repair failures using actual
results. C17, C++17 and Python are the primary evaluation languages.

## Specialized roles

| Role | Responsibility |
| --- | --- |
| Planner | Dependencies, stages and acceptance criteria |
| Coordinator | Context selection, delegation, progress and escalation |
| Coding worker | Small implementations using supplied types and APIs |
| Reviewer | Diagnosis using the specification, code and real diagnostics |
| Program | Access control, validation, file changes and tool execution |

The intended direction is a collection of focused specialists rather than a
single general model for every role. Models may run sequentially to fit the
available hardware. Different instructions applied to one base model do not
establish that separate learned specializations have been achieved.

A delegation contains a precise goal, source revisions, types/APIs, constraints
and acceptance criteria. Missing context must be reported rather than guessed.
Proposals and reviews are untrusted data and never grant permissions.

The target cycle is context -> plan -> small change -> build/test -> diagnosis
-> repair -> acceptance check -> persisted state. Repeated failures can escalate
to the coordinator with the attempted changes, actual logs and specific missing
information. Escalation must not silently reset the total task budget.

## Implemented boundary

The [local coordinator](COORDINATOR.md) supports bounded file operations and
text delegation. Reading after a write confirms contents. It has no command
runner, automatic compilation/test loop, persistent task recovery or automatic
reviewer escalation. Tools exposed through the [Codex adapter](CODEX.md) are
not automatically available to local models.

First evaluate one known bug on an owned fixture, including a regression test
and existing behavior checks. Only then broaden the task. Record actual
failures as well as successes. Model specialization, vocabulary reduction and
fine-tuning require separate measurements and held-out evaluation.
