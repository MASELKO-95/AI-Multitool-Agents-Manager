#include "aimt/ollama_pull.h"
#include "aimt/discovery.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>

bool aimt_ollama_pull_json(const char *model, char *out, size_t size)
{
    if (!model || !out || !size) return false;
    size_t length = 0;
    while (length < 128 && model[length]) ++length;
    if (!length || length == 128 || !((model[0] >= 'a' && model[0] <= 'z') || (model[0] >= '0' && model[0] <= '9')) ||
        strstr(model, "-cloud") || strstr(model, ":cloud")) return false;
    for (size_t i = 0; i < length; ++i)
        if (!((model[i] >= 'a' && model[i] <= 'z') || (model[i] >= 'A' && model[i] <= 'Z') ||
            (model[i] >= '0' && model[i] <= '9') || strchr("_-.:/", model[i]))) return false;
    struct json_object *root = json_object_new_object(), *name = json_object_new_string(model), *stream = json_object_new_boolean(true);
    if (!root || !name || !stream) { json_object_put(root); json_object_put(name); json_object_put(stream); return false; }
    json_object_object_add(root, "model", name); json_object_object_add(root, "stream", stream);
    const char *text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    bool ok = text && strlen(text) < size;
    if (ok) memcpy(out, text, strlen(text) + 1);
    json_object_put(root); return ok;
}
typedef struct {
    char line[8192];
    size_t used;
    bool success, failed;
    bool (*cancelled)(void *);
    void (*progress)(void *, int);
    void *userdata;
} pull_state;
static bool parse_line(pull_state *s)
{
    if (!s->used) return true;
    struct json_tokener *t = json_tokener_new_ex(8);
    if (!t) return false;
    json_tokener_set_flags(t, JSON_TOKENER_STRICT);
    struct json_object *root = json_tokener_parse_ex(t, s->line, (int)s->used), *status, *total, *completed, *error;
    bool ok = json_tokener_get_error(t) == json_tokener_success && json_tokener_get_parse_end(t) == s->used &&
        json_object_is_type(root, json_type_object);
    if (ok && json_object_object_get_ex(root, "error", &error)) ok = false;
    if (ok && json_object_object_get_ex(root, "status", &status) && json_object_is_type(status, json_type_string) &&
        json_object_get_string_len(status) == 7 && !strcmp(json_object_get_string(status), "success")) s->success = true;
    if (ok && json_object_object_get_ex(root, "total", &total) && json_object_object_get_ex(root, "completed", &completed)) {
        int64_t a = json_object_get_int64(total), b = json_object_get_int64(completed);
        ok = json_object_is_type(total, json_type_int) && json_object_is_type(completed, json_type_int) && a > 0 && b >= 0 && b <= a;
        if (ok && s->progress) s->progress(s->userdata, (int)((double)b / (double)a * 100.0));
    }
    json_object_put(root); json_tokener_free(t); s->used = 0;
    if (!ok) s->failed = true;
    return ok;
}
static size_t receive(void *data, size_t size, size_t count, void *userdata)
{
    pull_state *s = userdata;
    if (size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    const char *bytes = data;
    for (size_t i = 0; i < n; ++i) {
        if (bytes[i] == '\n') { if (!parse_line(s)) return 0; }
        else {
            if (s->used == sizeof(s->line) || bytes[i] == 0) { s->failed = true; return 0; }
            s->line[s->used++] = bytes[i];
        }
    }
    return n;
}
static int transfer(void *userdata, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    pull_state *s = userdata;
    (void)a; (void)b; (void)c; (void)d;
    return s->cancelled && s->cancelled(s->userdata);
}
bool aimt_ollama_pull(const char *endpoint, const char *model, bool (*cancelled)(void *),
    void (*progress)(void *, int), void *userdata, char *notice, size_t notice_size)
{
    char body[256], url[320];
    if (!notice || !notice_size) return false;
    snprintf(notice, notice_size, "Model not downloaded: Ollama, connection or response data error.");
    if (!aimt_local_endpoint_valid(endpoint) || !aimt_ollama_pull_json(model, body, sizeof(body)) ||
        snprintf(url, sizeof(url), "%s/api/pull", endpoint) >= (int)sizeof(url)) return false;
    pull_state s = {.cancelled=cancelled, .progress=progress, .userdata=userdata};
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
    bool ok = false;
    if (curl && headers) {
        curl_easy_setopt(curl, CURLOPT_URL, url); curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 700L); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1800L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s); curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer); curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &s);
        CURLcode code = curl_easy_perform(curl); long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (s.used && !parse_line(&s)) s.failed = true;
        ok = code == CURLE_OK && status == 200 && !s.failed && s.success;
    }
    curl_easy_cleanup(curl); curl_slist_free_all(headers);
    if (cancelled && cancelled(userdata)) snprintf(notice, notice_size, "Download request interrupted. Ollama controls server-side download state.");
    else if (ok) snprintf(notice, notice_size, "Downloaded %s. Models refreshed; select a model for the module.", model);
    return ok;
}
