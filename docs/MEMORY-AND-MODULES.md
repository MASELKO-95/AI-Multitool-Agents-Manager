# Memory and modules

Current session state and conversation preview are bounded and kept in RAM.
Private module settings and the project list persist separately. There is no
implemented encrypted archive or general persistent agent memory service.

Future storage should distinguish user-selected memories, project sources,
task state, generated proposals and verified results. Every context item needs
provenance and a source revision. A model-generated claim is not a verified fact.
User access policies govern retrieval as well as writes and exports.

Large data must be processed incrementally with explicit byte/count limits,
checked arithmetic and allocation failures. Do not keep an entire dataset or
all model weights in RAM by default. Cancellation and interruption need a
recoverable state that is validated against current files before resuming.

A specialist module defines inputs, outputs, required context, model roles and
tool requirements. Its manifest is data and cannot grant permissions. The
shared runtime enforces policy. Future indexes, archives and training examples
may contain private information and require deliberate storage and export rules.
