# Local coordinator

The coordinator uses Ollama and a validated JSON decision protocol. Built-in
method examples live in `src/core/learning/coordinator_lessons.c`; private
conversations and real project histories are not distributed as training data.
No model weights are changed by this mechanism.

Actions include `list`, `read`, `search`, `write`, `replace`, `delegate`,
`insert_delegate`, `complete` and `blocked`. The program validates requests and
applies the selected project's permissions. `state` records progress;
`blocked` requires nonempty `text`, while `complete` requires `text` or
`verbatim_path`. A decision is not a command shell.

`read` supports a byte offset and returns `next_offset`. `search` locates a
literal phrase in one file. Source context holds up to four fragments / 12288
bytes and reports omissions. `replace` changes one unique fragment in a file
up to 1 MiB, preserving the rest and verifying the result by reading again.
Unsafe paths, links, protected metadata, ambiguous matches and oversized input
are rejected. Concurrent host modifications are not comprehensively prevented.

`delegate(path)` reads an exact specification up to 16384 bytes.
`delegate(text)` supplies a short task directly. The worker has no tools;
provide the necessary types, APIs and code. `insert_delegate` inserts the last
successful worker result before a unique anchor. An anchor file can be supplied
through `verbatim_path`. Failed delegation clears the previous reusable result.

## Models and budgets

Empty model fields use a heuristic based on the task, declared capabilities,
local availability and RAM budget. Model names and capabilities are not quality
measurements. Manual model selection takes precedence. Selecting a model does
not download weights. Llama.cpp discovery/server startup does not yet provide a
chat adapter equivalent to Ollama.

Context options reach 131072 tokens, with a default of 8192. Generation can
request 8192 tokens, but result/protocol byte limits remain 8192 bytes. A large
token budget does not remove byte limits, ensure a model supports that context
or guarantee it fits RAM/VRAM. Long-response regression coverage is incomplete.

The coordinator has no shell, build/test runner, network tools, training or
persistent task recovery. A verified write establishes text contents, not
correct behavior. Automatic build/test repair remains planned.

## CLI and diagnostics

Use `ai-multitool-agents-manager-coordinate --help` for arguments. The CLI is
read-only unless `--edit` is provided, requires a new JSONL output file and
supports bounded steps/time. `--chat` uses Ollama's native chat renderer;
`--live` prints journal records to stderr as operations complete. It is not
streaming token generation or private internal reasoning.

Journals can include task inputs, source excerpts, decisions and outputs. Keep
them private. `generation_metadata` reports role, model, termination reason,
byte sizes and available token counters. Missing counters stay unknown.

The desktop stores a private project list in `.aimt-projects/projects.json`
with separate edit permission per project. Changing project clears the current
conversation and worker connection. Switching while a task runs is blocked.
