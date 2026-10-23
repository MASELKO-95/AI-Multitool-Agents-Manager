# Main module

The minimal session CLI is separate from the richer desktop and local services.
It keeps a bounded conversation in RAM and does not connect an AI model.
Files, commands and network tools are unavailable in this minimal mode.

Commands: `/help`, `/status`, `/modules`, `/module NAME`, `/history`, `/clear`,
`/safe` and `/quit`. A rejected or oversized line is discarded as a whole.
A full session does not silently remove older messages. `/clear` explicitly
removes history from the session buffer.

The main module is available; specialist entries in this CLI are placeholders.
`--memory-kib` controls the session core allocation budget, not total process
memory. `--safe` fails because full Safe Mode is not implemented.

For working model generation and project tools, use the desktop or the
coordinator/model CLI executables. See [structure](STRUCTURE.md).
