#ifndef AIMT_PLUGIN_H
#define AIMT_PLUGIN_H

#include <stddef.h>
#include "aimt/model_capability.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

#define AIMT_PLUGIN_MAX 24
#define AIMT_SETTING_MAX 20
#define AIMT_CHOICE_MAX 8
#define AIMT_SETTING_TEXT 512

typedef enum { AIMT_SETTING_TEXT_TYPE, AIMT_SETTING_INT, AIMT_SETTING_FLOAT,
               AIMT_SETTING_BOOL, AIMT_SETTING_CHOICE } aimt_setting_type;

typedef struct {
    char key[64];
    char label[128];
    char help[256];
    aimt_setting_type type;
    double minimum, maximum;
    int integer;
    float decimal;
    char text[AIMT_SETTING_TEXT];
    char choices[AIMT_CHOICE_MAX][64];
    int choice_count;
    int selected;
} aimt_setting;

typedef struct {
    char id[64];
    char title[128];
    char description[512];
    bool implemented;
    unsigned required_capabilities;
    aimt_setting settings[AIMT_SETTING_MAX];
    size_t setting_count;
} aimt_plugin;

typedef struct {
    aimt_plugin plugins[AIMT_PLUGIN_MAX];
    size_t count;
    size_t rejected;
} aimt_plugin_registry;

/* Data-only manifests; never loads or executes code supplied by a plugin. */
bool aimt_plugin_parse(const char *json, size_t length, aimt_plugin *out);
bool aimt_plugins_load(const char *directory, aimt_plugin_registry *out);
aimt_setting *aimt_plugin_setting(aimt_plugin *plugin, const char *key);
bool aimt_plugin_validate(const aimt_plugin *plugin);
/* Explicit user configuration. Does not change manifests, schemas or privileges.
 * Save creates a private directory/file; reads and writes refuse symlinks. */
bool aimt_plugin_settings_save(const char *directory, const aimt_plugin *plugin);
bool aimt_plugin_settings_load(const char *directory, aimt_plugin *plugin);

#ifdef __cplusplus
}
#endif

#endif
