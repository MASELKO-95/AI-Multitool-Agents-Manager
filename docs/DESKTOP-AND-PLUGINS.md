# Desktop and plugins

The native desktop uses SDL2 and a pinned Nuklear dependency. Start it through
`python3 run.py` or the built `ai-multitool-agents-manager-gui` executable.
The launcher manages only local servers it starts; direct GUI startup does not
manage those server processes.

The left panel selects project folders and previews files. Each project has
its own edit permission; new projects are read-only. A separate user terminal
opens in the selected folder. Changing projects or worker access clears the
conversation. Switching is blocked while a task is running.

Select a worker above chat. Local text generation, local coordination and
optional Codex modes have different capabilities; see [Codex](CODEX.md) and
[coordinator](COORDINATOR.md). The desktop defaults to a local worker.
Conversation preview is kept in RAM. Saving module settings is a separate action.

## Declarative manifests

`Modules/<id>/plugin.json` describes a module form. A manifest can contain a
stable `id`, title, description, required capabilities and typed settings.
Supported setting types include bool, bounded integer/float, text, multiline
text and enumerated choices. Limits and values are validated in C.

Manifests are data. They cannot execute code or grant permissions. Discovery
loads forms, not arbitrary native plugins. Shared settings cover model choice,
context policy, generation parameters, resource budgets and cloud policy.
Several resource scheduling settings describe future execution behavior and
are not currently enforced by an inference scheduler.

New media modules can be disabled when required model capabilities are absent.
The settings form remains available. Selecting a compatible model does not
implement the corresponding media runtime. Capability declarations are not
quality measurements.

Private settings are stored under `configs/module-settings/` by default.
Do not distribute that directory. Manifests contain public defaults only.
