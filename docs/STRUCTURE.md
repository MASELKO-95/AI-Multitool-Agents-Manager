# Source structure

AI Multitool Agents Manager is an experimental C17/C++17 program. The current
runtime includes the session CLI, native desktop, bounded local text generation
and coordinator tools. Empty `.gitkeep` directories reserve future components.

| Path | Responsibility |
| --- | --- |
| `src/main.c`, `src/ui/cli.c` | Minimal session CLI |
| `src/core/app.c` | Bounded session and permission defaults |
| `src/core/plugin.c` | Plugin manifests and setting validation |
| `src/core/storage/` | Module settings and project list |
| `src/core/models/catalog.c` | Model catalog and task/budget suggestions |
| `src/core/learning/coordinator_lessons.c` | Built-in method examples, not user history |
| `src/core/tasks/expert_protocol.c` | Expert message validation |
| `src/providers/ollama/` | Profiles, confirmed downloads and text generation |
| `src/providers/codex/` | Optional app-server adapter and isolation helpers |
| `src/providers/discovery.c` | Local engine and model discovery |
| `src/tools/files/project.c` | Bounded project file access |
| `src/ui/desktop.c` | SDL2/Nuklear desktop |
| `src/ui/coordinator_cli.c` | Coordinator CLI and JSONL journal |
| `include/aimt/` | Public headers with C/C++ boundaries |
| `Modules/` | Declarative module forms |
| `configs/` | Public catalogs, schemas and release allowlist |
| `tools/` | Optional local engine and image review helpers |
| `thirdparty/nuklear/` | Pinned dependency and its license |

Build/test execution and automatic repair in the local coordinator remain to
be implemented. Media adapters, encrypted storage and full Safe Mode are also
planned. See [roadmap](ROADMAP.md), [local coding agent](LOCAL-CODING-AGENT.md)
and [publishing](PUBLISHING.md).
