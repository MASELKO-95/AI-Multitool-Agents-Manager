# Roadmap

The goal is an experimental manager for specialized agents. A coordinator
selects context and delegates small tasks to workers with focused roles.
Program-enforced access controls remain authoritative over model instructions.

## Current prototype

The native desktop and session CLI are implemented. The application discovers
local models, suggests candidates, creates separate Ollama profiles and offers
user-confirmed downloads. Text generation, bounded project file operations,
local coordination and an optional isolated Codex adapter are available.
Expert request/proposal/needs_guidance v1 messages have a schema and C validator.

## Next stages

1. Qualify workers on separate C17, C++17 and Python fixtures. Preserve failed
   proposals and actual test results; do not fix a candidate merely to pass its
   evaluation and then attribute that repair to the model.
2. Implement an isolated, bounded build/test runner with fixed recipes. Test
   refusal of out-of-scope files, network use and attempts to bypass checks.
3. Add a repair loop based on real command status and bounded diagnostics.
4. Persist task state with source revisions and safe recovery after interruption.
5. Add explicit routing between planner, coordinator, coding, review and other
   specialist roles, with retry budgets and escalation to the coordinator.
6. Expand memory and context indexing, then implement media adapters separately.

A model declaring completion is not verification. A saved file is not a build,
and a successful build is not proof of correct application behavior. Training
or fine-tuning is a separate experiment requiring reviewed examples and held-out
evaluation. No custom model weights are included in this project.
