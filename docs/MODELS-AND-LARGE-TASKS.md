# Models and large tasks

The planned core uses C17 with C++17 interfaces where useful. Python remains an
optional adapter for AI libraries. A registry should keep provider, model
identifier, version/digest, format, quantization, declared capabilities,
context limits, license and source metadata separate from measured quality.

The direction is multiple specialized roles sharing explicit contracts, rather
than a single general model. Models can run sequentially. Dividing a task does
not merge their context windows or ensure that dependencies are preserved.
Large projects need a source index, revision-aware APIs, persisted task state
and precise context selection. Most of this infrastructure remains planned.

## Bounded processing

Large localization datasets must be streamed in records, not split blindly on
newlines: quoted CSV fields can contain line breaks. Preserve stable IDs,
placeholders, tags, source language, target language and formatting constraints.
Batch by context budget and meaningful scene boundaries with output reserve.
Validate completeness and structure independently of model judgments.

Save accepted output transactionally and resume only missing or invalidated
records. Source, glossary or model changes must invalidate dependent results.
Context-dependent duplicates cannot be collapsed solely because source strings
match. Structural validation does not prove translation quality.

These are requirements for future storage, scheduling and validation modules.
Do not describe the complete large-dataset pipeline as implemented.
