#define _POSIX_C_SOURCE 200809L
#include "aimt/plugin.h"
#include "aimt/app.h"

#include <json-c/json.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool text_property(struct json_object *object, const char *key, char *out, size_t size)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value) ||
        !json_object_is_type(value, json_type_string)) return false;
    const char *text = json_object_get_string(value);
    size_t length = (size_t)json_object_get_string_len(value);
    if (length >= size || !aimt_text_valid(text, length)) return false;
    memcpy(out, text, length + 1u);
    return true;
}

static bool identifier(const char *value)
{
    if (value[0] == '\0') return false;
    for (size_t i = 0; value[i]; ++i)
        if (!((value[i] >= 'a' && value[i] <= 'z') ||
              (value[i] >= '0' && value[i] <= '9') || value[i] == '_')) return false;
    return true;
}

static void common_settings(aimt_plugin *plugin)
{
    aimt_setting *s = plugin->settings;
    s[0] = (aimt_setting){.key="provider", .label="AI engine", .type=AIMT_SETTING_CHOICE,
        .choices={"Automatic", "Ollama", "llama.cpp"}, .choice_count=3};
    s[1] = (aimt_setting){.key="model", .label="Model", .type=AIMT_SETTING_TEXT_TYPE,
        .help="Select a discovered model lub wpisz jego identyfikator."};
    s[2] = (aimt_setting){.key="context_mode", .label="Context selection", .type=AIMT_SETTING_CHOICE,
        .choices={"Task-based", "Maximum stable", "Manual"}, .choice_count=3};
    s[3] = (aimt_setting){.key="context_tokens", .label="Context limit (tokens)",
        .type=AIMT_SETTING_INT, .minimum=512, .maximum=131072, .integer=4096,
        .help="Requested limit. Available context depends on the model, engine and memory."};
    s[4] = (aimt_setting){.key="output_tokens", .label="Output limit (tokens)",
        .type=AIMT_SETTING_INT, .minimum=64, .maximum=16384, .integer=1024};
    s[5] = (aimt_setting){.key="temperature", .label="Temperature", .type=AIMT_SETTING_FLOAT,
        .minimum=0, .maximum=2, .decimal=0.2f};
    s[6] = (aimt_setting){.key="ram_mib", .label="Worker RAM budget (MiB)",
        .type=AIMT_SETTING_INT, .minimum=256, .maximum=131072, .integer=8192,
        .help="Configuration for the future scheduler; not currently enforced."};
    s[7] = (aimt_setting){.key="vram_mib", .label="Worker VRAM budget (MiB)",
        .type=AIMT_SETTING_INT, .minimum=0, .maximum=131072, .integer=6144};
    s[8] = (aimt_setting){.key="cloud_policy", .label="Cloud assistance for this module", .type=AIMT_SETTING_CHOICE,
        .choices={"Local only", "Assistance after an error", "Assistance and review"}, .choice_count=3,
        .help="Also requires global permission and a connected account. The adapter is not implemented."};
    s[9] = (aimt_setting){.key="cloud_model", .label="Coordinator / reviewer model", .type=AIMT_SETTING_TEXT_TYPE,
        .help="The identifier will be checked after connecting a provider."};
    plugin->setting_count = 10;
}

aimt_setting *aimt_plugin_setting(aimt_plugin *plugin, const char *key)
{
    if (plugin == NULL || key == NULL || plugin->setting_count > AIMT_SETTING_MAX) return NULL;
    for (size_t i = 0; i < plugin->setting_count; ++i)
        if (strcmp(plugin->settings[i].key, key) == 0) return &plugin->settings[i];
    return NULL;
}

bool aimt_plugin_validate(const aimt_plugin *plugin)
{
    if (plugin == NULL || plugin->setting_count > AIMT_SETTING_MAX) return false;
    const char *id_end = memchr(plugin->id, 0, sizeof(plugin->id));
    const char *title_end = memchr(plugin->title, 0, sizeof(plugin->title));
    const char *description_end = memchr(plugin->description, 0, sizeof(plugin->description));
    if (!id_end || !identifier(plugin->id) || !title_end || !plugin->title[0] ||
        !aimt_text_valid(plugin->title, (size_t)(title_end - plugin->title)) || !description_end ||
        !aimt_text_valid(plugin->description, (size_t)(description_end - plugin->description))) return false;
    for (size_t i = 0; i < plugin->setting_count; ++i) {
        const aimt_setting *s = &plugin->settings[i];
        if (s->type == AIMT_SETTING_INT && (s->integer < s->minimum || s->integer > s->maximum)) return false;
        if (s->type == AIMT_SETTING_FLOAT && (!isfinite(s->decimal) ||
            s->decimal < s->minimum || s->decimal > s->maximum)) return false;
        if (s->type == AIMT_SETTING_BOOL && s->integer != 0 && s->integer != 1) return false;
        if (s->type == AIMT_SETTING_CHOICE && (s->choice_count < 1 || s->choice_count > AIMT_CHOICE_MAX ||
            s->selected < 0 || s->selected >= s->choice_count)) return false;
        if (s->type == AIMT_SETTING_TEXT_TYPE) {
            const char *end = memchr(s->text, '\0', sizeof(s->text));
            if (end == NULL || !aimt_text_valid(s->text, (size_t)(end - s->text))) return false;
        }
    }
    const aimt_setting *context = NULL, *output = NULL;
    for (size_t i = 0; i < plugin->setting_count; ++i) {
        if (strcmp(plugin->settings[i].key, "context_tokens") == 0) context = &plugin->settings[i];
        if (strcmp(plugin->settings[i].key, "output_tokens") == 0) output = &plugin->settings[i];
    }
    if (context != NULL && output != NULL && output->integer >= context->integer) return false;
    return true;
}

static bool numeric_property(struct json_object *object, const char *key, double *out)
{
    struct json_object *value;
    if (!json_object_object_get_ex(object, key, &value) ||
        !(json_object_is_type(value, json_type_int) || json_object_is_type(value, json_type_double))) return false;
    *out = json_object_get_double(value);
    return isfinite(*out);
}

bool aimt_plugin_parse(const char *json, size_t length, aimt_plugin *out)
{
    struct json_tokener *tokener;
    struct json_object *root = NULL, *fields = NULL, *version = NULL;
    bool ok = false;
    if (json == NULL || out == NULL || length == 0 || length > 32768) return false;
    memset(out, 0, sizeof(*out));
    tokener = json_tokener_new_ex(16);
    if (tokener == NULL) return false;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    root = json_tokener_parse_ex(tokener, json, (int)length);
    if (json_tokener_get_error(tokener) != json_tokener_success ||
        !json_object_is_type(root, json_type_object)) goto done;
    if (!json_object_object_get_ex(root, "schema_version", &version) ||
        !json_object_is_type(version, json_type_int) || json_object_get_int64(version) != 1) goto done;
    if (!text_property(root, "id", out->id, sizeof(out->id)) || !identifier(out->id) ||
        !text_property(root, "title", out->title, sizeof(out->title)) ||
        !text_property(root, "description", out->description, sizeof(out->description))) goto done;
    /* Only compiled-in implementations may become executable. */
    out->implemented = strcmp(out->id, "main") == 0;
    out->required_capabilities = AIMT_MODEL_COMPLETION;
    struct json_object *requires;
    if (json_object_object_get_ex(root, "requires", &requires)) {
        if (!json_object_is_type(requires, json_type_array) || json_object_array_length(requires) < 1 ||
            json_object_array_length(requires) > 4) goto done;
        out->required_capabilities = 0;
        for (size_t i = 0; i < json_object_array_length(requires); ++i) {
            struct json_object *r = json_object_array_get_idx(requires, i);
            if (!json_object_is_type(r, json_type_string)) goto done;
            const char *s = json_object_get_string(r);
            if ((size_t)json_object_get_string_len(r) != strlen(s)) goto done;
            unsigned bit = !strcmp(s, "completion") ? AIMT_MODEL_COMPLETION : !strcmp(s, "vision") ? AIMT_MODEL_VISION :
                !strcmp(s, "embedding") ? AIMT_MODEL_EMBEDDING : !strcmp(s, "tools") ? AIMT_MODEL_TOOLS :
                !strcmp(s, "image_generation") ? AIMT_MODEL_IMAGE_GENERATION : !strcmp(s, "stt") ? AIMT_MODEL_STT :
                !strcmp(s, "tts") ? AIMT_MODEL_TTS : !strcmp(s, "mesh_generation") ? AIMT_MODEL_MESH_GENERATION : 0;
            if (!bit || (out->required_capabilities & bit)) goto done;
            out->required_capabilities |= bit;
        }
    }
    common_settings(out);
    if (!json_object_object_get_ex(root, "settings", &fields) ||
        !json_object_is_type(fields, json_type_array) ||
        json_object_array_length(fields) > AIMT_SETTING_MAX - out->setting_count) goto done;
    for (size_t i = 0; i < json_object_array_length(fields); ++i) {
        struct json_object *field = json_object_array_get_idx(fields, i), *value = NULL;
        aimt_setting setting = {0};
        char type[32];
        if (!json_object_is_type(field, json_type_object) ||
            !text_property(field, "key", setting.key, sizeof(setting.key)) || !identifier(setting.key) ||
            aimt_plugin_setting(out, setting.key) != NULL ||
            !text_property(field, "label", setting.label, sizeof(setting.label)) ||
            !text_property(field, "type", type, sizeof(type)) ||
            !json_object_object_get_ex(field, "default", &value)) goto done;
        struct json_object *help = NULL;
        if (json_object_object_get_ex(field, "help", &help) &&
            !text_property(field, "help", setting.help, sizeof(setting.help))) goto done;
        if (strcmp(type, "text") == 0) {
            setting.type = AIMT_SETTING_TEXT_TYPE;
            if (!text_property(field, "default", setting.text, sizeof(setting.text))) goto done;
        } else if (strcmp(type, "bool") == 0) {
            setting.type = AIMT_SETTING_BOOL;
            if (!json_object_is_type(value, json_type_boolean)) goto done;
            setting.integer = json_object_get_boolean(value);
        } else if (strcmp(type, "int") == 0 || strcmp(type, "float") == 0) {
            double number;
            if (!numeric_property(field, "min", &setting.minimum) ||
                !numeric_property(field, "max", &setting.maximum) ||
                !numeric_property(field, "default", &number) ||
                setting.minimum > setting.maximum || setting.minimum < -1000000 ||
                setting.maximum > 1000000 || number < setting.minimum || number > setting.maximum) goto done;
            if (strcmp(type, "int") == 0) {
                setting.type = AIMT_SETTING_INT;
                if (!json_object_is_type(value, json_type_int) ||
                    floor(setting.minimum) != setting.minimum || floor(setting.maximum) != setting.maximum) goto done;
                setting.integer = (int)number;
            } else { setting.type = AIMT_SETTING_FLOAT; setting.decimal = (float)number; }
        } else if (strcmp(type, "choice") == 0) {
            struct json_object *choices;
            setting.type = AIMT_SETTING_CHOICE;
            if (!json_object_object_get_ex(field, "choices", &choices) ||
                !json_object_is_type(choices, json_type_array) ||
                !json_object_is_type(value, json_type_int)) goto done;
            size_t count = json_object_array_length(choices);
            if (count < 1 || count > AIMT_CHOICE_MAX) goto done;
            setting.choice_count = (int)count;
            int64_t selected = json_object_get_int64(value);
            if (selected < 0 || selected >= (int64_t)count) goto done;
            setting.selected = (int)selected;
            for (size_t j = 0; j < count; ++j) {
                struct json_object *choice = json_object_array_get_idx(choices, j);
                if (!json_object_is_type(choice, json_type_string)) goto done;
                size_t n = (size_t)json_object_get_string_len(choice);
                if (n >= sizeof(setting.choices[j]) ||
                    !aimt_text_valid(json_object_get_string(choice), n)) goto done;
                memcpy(setting.choices[j], json_object_get_string(choice), n + 1u);
            }
        } else goto done;
        out->settings[out->setting_count++] = setting;
    }
    ok = aimt_plugin_validate(out);
done:
    if (root != NULL) json_object_put(root);
    json_tokener_free(tokener);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

bool aimt_plugins_load(const char *directory, aimt_plugin_registry *out)
{
    if (directory == NULL || out == NULL) return false;
    memset(out, 0, sizeof(*out));
    DIR *dir = opendir(directory);
    if (dir == NULL) return false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        struct stat st;
        if (fstatat(dirfd(dir), entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(st.st_mode)) continue;
        int subdir = openat(dirfd(dir), entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (subdir < 0) { out->rejected++; continue; }
        int file = openat(subdir, "plugin.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        close(subdir);
        if (file < 0) continue;
        char buffer[32769];
        size_t used = 0;
        bool valid = fstat(file, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 && st.st_size <= 32768;
        if (valid) {
            ssize_t n;
            while (used < sizeof(buffer) && (n = read(file, buffer + used, sizeof(buffer) - used)) > 0)
                used += (size_t)n;
            if (used == sizeof(buffer)) valid = false;
            if (used < sizeof(buffer) && n < 0) valid = false;
        }
        close(file);
        aimt_plugin plugin;
        if (!valid || !aimt_plugin_parse(buffer, used, &plugin) || out->count == AIMT_PLUGIN_MAX) {
            out->rejected++; continue;
        }
        bool duplicate = false;
        for (size_t i = 0; i < out->count; ++i)
            if (strcmp(out->plugins[i].id, plugin.id) == 0) duplicate = true;
        if (duplicate) { out->rejected++; continue; }
        out->plugins[out->count++] = plugin;
    }
    closedir(dir);
    /* Stable order, main first. */
    for (size_t i = 0; i < out->count; ++i)
        for (size_t j = i + 1; j < out->count; ++j)
            if (strcmp(out->plugins[j].id, "main") == 0 ||
                (strcmp(out->plugins[i].id, "main") != 0 && strcmp(out->plugins[i].id, out->plugins[j].id) > 0)) {
                aimt_plugin temporary = out->plugins[i];
                out->plugins[i] = out->plugins[j]; out->plugins[j] = temporary;
            }
    return out->count > 0;
}
