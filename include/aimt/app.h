#ifndef AIMT_APP_H
#define AIMT_APP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIMT_MESSAGE_MAX_BYTES 2048u
#define AIMT_SESSION_MIN_BYTES (8u * 1024u)
#define AIMT_SESSION_MAX_BYTES (4096u * 1024u)

typedef struct aimt_app aimt_app;

typedef enum {
    AIMT_OK,
    AIMT_INVALID,
    AIMT_NO_MEMORY,
    AIMT_UNAVAILABLE,
    AIMT_DENIED,
    AIMT_FULL
} aimt_result;

typedef enum {
    AIMT_CAP_READ_FILES,
    AIMT_CAP_WRITE_FILES,
    AIMT_CAP_COMMANDS,
    AIMT_CAP_NETWORK
} aimt_capability;

typedef struct {
    size_t session_budget_bytes;
    bool require_safe_mode;
} aimt_config;

typedef struct {
    const char *id;
    const char *description;
    bool available;
} aimt_module_info;

typedef struct {
    size_t allocated_bytes;
    size_t budget_bytes;
    size_t message_count;
    size_t message_capacity;
    size_t text_bytes;
    const char *active_module;
} aimt_status;

aimt_config aimt_config_default(void);
aimt_result aimt_app_create(const aimt_config *config, aimt_app **out);
void aimt_app_destroy(aimt_app *app);
aimt_result aimt_app_status(const aimt_app *app, aimt_status *out);
size_t aimt_module_count(void);
const aimt_module_info *aimt_module_at(size_t index);
aimt_result aimt_app_select_module(aimt_app *app, const char *id);
/* Stores a user message only. No model inference is performed. */
aimt_result aimt_app_submit(aimt_app *app, const char *text, size_t length);
const char *aimt_app_message(const aimt_app *app, size_t index);
void aimt_app_clear(aimt_app *app);
/* All capabilities are denied until real executors are implemented. */
aimt_result aimt_app_authorize(const aimt_app *app, aimt_capability capability);
bool aimt_text_valid(const char *text, size_t length);

#ifdef __cplusplus
}
#endif

#endif
