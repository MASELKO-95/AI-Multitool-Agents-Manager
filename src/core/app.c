#include "aimt/app.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char text[AIMT_MESSAGE_MAX_BYTES + 1u];
} aimt_message;

struct aimt_app {
    size_t budget;
    size_t allocated;
    size_t capacity;
    size_t count;
    size_t text_bytes;
    size_t module_index;
    aimt_message messages[];
};

static const aimt_module_info modules[] = {
    {"main", "Chat and session management; no model connected yet", true},
    {"specification", "Requirements interpretation - planned", false},
    {"coding", "Coding - planned", false},
    {"translation", "Translation — planowany", false},
    {"support", "Supportive conversation - planned", false}
};

/* Reject malformed UTF-8 and terminal control characters, including C1. */
bool aimt_text_valid(const char *text, size_t length)
{
    size_t i = 0;
    if (text == NULL) return false;
    while (i < length) {
        uint32_t code;
        uint32_t minimum;
        size_t continuation;
        unsigned char first = (unsigned char)text[i++];
        if (first < 0x80u) {
            code = first; minimum = 0; continuation = 0;
        } else if (first >= 0xc2u && first <= 0xdfu) {
            code = first & 0x1fu; minimum = 0x80u; continuation = 1;
        } else if (first >= 0xe0u && first <= 0xefu) {
            code = first & 0x0fu; minimum = 0x800u; continuation = 2;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            code = first & 0x07u; minimum = 0x10000u; continuation = 3;
        } else {
            return false;
        }
        if (continuation > length - i) return false;
        for (size_t j = 0; j < continuation; ++j) {
            unsigned char next = (unsigned char)text[i++];
            if ((next & 0xc0u) != 0x80u) return false;
            code = (code << 6) | (next & 0x3fu);
        }
        if (code < minimum || code > 0x10ffffu ||
            (code >= 0xd800u && code <= 0xdfffu)) return false;
        if ((code < 0x20u && code != '\t' && code != '\n') ||
            (code >= 0x7fu && code <= 0x9fu)) return false;
    }
    return true;
}

aimt_config aimt_config_default(void)
{
    aimt_config config = {64u * 1024u, false};
    return config;
}

aimt_result aimt_app_create(const aimt_config *config, aimt_app **out)
{
    size_t capacity;
    size_t allocated;
    aimt_app *app;
    if (out == NULL) return AIMT_INVALID;
    *out = NULL;
    if (config == NULL || config->session_budget_bytes < AIMT_SESSION_MIN_BYTES ||
        config->session_budget_bytes > AIMT_SESSION_MAX_BYTES) return AIMT_INVALID;
    /* Never silently downgrade a requested security mode. */
    if (config->require_safe_mode) return AIMT_UNAVAILABLE;
    if (config->session_budget_bytes <= sizeof(aimt_app)) return AIMT_INVALID;
    capacity = (config->session_budget_bytes - sizeof(aimt_app)) / sizeof(aimt_message);
    if (capacity > 256u) capacity = 256u;
    if (capacity == 0) return AIMT_INVALID;
    allocated = sizeof(aimt_app) + capacity * sizeof(aimt_message);
    app = calloc(1, allocated);
    if (app == NULL) return AIMT_NO_MEMORY;
    app->budget = config->session_budget_bytes;
    app->allocated = allocated;
    app->capacity = capacity;
    *out = app;
    return AIMT_OK;
}

static void wipe(void *buffer, size_t length)
{
    volatile unsigned char *bytes = buffer;
    while (length-- > 0) *bytes++ = 0;
}

void aimt_app_destroy(aimt_app *app)
{
    if (app != NULL) {
        size_t length = app->allocated;
        wipe(app, length);
        free(app);
    }
}

aimt_result aimt_app_status(const aimt_app *app, aimt_status *out)
{
    if (app == NULL || out == NULL) return AIMT_INVALID;
    *out = (aimt_status){app->allocated, app->budget, app->count,
                         app->capacity, app->text_bytes, modules[app->module_index].id};
    return AIMT_OK;
}

size_t aimt_module_count(void)
{
    return sizeof(modules) / sizeof(modules[0]);
}

const aimt_module_info *aimt_module_at(size_t index)
{
    return index < aimt_module_count() ? &modules[index] : NULL;
}

aimt_result aimt_app_select_module(aimt_app *app, const char *id)
{
    if (app == NULL || id == NULL) return AIMT_INVALID;
    for (size_t i = 0; i < aimt_module_count(); ++i) {
        if (strcmp(id, modules[i].id) == 0) {
            if (!modules[i].available) return AIMT_UNAVAILABLE;
            app->module_index = i;
            return AIMT_OK;
        }
    }
    return AIMT_INVALID;
}

aimt_result aimt_app_submit(aimt_app *app, const char *text, size_t length)
{
    if (app == NULL || length == 0 || length > AIMT_MESSAGE_MAX_BYTES ||
        !aimt_text_valid(text, length)) return AIMT_INVALID;
    /* Do not evict conversation context silently. */
    if (app->count == app->capacity) return AIMT_FULL;
    memcpy(app->messages[app->count].text, text, length);
    app->messages[app->count].text[length] = '\0';
    app->count++;
    app->text_bytes += length;
    return AIMT_OK;
}

const char *aimt_app_message(const aimt_app *app, size_t index)
{
    return app != NULL && index < app->count ? app->messages[index].text : NULL;
}

void aimt_app_clear(aimt_app *app)
{
    if (app != NULL) {
        wipe(app->messages, app->capacity * sizeof(aimt_message));
        app->count = 0;
        app->text_bytes = 0;
    }
}

aimt_result aimt_app_authorize(const aimt_app *app, aimt_capability capability)
{
    if (app == NULL || capability < AIMT_CAP_READ_FILES || capability > AIMT_CAP_NETWORK)
        return AIMT_INVALID;
    return AIMT_DENIED;
}
