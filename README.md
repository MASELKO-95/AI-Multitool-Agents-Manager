# AI Multitool Agents Manager

[![MIT license](https://img.shields.io/github/license/MASELKO-95/AI-Multitool-Agents-Manager?style=flat-square)](LICENSE)
[![Release downloads](https://img.shields.io/github/downloads/MASELKO-95/AI-Multitool-Agents-Manager/total?style=flat-square&label=release%20downloads&color=2da44e)](https://github.com/MASELKO-95/AI-Multitool-Agents-Manager/releases)
[![README views](https://hits.sh/github.com/MASELKO-95/AI-Multitool-Agents-Manager.svg?style=flat-square&label=README%20views&color=3b82f6)](https://hits.sh/github.com/MASELKO-95/AI-Multitool-Agents-Manager/)

**An experimental program for managing specialized AI agents and local models.**

The goal is to coordinate agents with focused roles instead of relying on one
large general model for every task. A planner and coordinator select context,
delegate small tasks, review results and keep access under user control.

The current prototype uses C17/C++17, with optional Python tools. It provides a
native desktop interface, project selection, model discovery, a local Ollama
coordinator and an optional Codex CLI integration. Routing a large team of
specialists, persistent task recovery and automatic local build/test repair
loops are still planned. This is experimental software under active development.

## What works

- Native SDL2/Nuklear interface, bounded in-memory chat and module settings.
- Project folders with separate read-only/edit permissions and a user terminal.
- Local model discovery, task-based model suggestions and separate Ollama profiles.
- Text generation and a local coordinator with bounded file tools and delegation.
- Optional Codex execution or coordination through a local app-server.
- Versioned expert message contracts and a native message validator.

Image, video, speech, avatar and mesh modules currently describe settings and
planned adapters. Their forms do not imply working media generation or device
access. Full Safe Mode and encrypted storage are not implemented.

## Run

```sh
python3 run.py
```

The launcher builds the application with CMake using at most two parallel jobs.
It checks the installed Ollama service and starts a local server if needed.
It does not download models. Closing the launcher stops only a server that it
started. `Run.py` is also supported.

Requirements: Linux, CMake >= 3.20, C17/C++17 compilers, Python 3, pkg-config,
SDL2 >= 2.0.18, libcurl >= 7.85 and json-c >= 0.15 development packages.
Codex is optional and additionally requires an installed CLI, your own login,
bubblewrap and Landlock ABI >= 3. Missing required isolation blocks that adapter.

```sh
python3 run.py --project /path/to/project
python3 run.py --engine none
python3 run.py --engine llama --gguf /path/to/model.gguf
```

Select a worker above the chat and send a task. A local model is selected by
default. No cloud task starts automatically. Each user supplies their own
installed models and, when using Codex, their own account.

The local coordinator can list, read, search, delegate and make bounded file
changes when editing is enabled. A read after a write verifies the saved text;
it does not establish that the changed program compiles or works.
[Coordinator details](docs/COORDINATOR.md).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
./build/ai-multitool-agents-manager --help
./build/ai-multitool-agents-manager-gui
./build/ai-multitool-agents-manager-models --help
```

Terminal-only build without SDL:

```sh
cmake -S . -B build-terminal -DCMAKE_BUILD_TYPE=Release -DAIMT_BUILD_GUI=OFF
cmake --build build-terminal --parallel 2
./build-terminal/ai-multitool-agents-manager
```

For a minimal session CLI without curl/json-c, also set
`-DAIMT_BUILD_LOCAL_SERVICES=OFF`. To use AddressSanitizer and
UndefinedBehaviorSanitizer with GCC/Clang, configure a separate build using
`-DAIMT_ENABLE_SANITIZERS=ON`.

## Documentation

- [Source structure](docs/STRUCTURE.md) and [roadmap](docs/ROADMAP.md).
- [Desktop and plugins](docs/DESKTOP-AND-PLUGINS.md).
- [Local coding agents and specialized roles](docs/LOCAL-CODING-AGENT.md).
- [Model catalog and settings](docs/MODEL-CATALOG-AND-SETTINGS.md).
- [Codex integration and access boundaries](docs/CODEX.md).
- [Security requirements and Safe Mode status](docs/SECURITY-AND-SAFE-MODE.md).

## Privacy and publishing

There is no shared API key or bundled Codex account. Authentication remains in
each user's local Codex CLI installation. Do not add login files, `.env`,
private settings, conversation histories or task results to the repository.

The public source package excludes private state, conversation logs, build
outputs, test suites, Python caches and previous Git history.
[Publishing instructions](docs/PUBLISHING.md).

The download counter tracks files attached to GitHub Releases, starting with
the first published release; it does not count clones or GitHub's source ZIPs.
The README view counter is provided by Hits.sh. Image caching and automated
requests can affect its count, so it is not a count of unique visitors.

## License

[MIT](LICENSE). Use, modify and redistribute the code, including commercially,
while retaining the copyright and license notice. An additional project or
author credit in your interface or description is welcome, but optional.
The software is provided without warranty. Dependencies retain their own
licenses; Nuklear's license is in `thirdparty/nuklear/LICENSE`.
