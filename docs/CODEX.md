# Optional Codex integration

The C adapter runs a locally installed `codex app-server --listen stdio://`.
Each user supplies their own CLI installation and account. No account, API key
or authentication file is included in the public source tree.

## Modes

- Local model: Ollama text generation without project tools or cloud execution.
- Local coordinator: bounded project tools and local delegation.
- Codex read-only or edit: a task in the selected project.
- Codex coordinates / local worker: short local delegations and reviews.
- Codex coordinates / agents / edit: local delegation plus Codex file changes,
  commands and checks within the selected scope.

Sending a task starts work. Changing folders, model or access starts a new
conversation. Interrupting does not undo earlier writes. The desktop defaults
to a local model. If Codex fails, the local coordinator may take over within the
same selected access scope; user interruption does not trigger a fallback.

## Isolation boundary

The adapter requires bubblewrap, Landlock ABI >= 3 and built helper programs.
Missing isolation blocks startup. Bubblewrap provides process/user namespaces
and private temporary storage. Landlock limits access to the project, required
system libraries/tools and trusted Codex runtime. Read-only mode forbids project
writes; edit mode permits writes in the project and private runtime directory.

Existing `.git`, `.codex`, `.agents`, `.aws`, `.aimt-learning` and
`.aimt-projects` directories are mounted read-only. Links in place of protected
directories are rejected. Edit startup checks up to 65536 entries / 32 levels
and rejects hard links and special files. This is not a comprehensive defense
against concurrent host changes.

The command helper adds inherited Landlock restrictions excluding the private
Codex account store. Seccomp rejects sockets, mount changes, ptrace, access to
other process memory and changes to ownership, permissions, timestamps and
extended attributes. Commands cannot use host network or external IPC; the
app-server's provider connection remains available. The user terminal is
separate and runs with the user's normal permissions.

Named internal Codex profiles are not the security boundary; external helpers
enforce isolation. The client validates the confirmed folder, project scope,
access profile, on-request policy and user reviewer, and rejects permission
expansion. External MCP/plugins/hooks/browser/computer/web/cloud-agent features
are disabled. This does not constitute a complete Multitool Safe Mode.

## Account and data

Codex CLI handles authentication. The adapter does not parse tokens. The user's
existing `auth.json` and `config.toml` are exposed read-only to the trusted CLI
inside a private `CODEX_HOME`; other host account state is hidden. Runtime
sessions/cache/diagnostics use private temporary directories. Device login in
the app is temporary; persistent login is handled by the user's normal CLI.
Publishing source files does not grant access to an existing logged-in account.

Tasks and selected file excerpts are processed by the configured provider.
Conversation preview remains in RAM; optional reviewed local examples are a
separate, explicit setting. Storage encryption is not implemented.

The preview is bounded to 128 KiB, task input to 2048 bytes and protocol frame
to 1 MiB. These are not provider billing or total RAM limits. Task runtime has
no total timeout, while connection startup has a 30-second limit. Local request
budgets and tool availability are described in [coordinator](COORDINATOR.md).

## Verification scope

This distribution omits the test suite and evaluation scripts. A successful
build does not establish complete isolation in every environment. Verify
changes using owned temporary fixtures before using private data or granting
edit access. Do not treat protocol validation as a complete security audit.
