# Learning, context and interface

The application currently has reviewed, built-in coordinator method examples
and an optional record of reviewed local results. These affect supplied context;
they do not train or update model weights. Private conversation logs and real
project histories are excluded from the public distribution.

A future learning pipeline needs explicit user consent, source provenance,
separation of training and held-out evaluation, actual verification of repairs
and a rollback path. An AI reviewer approving code is insufficient evidence.
Do not present instruction tuning as vocabulary pruning or parameter reduction.

Context should contain the smallest useful sources and contracts, preserving
identifiers and exact APIs. Truncation, omissions, unknown metrics and missing
verification must be visible. Repeated unsupported model assertions must not
silently become trusted memory.

The interface should distinguish plans, running operations, saved changes and
verified results. Show the selected worker, project access and interruption
state. Do not display a completed plan as a completed build. Diagnostic logs
may contain private content and must stay out of public exports.
