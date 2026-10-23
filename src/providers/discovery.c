#define _POSIX_C_SOURCE 200809L
#include "aimt/discovery.h"
#include "aimt/app.h"

#include <curl/curl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

bool aimt_local_endpoint_valid(const char *endpoint)
{
    /* Literal loopback avoids DNS rebinding and LAN scans. */
    const char *port = NULL;
    if (endpoint == NULL) return false;
    if (strncmp(endpoint, "http://127.0.0.1:", 17) == 0) port = endpoint + 17;
    else if (strncmp(endpoint, "http://[::1]:", 13) == 0) port = endpoint + 13;
    else return false;
    size_t number = 0, digits = 0;
    while (*port) {
        if (*port < '0' || *port > '9' || digits == 5) return false;
        number = number * 10u + (size_t)(*port++ - '0'); digits++;
    }
    return digits > 0 && number > 0 && number <= 65535;
}

static bool copy_json_string(struct json_object *object, const char *key, char *out, size_t capacity)
{
    struct json_object *value;
    if (!json_object_object_get_ex(object, key, &value) || !json_object_is_type(value, json_type_string)) return false;
    size_t length = (size_t)json_object_get_string_len(value);
    const char *text = json_object_get_string(value);
    if (length == 0 || length >= capacity || !aimt_text_valid(text, length)) return false;
    memcpy(out, text, length + 1u);
    return true;
}
static bool parse_capabilities(struct json_object *root, aimt_discovered_model *model)
{
    struct json_object *array;
    if (!json_object_object_get_ex(root, "capabilities", &array) || !json_object_is_type(array, json_type_array) ||
        json_object_array_length(array) > 16) return false;
    unsigned flags = 0;
    for (size_t i = 0; i < json_object_array_length(array); ++i) {
        struct json_object *v = json_object_array_get_idx(array, i);
        if (!json_object_is_type(v, json_type_string)) return false;
        const char *s = json_object_get_string(v);
        if ((size_t)json_object_get_string_len(v) != strlen(s) || strlen(s) > 32) return false;
        if (!strcmp(s, "completion")) flags |= AIMT_MODEL_COMPLETION;
        if (!strcmp(s, "vision")) flags |= AIMT_MODEL_VISION;
        if (!strcmp(s, "embedding")) flags |= AIMT_MODEL_EMBEDDING;
        if (!strcmp(s, "tools")) flags |= AIMT_MODEL_TOOLS;
        /* Other adapters must register generation/STT/TTS/mesh capabilities;
         * Ollama vision is not image generation. */
    }
    model->capabilities = flags; model->capabilities_known = true;
    return true;
}
bool aimt_discovery_parse_capabilities(const char *body, size_t length, aimt_discovered_model *model)
{
    if (!body || !model || !length || length > AIMT_DISCOVERY_BODY_MAX) return false;
    struct json_tokener *t = json_tokener_new_ex(20);
    if (!t) return false;
    json_tokener_set_flags(t, JSON_TOKENER_STRICT);
    struct json_object *root = json_tokener_parse_ex(t, body, (int)length);
    bool ok = json_tokener_get_error(t) == json_tokener_success && json_tokener_get_parse_end(t) == length &&
        json_object_is_type(root, json_type_object) && parse_capabilities(root, model);
    json_object_put(root); json_tokener_free(t); return ok;
}
bool aimt_discovery_supports(const aimt_discovery *local, unsigned required)
{
    if (!local || local->count > AIMT_MODELS_MAX || !required || (required & ~255u)) return false;
    unsigned available = 0;
    for (size_t i = 0; i < local->count; ++i)
        if (local->models[i].capabilities_known) available |= local->models[i].capabilities;
    return (available & required) == required;
}

bool aimt_discovery_parse_models(const char *body, size_t length, const char *provider, aimt_discovery *out)
{
    struct json_object *root = NULL, *models = NULL;
    bool ok = false;
    if (body == NULL || provider == NULL || out == NULL || length == 0 ||
        length > AIMT_DISCOVERY_BODY_MAX || out->count > AIMT_MODELS_MAX) return false;
    bool ollama = strcmp(provider, "Ollama") == 0;
    if (!ollama && strcmp(provider, "llama.cpp") != 0) return false;
    struct json_tokener *tokener = json_tokener_new_ex(20);
    if (tokener == NULL) return false;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    root = json_tokener_parse_ex(tokener, body, (int)length);
    if (json_tokener_get_error(tokener) != json_tokener_success || !json_object_is_type(root, json_type_object) ||
        !json_object_object_get_ex(root, ollama ? "models" : "data", &models) ||
        !json_object_is_type(models, json_type_array)) goto done;
    for (size_t i = 0; i < json_object_array_length(models); ++i) {
        struct json_object *object = json_object_array_get_idx(models, i), *details, *size;
        aimt_discovered_model model = {0};
        if (!json_object_is_type(object, json_type_object) ||
            !copy_json_string(object, ollama ? "name" : "id", model.id, sizeof(model.id))) {
            out->omitted++; continue;
        }
        snprintf(model.provider, sizeof(model.provider), "%s", provider);
        if (ollama) parse_capabilities(object, &model);
        if (ollama && json_object_object_get_ex(object, "details", &details) && json_object_is_type(details, json_type_object)) {
            copy_json_string(details, "quantization_level", model.quantization, sizeof(model.quantization));
            copy_json_string(details, "parameter_size", model.parameters, sizeof(model.parameters));
        }
        if (ollama && json_object_object_get_ex(object, "size", &size) &&
            json_object_is_type(size, json_type_int) && json_object_get_int64(size) >= 0)
            model.disk_bytes = (uint64_t)json_object_get_int64(size);
        bool duplicate = false;
        for (size_t j = 0; j < out->count; ++j)
            if (strcmp(out->models[j].id, model.id) == 0 && strcmp(out->models[j].provider, provider) == 0) duplicate = true;
        if (duplicate) continue;
        if (out->count == AIMT_MODELS_MAX) { out->omitted++; continue; }
        out->models[out->count++] = model;
    }
    ok = true;
done:
    if (root != NULL) json_object_put(root);
    json_tokener_free(tokener);
    return ok;
}

typedef struct { char *data; size_t used; bool exceeded; } response_buffer;

static size_t receive(void *data, size_t size, size_t count, void *userdata)
{
    response_buffer *buffer = userdata;
    if (size != 0 && count > SIZE_MAX / size) return 0;
    size_t length = size * count;
    if (length > AIMT_DISCOVERY_BODY_MAX - buffer->used) { buffer->exceeded = true; return 0; }
    memcpy(buffer->data + buffer->used, data, length);
    buffer->used += length;
    buffer->data[buffer->used] = '\0';
    return length;
}

static bool query(const char *endpoint, const char *route, const char *provider, aimt_discovery *out)
{
    if (!aimt_local_endpoint_valid(endpoint)) return false;
    char url[320];
    if (snprintf(url, sizeof(url), "%s%s", endpoint, route) >= (int)sizeof(url)) return false;
    response_buffer buffer = {calloc(1, AIMT_DISCOVERY_BODY_MAX + 1u), 0, false};
    if (buffer.data == NULL) return false;
    CURL *curl = curl_easy_init();
    bool ok = false;
    if (curl != NULL) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 700L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2500L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        CURLcode result = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        ok = result == CURLE_OK && status == 200 && !buffer.exceeded &&
             aimt_discovery_parse_models(buffer.data, buffer.used, provider, out);
        curl_easy_cleanup(curl);
    }
    free(buffer.data);
    return ok;
}

static void find_binary(const char *name, char *out, size_t capacity)
{
    const char *path = getenv("PATH");
    if (path == NULL) return;
    /* Bounded enumeration; never invokes a shell or the detected executable. */
    size_t checked = 0;
    while (*path && checked++ < 128) {
        const char *end = strchr(path, ':');
        size_t n = end != NULL ? (size_t)(end - path) : strlen(path);
        if (n > 0 && n + strlen(name) + 2u < capacity) {
            char candidate[1024];
            if (n + strlen(name) + 2u < sizeof(candidate)) {
                memcpy(candidate, path, n); candidate[n] = '/';
                strcpy(candidate + n + 1u, name);
                struct stat st;
                if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode) && access(candidate, X_OK) == 0) {
                    snprintf(out, capacity, "%s", candidate); return;
                }
            }
        }
        if (end == NULL) break;
        path = end + 1;
    }
}
static void query_capabilities(const char *endpoint, aimt_discovered_model *model)
{
    char url[320];
    if (!aimt_local_endpoint_valid(endpoint) || snprintf(url, sizeof(url), "%s/api/show", endpoint) >= (int)sizeof(url)) return;
    struct json_object *body = json_object_new_object();
    if (!body) return;
    json_object_object_add(body, "model", json_object_new_string(model->id));
    response_buffer buffer = {calloc(1, AIMT_DISCOVERY_BODY_MAX + 1u), 0, false};
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
    if (buffer.data && curl && headers) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_PROXY, ""); curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 300L); curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 700L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(body));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive); curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        CURLcode code = curl_easy_perform(curl); long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (code == CURLE_OK && status == 200 && !buffer.exceeded)
            aimt_discovery_parse_capabilities(buffer.data, buffer.used, model);
    }
    curl_easy_cleanup(curl); curl_slist_free_all(headers); free(buffer.data); json_object_put(body);
}

void aimt_discover(const char *ollama_endpoint, const char *llama_endpoint, aimt_discovery *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    find_binary("ollama", out->ollama_binary, sizeof(out->ollama_binary));
    find_binary("llama-server", out->llama_binary, sizeof(out->llama_binary));
    find_binary("codex", out->codex_binary, sizeof(out->codex_binary));
    if (ollama_endpoint != NULL) snprintf(out->endpoint, sizeof(out->endpoint), "%s", ollama_endpoint);
    if (llama_endpoint != NULL) snprintf(out->llama_endpoint, sizeof(out->llama_endpoint), "%s", llama_endpoint);
    out->ollama_online = query(out->endpoint, "/api/tags", "Ollama", out);
    out->llama_online = query(out->llama_endpoint, "/v1/models", "llama.cpp", out);
    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC, &start) == 0) for (size_t i = 0; i < out->count; ++i) {
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
            (double)(now.tv_sec - start.tv_sec) + (double)(now.tv_nsec - start.tv_nsec) / 1e9 > 4.0) break;
        if (!out->models[i].capabilities_known && !strcmp(out->models[i].provider, "Ollama"))
            query_capabilities(out->endpoint, &out->models[i]);
    }
    snprintf(out->status, sizeof(out->status), "Ollama: %s  |  llama.cpp: %s  |  models: %zu",
             out->ollama_online ? "connected" : "unavailable", out->llama_online ? "connected" : "unavailable", out->count);
}
