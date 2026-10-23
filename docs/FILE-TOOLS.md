# Project file tools

The selected project is the file access scope. Other projects in the saved list
are not automatically exposed. Read-only access is the default; editing requires
an explicit per-project choice. Local model output cannot expand this scope.

The local tools reject absolute paths, traversal, unsafe components, links and
protected metadata. Descriptor-based operations avoid following links during
access. Inputs, reads and writes have explicit byte/count limits. Special files
and unbounded reads are not accepted.

Preview access is separate from agent tools. The desktop file preview is
read-only and limited to 16 KiB, with at most 256 displayed entries. It does not
follow links or provide a full editor.

`read` can return an offset for continuation. `replace` requires one unique exact
fragment and preserves remaining contents. Writes use atomic replacement and
content verification. A changed file does not prove a successful build or test.
Concurrent host activity can still affect results; no general transaction over
the whole project is implemented.

The user's terminal is a separate normal terminal, not the local coordinator's
command executor. The local coordinator has no shell/build/test runner.
