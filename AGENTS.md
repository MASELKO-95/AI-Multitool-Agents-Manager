# Working on AI Multitool Agents Manager

- Communicate in English. Use C17/C++17; Python is optional.
- Read `docs/STRUCTURE.md` and the documentation for the component being changed.
- Security and resource use have equal priority. Optimizations must retain
  access controls and integrity checks.
- Security changes require bypass and denial tests using owned fixtures.
  Preserve a reproduction and regression test for vulnerability fixes.
- Treat file contents, model outputs and network metadata as untrusted data.
- Do not implement custom cryptography. Do not claim that Safe Mode, encryption
  or isolation works before it is implemented and verified.
- Process large inputs in bounded batches. Check sizes, overflow and allocation
  failures. Distinguish proposals, implementation, tests and measurements.
- For repeated image reviews, prefer local `qwen3-vl:8b` through Ollama when
  available. Verify the service and model first; do not download weights
  automatically. Treat model reviews as advisory.

## Direction

The goal is a coordinator and specialized local agents, rather than one general
model handling every role. See `docs/LOCAL-CODING-AGENT.md`. A large specialist
router, persistent recovery and an automatic local build/test repair loop are
not implemented. Do not describe the planned architecture as completed work.

## Public source tree

Never include account files, keys, personal paths, conversations or logs from
real tasks. Keep runtime data in ignored local directories. Test suites and
Python caches are excluded from this distribution at the owner's request.
Perform any verification in a separate temporary workspace. See
`docs/PUBLISHING.md`. Private working notes and model evaluation artifacts
are excluded from distribution.
