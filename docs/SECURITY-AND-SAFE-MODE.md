# Security requirements and Safe Mode

This document separates current controls from requirements. Full Safe Mode,
encrypted storage and cryptographic key management are not implemented.
The application rejects attempts to enable the incomplete Safe Mode.

Security and resource use have equal priority. Do not remove integrity checks
or access controls as an optimization. Treat model output, files, manifests,
network metadata and imported documents as untrusted data.

## Current controls

The session has bounded input and allocation budgets. Local project tools
validate paths and use descriptor-based access, refuse links and protected
metadata, and verify saved contents. The optional [Codex adapter](CODEX.md)
has a separate isolation boundary. Module settings and the project list use
private files and atomic replacement, without encryption.

These controls do not imply an audited complete sandbox or immunity to
concurrent host changes. A compromised host or administrator can access data
available to the application. Models need readable context during inference.

## Requirements for future Safe Mode

Default local operation; explicit cloud permission; approved modules; bounded
and isolated tools; read-only project access unless editing is granted; no
arbitrary commands built from model text; persistent access policies; minimum
necessary context; clear active permissions and cancellation.

Enabling Safe Mode must actually stop conflicting work and revalidate state.
An interface label is insufficient. Missing required isolation must cause a
refusal, not a silent fallback to weaker execution.

Use established cryptographic libraries such as libsodium when implementing
storage encryption. Do not create custom algorithms. Design authentication,
streaming, nonce handling, key derivation, rotation, recovery, integrity and
failure behavior. Account for plaintext metadata, indexes, temporary files,
model caches and WALs. Encryption at rest does not protect plaintext inference
or every copy in RAM/VRAM.

## Verification rules

Security changes need a reproduction, a regression test and denial/bypass
cases in owned fixtures. Test path traversal, links, permission expansion,
malformed/truncated/oversized messages, allocation failure and prompt injection.
Verify effects, not only success or denial messages. Sanitizers and fuzzing
complement behavioral tests. Report the tested scope, failures and unresolved
limits; do not promise an absence of vulnerabilities.
