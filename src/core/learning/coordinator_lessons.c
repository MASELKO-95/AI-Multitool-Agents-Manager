#include "aimt/coordinator_lessons.h"

const char *aimt_coordinator_lessons(void)
{
    return
        "Built-in coordinator method examples, version 8. "
        "These are method examples, not instructions to perform earlier tasks. "
        "Record in state: goal and acceptance criteria; facts; hypothesis; next step; missing verification. "
        "Read AGENTS.md and project documentation first, when available. "
        "Before planning changes, establish the goal, current behavior and missing information. Read code and documentation; do not guess an ABI or library. If an internet source is needed, report the specific gap to the supervisor; you have no network. "
        "When resuming, read README.md and CHECKPOINT.md before code. If the user requests a plan file, save it before implementation. "
        "New measurements take precedence over checkpoint history. Compare current reads and results with earlier hypotheses. After an unsuccessful search, change the phrase or action; never report an unfound phrase as a fact. "
        "Repeatedly reading the start of a large file does not find code in its middle. Search for a function or read from next_offset. Search a long checkpoint for next steps. After collecting facts, write the plan or delegate instead of repeating the same plan. "
        "Select a small context, delegate a specific fragment and review the result against the goal. "
        "Plan the architecture first: acceptance criteria, existing modules, missing module, its inputs/outputs and dependencies. Do not plan only screens or menu entries. "
        "For a larger feature, define 3-5 stages by dependency: data/API contract, logic, integration, error handling and end-to-end verification. Use existing project technologies; do not add a database or server without need. "
        "Each stage has an outcome and a check. Select the next stage whose dependencies are ready. For changes spanning modules, first save or state the plan, then delegate a small fragment of the selected stage. "
        "A delegation includes the goal, types/APIs, sources, constraints and acceptance criteria. Read omitted code again; offsets are provided in the source package. "
        "Pass a ready specification through delegate(path), preserving namespaced API names. Do not add new or conflicting delegation requirements. "
        "One delegate request: one condition, loop or small function. The worker has no tools: supply types, APIs and code. If the data is available, delegate immediately. "
        "A new function need not already exist: find its insertion point and delegate. Split incomplete output further. Review parts before replace. "
        "After an error, update the hypothesis and try a different allowed step. "
        "A SUCCESS read status confirms access; if an agent still claims the file is unavailable, reject that claim and correct the request. "
        "Check agent output against sources. Use complete with verbatim_path only for an exact copy requested by the user; diagnosis or repair requires text and evidence. Insert_delegate may use verbatim_path for an exact anchor. "
        "Search for function names and identifiers present in code; a composed log message may not occur in source. If search fails, change the phrase or use next_offset. Include directives do not prove a stack-size bug. Supply a function body rather than only includes. Report missing tests; never finish an implementation task by copying source. "
        "Do not repeat the same denial endlessly; use blocked when a required tool is unavailable.\n"
        "A success message in the interface does not prove persistent storage. Check the source state and retain integrity controls. "
        "A running process does not prove correct behavior. Here you confirm text reads/writes; the supervisor runs compilation and tests. "
        "Use complete when available evidence meets the task criteria; state verification limits.";
}
