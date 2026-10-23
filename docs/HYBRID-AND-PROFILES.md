# Hybrid coordination and model profiles

The optional Codex coordinator can delegate short text tasks to a local Ollama
worker with `local_generate`, then review the last result using `local_review`.
The worker receives supplied context, without tools or direct file access.
Reviews are untrusted assessments and are not user verification.

At most four local requests are allowed per turn. The adapter supplies bounded
input and output; consult the code for the active request configuration.
Read-only coordination does not modify the project. The explicit edit mode can
apply changes and run checks through Codex's separately isolated tools.

Reviewed examples remain in conversation memory. Saving examples to the
project requires a separate setting and uses `.aimt-learning/examples.jsonl`
with a size limit. These records may contain private task data and must not be
published. No learning feature here modifies model weights.

Ollama profiles are separate `aimt_*` aliases of an already installed base.
They set context, output and temperature without replacing the original model.
The application validates names and parameters and does not download a missing
base implicitly. A profile is configuration, not a newly trained model or proof
that its requested context fits the hardware.
