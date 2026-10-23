#define _POSIX_C_SOURCE 200809L
#include "aimt/ollama_profile.h"
#include "aimt/discovery.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool model_name(const char *s, size_t capacity)
{
    if (!s[0] || !memchr(s, 0, capacity) || strstr(s, "-cloud") || strstr(s, ":cloud")) return false;
    for (size_t i = 0; s[i]; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
            (s[i] >= '0' && s[i] <= '9') || strchr("_-.:/", s[i]))) return false;
    return true;
}
static bool managed_name(const char *s)
{
    if (strncmp(s, "aimt_", 5) || !s[5]) return false;
    bool tag = false;
    for (size_t i = 5; s[i]; ++i) {
        if (s[i] == ':') {
            if (tag || i == 5 || !s[i + 1]) return false;
            tag = true;
        } else if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') ||
            s[i] == '_' || (tag && s[i] == '-'))) return false;
    }
    return tag;
}
bool aimt_ollama_profile_json(const aimt_ollama_profile *p, char *out, size_t size)
{
    if (!p || !out || !size || !model_name(p->base, sizeof(p->base)) ||
        !model_name(p->name, sizeof(p->name)) || !managed_name(p->name) ||
        !strcmp(p->base, p->name) || p->context < 512 || p->context > 131072 ||
        p->output < 64 || p->output > 16384 || p->output >= p->context ||
        !isfinite(p->temperature) || p->temperature < 0 || p->temperature > 2) return false;
    struct json_object *root = json_object_new_object(), *parameters = json_object_new_object();
    if (!root || !parameters) { json_object_put(root); json_object_put(parameters); return false; }
    json_object_object_add(root, "from", json_object_new_string(p->base));
    json_object_object_add(root, "model", json_object_new_string(p->name));
    json_object_object_add(root, "stream", json_object_new_boolean(false));
    json_object_object_add(root, "parameters", parameters);
    json_object_object_add(parameters, "num_ctx", json_object_new_int((int)p->context));
    json_object_object_add(parameters, "num_predict", json_object_new_int((int)p->output));
    json_object_object_add(parameters, "temperature", json_object_new_double(p->temperature));
    const char *json = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    bool ok = json && strlen(json) < size;
    if (ok) memcpy(out, json, strlen(json) + 1);
    json_object_put(root);
    return ok;
}
#define RESPONSE_MAX 262144u
typedef struct { char *text; size_t used; } response;
static size_t receive(void *data, size_t size, size_t count, void *userdata)
{
    response *r = userdata;
    if (size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    if (n > RESPONSE_MAX - r->used) return 0;
    memcpy(r->text + r->used, data, n); r->used += n; r->text[r->used] = 0;
    return n;
}
static struct json_object *request(const char *endpoint, const char *route, const char *body)
{
    char url[320];
    if (!aimt_local_endpoint_valid(endpoint) || snprintf(url, sizeof(url), "%s%s", endpoint, route) >= (int)sizeof(url)) return NULL;
    response r = {calloc(1, RESPONSE_MAX + 1), 0};
    if (!r.text) return NULL;
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
    struct json_object *root = NULL;
    if (curl && headers) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 700L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        if (body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r);
        CURLcode code = curl_easy_perform(curl);
        long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (code == CURLE_OK && status == 200) {
            struct json_tokener *t = json_tokener_new_ex(20);
            if (t) {
                json_tokener_set_flags(t, JSON_TOKENER_STRICT);
                root = json_tokener_parse_ex(t, r.text, (int)r.used);
                if (json_tokener_get_error(t) != json_tokener_success ||
                    json_tokener_get_parse_end(t) != r.used || !json_object_is_type(root, json_type_object)) {
                    json_object_put(root); root = NULL;
                }
                json_tokener_free(t);
            }
        }
    }
    curl_easy_cleanup(curl); curl_slist_free_all(headers); free(r.text);
    return root;
}
bool aimt_ollama_profile_create(const char *endpoint, const aimt_ollama_profile *p, char *notice, size_t notice_size)
{
    char body[1024];
    if (!notice || !notice_size) return false;
    snprintf(notice, notice_size, "Profile not created: check the local model and parameters.");
    if (!aimt_local_endpoint_valid(endpoint) || !aimt_ollama_profile_json(p, body, sizeof(body))) return false;
    /* Fresh tags prevent creating from an absent or cloud-only model. */
    struct json_object *tags = request(endpoint, "/api/tags", NULL), *models;
    bool present = false;
    if (tags && json_object_object_get_ex(tags, "models", &models) && json_object_is_type(models, json_type_array)) {
        for (size_t i = 0; i < json_object_array_length(models); ++i) {
            struct json_object *m = json_object_array_get_idx(models, i), *name, *bytes;
            if (json_object_object_get_ex(m, "name", &name) && json_object_is_type(name, json_type_string) &&
                (size_t)json_object_get_string_len(name) == strlen(p->base) && !strcmp(json_object_get_string(name), p->base) &&
                json_object_object_get_ex(m, "size", &bytes) && json_object_is_type(bytes, json_type_int) &&
                json_object_get_int64(bytes) > 0) present = true;
        }
    }
    json_object_put(tags);
    if (!present) { snprintf(notice, notice_size, "The base model is not available locally. Nothing was downloaded."); return false; }
    /* Validate declared native context; do not assume a tag supports 128k. */
    struct json_object *show_request = json_object_new_object();
    if (!show_request) return false;
    json_object_object_add(show_request, "model", json_object_new_string(p->base));
    struct json_object *show = request(endpoint, "/api/show", json_object_to_json_string(show_request)), *info;
    json_object_put(show_request);
    uint64_t native_context = 0;
    if (show && json_object_object_get_ex(show, "model_info", &info) && json_object_is_type(info, json_type_object)) {
        json_object_object_foreach(info, key, val) {
            size_t n = strlen(key);
            if (n >= 15 && !strcmp(key + n - 15, ".context_length") && json_object_is_type(val, json_type_int)) {
                int64_t v = json_object_get_int64(val);
                if (v > 0 && (!native_context || (uint64_t)v < native_context)) native_context = (uint64_t)v;
            }
        }
    }
    json_object_put(show);
    if (!native_context || p->context > native_context) {
        snprintf(notice, notice_size, "Context exceeds the declared model limit, or the limit is unknown."); return false;
    }
    struct json_object *result = request(endpoint, "/api/create", body), *status;
    bool ok = result && json_object_object_get_ex(result, "status", &status) && json_object_is_type(status, json_type_string) &&
        !strcmp(json_object_get_string(status), "success");
    json_object_put(result);
    if (ok) snprintf(notice, notice_size, "Created %s: context %u, output %u. Original retained.", p->name, p->context, p->output);
    return ok;
}
