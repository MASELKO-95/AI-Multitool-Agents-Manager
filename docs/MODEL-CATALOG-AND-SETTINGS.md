# Model catalog and settings

`configs/model-catalog.json` contains public model suggestions with provenance,
roles, approximate sizes and license notes. These are candidates for local
evaluation, not a guarantee of compatibility, stability or quality. Catalog
verification dates describe earlier checks; verify changing upstream details
before downloading or redistributing weights.

Discovery queries local engine metadata. It does not load or download every
model. Empty model fields use a heuristic based on task, declared capability,
local availability and the configured RAM budget. A manually selected model
takes precedence. Missing size metadata can exclude automatic candidates.

Requested context, model size, KV cache, runtime buffers and other processes all
affect memory use. A file-size estimate plus reserve is not an enforced RAM/VRAM
limit. Measure the selected model with the intended task. A large context value
does not establish model support or improved task quality.

The model panel filters discovered entries and can open online searches in a
browser. Declared text, vision, embedding or tool support is not a measured
quality score. Image understanding does not imply image generation.

Ollama operations are restricted to validated loopback HTTP endpoints. Explicit
in-app download requires confirmation and an installed Ollama service. Startup
and automatic model selection do not download weights. Profiles set parameters
of a separate alias of an installed model.
