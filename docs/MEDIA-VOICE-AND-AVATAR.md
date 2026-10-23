# Media, voice and avatar modules

These modules currently provide declarative forms and requirements. Their
runtime adapters are not implemented. Finding local checkpoints or selecting a
model does not activate generation, a microphone or a camera.

| Module | Planned behavior |
| --- | --- |
| Images | Generation/editing through a dedicated image engine |
| Video | Frame analysis and an edit plan reviewed before export |
| Mesh | Geometry generation, optional textures and export validation |
| Speech | Local speech recognition and XTTS v2 synthesis |
| Avatar | Voice-driven 2D/3D character with optional camera input |

Text/vision models, image generators, speech models and mesh generators have
different interfaces and resource requirements. A vision capability declaration
does not make a model an image or mesh generator. Voice needs both recognition
and synthesis components; an XTTS installation does not provide STT.

Future device access must be explicit and revocable. Reference recordings,
camera frames and generated outputs can be private. Keep them outside public
sources. Use bounded chunks and cancellation; show a preview before exporting.
The application must not infer emotions or intentions from camera frames alone.

Check each engine and model license separately. The repository's MIT license
does not relicense external model weights or voice recordings.
