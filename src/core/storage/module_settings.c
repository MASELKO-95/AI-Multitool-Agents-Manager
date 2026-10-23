#define _POSIX_C_SOURCE 200809L
#include "aimt/plugin.h"
#include "aimt/app.h"
#include <json-c/json.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool valid_id(const char *id)
{
    if (!id || !id[0] || !memchr(id, 0, 64)) return false;
    for (size_t i = 0; id[i]; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '_')) return false;
    return true;
}
static bool valid_text(const char *text, size_t size)
{
    const char *end = memchr(text, 0, size);
    return end && aimt_text_valid(text, (size_t)(end - text));
}
static int directory_open(const char *path, bool create)
{
    if (!path) return -1;
    if (create && mkdir(path, 0700) != 0 && errno != EEXIST) return -1;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (fd >= 0 && (fstat(fd, &st) != 0 || st.st_uid != geteuid() || (st.st_mode & 0077))) {
        close(fd); return -1;
    }
    return fd;
}
bool aimt_plugin_settings_save(const char *directory, const aimt_plugin *plugin)
{
    if (!plugin || !valid_id(plugin->id) || !aimt_plugin_validate(plugin) ||
        !plugin->title[0] || !valid_text(plugin->title, sizeof(plugin->title)) ||
        !valid_text(plugin->description, sizeof(plugin->description))) return false;
    struct json_object *root = json_object_new_object(), *values = json_object_new_object();
    if (!root || !values) { json_object_put(root); json_object_put(values); return false; }
    json_object_object_add(root, "schema_version", json_object_new_int(1));
    json_object_object_add(root, "id", json_object_new_string(plugin->id));
    json_object_object_add(root, "title", json_object_new_string(plugin->title));
    json_object_object_add(root, "description", json_object_new_string(plugin->description));
    json_object_object_add(root, "values", values);
    for (size_t i = 0; i < plugin->setting_count; ++i) {
        const aimt_setting *s = &plugin->settings[i];
        struct json_object *v = NULL;
        switch (s->type) {
            case AIMT_SETTING_TEXT_TYPE: v = json_object_new_string(s->text); break;
            case AIMT_SETTING_FLOAT: v = json_object_new_double(s->decimal); break;
            case AIMT_SETTING_BOOL: v = json_object_new_boolean(s->integer); break;
            case AIMT_SETTING_CHOICE: v = json_object_new_int(s->selected); break;
            case AIMT_SETTING_INT: v = json_object_new_int(s->integer); break;
            default: json_object_put(root); return false;
        }
        if (!v) { json_object_put(root); return false; }
        json_object_object_add(values, s->key, v);
    }
    const char *text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    size_t length = text ? strlen(text) : 0;
    int dir = length && length <= 32768 ? directory_open(directory, true) : -1;
    if (dir < 0) { json_object_put(root); return false; }
    char name[80], temporary[96];
    snprintf(name, sizeof(name), "%s.json", plugin->id);
    struct stat st;
    bool ok = true;
    if (fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) == 0)
        ok = S_ISREG(st.st_mode) && st.st_uid == geteuid();
    else if (errno != ENOENT) ok = false;
    int fd = -1;
    for (unsigned i = 0; ok && i < 32; ++i) {
        snprintf(temporary, sizeof(temporary), ".%s.%ld.%u.tmp", plugin->id, (long)getpid(), i);
        fd = openat(dir, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) ok = false;
    size_t written = 0;
    while (ok && written < length) {
        ssize_t n = write(fd, text + written, length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) ok = false; else written += (size_t)n;
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (fd >= 0 && close(fd) != 0) ok = false;
    if (ok && renameat(dir, temporary, dir, name) != 0) ok = false;
    if (ok && fsync(dir) != 0) ok = false;
    if (fd >= 0) unlinkat(dir, temporary, 0);
    close(dir); json_object_put(root);
    return ok;
}
static bool copy_string(struct json_object *root, const char *key, char *out, size_t capacity)
{
    struct json_object *v;
    if (!json_object_object_get_ex(root, key, &v) || !json_object_is_type(v, json_type_string)) return false;
    size_t n = (size_t)json_object_get_string_len(v);
    if (n >= capacity || !aimt_text_valid(json_object_get_string(v), n)) return false;
    memcpy(out, json_object_get_string(v), n + 1);
    return true;
}
bool aimt_plugin_settings_load(const char *directory, aimt_plugin *plugin)
{
    if (!plugin || !valid_id(plugin->id) || !aimt_plugin_validate(plugin)) return false;
    int dir = directory_open(directory, false);
    if (dir < 0) return false;
    char name[80]; snprintf(name, sizeof(name), "%s.json", plugin->id);
    int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    close(dir);
    if (fd < 0) return false;
    char text[32769]; size_t used = 0;
    struct stat st;
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
        !(st.st_mode & 0077) && st.st_size > 0 && st.st_size <= 32768;
    if (ok) {
        ssize_t n;
        while (used < sizeof(text) && (n = read(fd, text + used, sizeof(text) - used)) > 0) used += (size_t)n;
        ok = used < sizeof(text) && n == 0;
    }
    close(fd);
    if (!ok) return false;
    struct json_tokener *t = json_tokener_new_ex(12);
    if (!t) return false;
    json_tokener_set_flags(t, JSON_TOKENER_STRICT);
    struct json_object *root = json_tokener_parse_ex(t, text, (int)used), *v, *values;
    aimt_plugin draft = *plugin;
    char id[64];
    ok = json_tokener_get_error(t) == json_tokener_success && json_tokener_get_parse_end(t) == used &&
        json_object_is_type(root, json_type_object) &&
        json_object_object_get_ex(root, "schema_version", &v) && json_object_is_type(v, json_type_int) &&
        json_object_get_int64(v) == 1 && copy_string(root, "id", id, sizeof(id)) && !strcmp(id, plugin->id) &&
        copy_string(root, "title", draft.title, sizeof(draft.title)) && draft.title[0] &&
        copy_string(root, "description", draft.description, sizeof(draft.description)) &&
        json_object_object_get_ex(root, "values", &values) && json_object_is_type(values, json_type_object) &&
        (size_t)json_object_object_length(values) == draft.setting_count;
    for (size_t i = 0; ok && i < draft.setting_count; ++i) {
        aimt_setting *s = &draft.settings[i];
        ok = json_object_object_get_ex(values, s->key, &v);
        if (!ok) break;
        switch (s->type) {
            case AIMT_SETTING_TEXT_TYPE: ok = copy_string(values, s->key, s->text, sizeof(s->text)); break;
            case AIMT_SETTING_FLOAT:
                ok = json_object_is_type(v, json_type_int) || json_object_is_type(v, json_type_double);
                if (ok) s->decimal = (float)json_object_get_double(v);
                break;
            case AIMT_SETTING_BOOL:
                ok = json_object_is_type(v, json_type_boolean);
                if (ok) s->integer = json_object_get_boolean(v);
                break;
            case AIMT_SETTING_INT: case AIMT_SETTING_CHOICE: {
                int64_t n = json_object_get_int64(v);
                ok = json_object_is_type(v, json_type_int) && n >= -1000000 && n <= 1000000;
                if (ok) { if (s->type == AIMT_SETTING_CHOICE) s->selected = (int)n; else s->integer = (int)n; }
                break;
            }
            default: ok = false; break;
        }
    }
    if (ok) ok = aimt_plugin_validate(&draft);
    if (ok) *plugin = draft;
    json_object_put(root); json_tokener_free(t);
    return ok;
}
