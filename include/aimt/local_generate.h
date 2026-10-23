#ifndef AIMT_LOCAL_GENERATE_H
#define AIMT_LOCAL_GENERATE_H
#include <stdbool.h>
#define AIMT_LOCAL_OUTPUT_MAX 8192u
#define AIMT_LOCAL_PROMPT_MAX 32768u
typedef struct aimt_local_job aimt_local_job;
typedef void (*aimt_local_observer)(void *data, const char *event, unsigned step, const char *text);
typedef struct {
    unsigned max_steps; /* 1..128; zero uses 128. */
    aimt_local_observer observer; /* Called on worker thread; borrowed strings. */
    void *observer_data;
    bool chat; /* Opt-in native chat renderer instead of legacy generate. */
    unsigned coordinator_context, worker_context; /* 2048..131072; zero uses 8192. */
    unsigned threads; /* 1..64; zero leaves Ollama's default. */
    unsigned request_timeout; /* 1..3600 seconds; zero uses 180. */
    bool unload; /* Release each model after its request on memory-limited hosts. */
    bool file_briefs; /* Delegate only exact project files; no model paraphrases. */
} aimt_local_options;
/* Local text worker: no tools, files, commands, redirects or model downloads. */
aimt_local_job *aimt_local_start(const char *endpoint, const char *model, const char *prompt);
/* Local coordinator selects bounded project context and delegates text work.
 * No shell, cloud calls, training or model downloads. */
aimt_local_job *aimt_local_coordinate(const char *endpoint, const char *coordinator,
    const char *worker, const char *project, const char *prompt, bool allow_write);
aimt_local_job *aimt_local_coordinate_options(const char *endpoint, const char *coordinator,
    const char *worker, const char *project, const char *prompt, bool allow_write,
    const aimt_local_options *options);
bool aimt_local_done(const aimt_local_job *job);
const char *aimt_local_result(const aimt_local_job *job, bool *success);
/* Bounded operation trace, readable only after completion. */
const char *aimt_local_trace(const aimt_local_job *job);
void aimt_local_cancel(aimt_local_job *job);
void aimt_local_destroy(aimt_local_job *job);
#endif
