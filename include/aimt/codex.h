#ifndef AIMT_CODEX_H
#define AIMT_CODEX_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIMT_CODEX_MODELS_MAX 32
#define AIMT_CODEX_HISTORY_MAX (128u * 1024u)

typedef struct aimt_codex aimt_codex;
typedef struct { char id[128]; char title[128]; } aimt_codex_model;
typedef struct {
    bool connected, authenticated, busy, login_pending, task_failed;
    size_t model_count;
    aimt_codex_model models[AIMT_CODEX_MODELS_MAX];
    char status[512];
    char login_url[512], login_code[128];
    char activity[1024];
} aimt_codex_status;

aimt_codex *aimt_codex_create(void);
void aimt_codex_destroy(aimt_codex *client);
bool aimt_codex_connect(aimt_codex *client, const char *executable, const char *project, bool allow_write);
void aimt_codex_disconnect(aimt_codex *client);
void aimt_codex_poll(aimt_codex *client);
const aimt_codex_status *aimt_codex_get_status(const aimt_codex *client);
const char *aimt_codex_history(const aimt_codex *client);
bool aimt_codex_login(aimt_codex *client);
/* New conversation; never reuse a thread across projects or permission changes. */
bool aimt_codex_clear(aimt_codex *client);
/* Explicit user-selected hybrid mode. Local model and endpoint cannot be chosen by AI. */
bool aimt_codex_permission_json(aimt_codex *client, char *out, size_t capacity);
bool aimt_codex_hybrid(aimt_codex *client, bool enabled, const char *endpoint,
                       const char *local_model, bool save_examples);
bool aimt_codex_send(aimt_codex *client, const char *project, const char *model,
                     const char *message, bool allow_write);
bool aimt_codex_interrupt(aimt_codex *client);

#ifdef __cplusplus
}
#endif
#endif
