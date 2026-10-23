# Declarative modules

`plugin.json` files describe forms for `main`, `specification`, `coding`,
`translation`, `support`, `images`, `video`, `mesh`, `speech` and `avatar`.
The desktop discovers manifests at startup; it does not load executable code.

A module describes inputs, settings, required capabilities and future runtime
needs. Permission enforcement belongs to the shared program, not the manifest.
The optional `requires` list can include completion, vision, embedding, tools,
image_generation, stt, tts and mesh_generation. Discovery can disable a module
when required capabilities are missing. It does not activate devices or adapters.

See [desktop and plugins](../docs/DESKTOP-AND-PLUGINS.md) and
[media plans](../docs/MEDIA-VOICE-AND-AVATAR.md). Most specialized media runtimes
are not implemented. Keep private settings outside public manifests.
