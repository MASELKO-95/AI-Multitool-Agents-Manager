# Publishing

The public project is named **AI Multitool Agents Manager** and uses the MIT
license. It is an experimental manager for specialized agents. The public
description distinguishes working features from planned automation.

Use the prepared source folder or ZIP. It contains the application, public
module defaults, model metadata, documentation and required third-party files.
It omits account data, private settings, conversations, evaluation histories,
test suites, Python caches, compiled binaries and previous Git history.

Each user supplies their own models and optional Codex login. The application
uses Codex CLI authentication; no shared account or API key is distributed.
Do not copy an existing `auth.json`, `.codex`, `.env`, credential file or local
project list into the repository. `.gitignore` provides additional exclusions;
it does not remove anything already committed or guarantee that all sensitive
content is detected.

Create a separate repository inside the clean source folder. Check its root
before adding files:

```sh
git init --initial-branch=main
git rev-parse --show-toplevel
git add .
git diff --cached --stat
```

Review staged filenames and contents before committing. Git author metadata
comes from your local Git configuration; choose a public name and a private
or GitHub noreply email if needed. No remote publication is performed by
building or launching this program.

The source review checks common credential formats and personal paths without
printing secret values. Such checks cannot identify every possible secret.
If a credential was previously published, deleting a file does not revoke it
or remove historical copies; revoke it at its provider and review that history.

Keep `LICENSE` and dependency licenses in copies or substantial portions of the
code. Additional project credit is welcome but optional. Model weights and
other external assets retain their own licenses.
