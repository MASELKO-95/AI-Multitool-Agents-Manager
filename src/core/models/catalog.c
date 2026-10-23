#define _POSIX_C_SOURCE 200809L
#include "aimt/catalog.h"
#include "aimt/app.h"
#include <json-c/json.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>

aimt_task aimt_task_from_text(const char *text, aimt_task fallback)
{
    if (!text || strlen(text)>8192) return fallback;
    char lower[8193]; size_t n=strlen(text);
    for(size_t i=0;i<=n;i++)lower[i]=(char)tolower((unsigned char)text[i]);
    if(strstr(lower,"translat"))return AIMT_TASK_TRANSLATE;
    if(strstr(lower,"debug") || strstr(lower,"code") || strstr(lower,"compil") || strstr(lower,"repair") || strstr(lower,"fix") || strstr(lower,"implement") || strstr(lower,"test"))return AIMT_TASK_CODE;
    if(strstr(lower,"plan") || strstr(lower,"coordinate") || strstr(lower,"decompose"))return AIMT_TASK_PLAN;
    return fallback;
}

size_t aimt_model_select(const aimt_catalog *catalog,const aimt_discovery *local,
    aimt_task task,unsigned ram,unsigned vram,bool coordinator)
{
    if(!local || local->count>AIMT_MODELS_MAX || !local->ollama_online ||
       (catalog && catalog->count>AIMT_CATALOG_MAX) || (unsigned)task>=AIMT_TASK_COUNT ||
       ram>131072 || vram>131072)return SIZE_MAX;
    unsigned required=task==AIMT_TASK_EMBED?AIMT_MODEL_EMBEDDING:AIMT_MODEL_COMPLETION;
    if(task==AIMT_TASK_VISION)required|=AIMT_MODEL_VISION;
    size_t best=SIZE_MAX;int best_score=INT_MIN;
    for(size_t i=0;i<local->count;i++) {
        const aimt_discovered_model *m=&local->models[i];
        size_t n=strnlen(m->id,sizeof(m->id));
        if(!n || n==sizeof(m->id) || strcmp(m->provider,"Ollama"))continue;
        bool safe=true;
        char id[256];
        for(size_t k=0;k<=n;k++)id[k]=(char)tolower((unsigned char)m->id[k]);
        for(size_t k=0;k<n;k++)if(!isalnum((unsigned char)m->id[k]) && !strchr("_-.:/",m->id[k]))safe=false;
        if(!safe || strstr(id,"cloud") || (m->capabilities_known && (m->capabilities&required)!=required))continue;
        if(!m->capabilities_known && (task==AIMT_TASK_EMBED || task==AIMT_TASK_VISION || strstr(id,"embed")))continue;
        uint64_t weights=m->disk_bytes/1048576u+(m->disk_bytes%1048576u!=0);
        uint64_t estimate=weights+1024u;
        if(!m->disk_bytes || estimate>ram)continue;
        int fit=30;
        if(catalog)for(size_t k=0;k<catalog->count;k++)if(!strcmp(m->id,catalog->models[k].id))fit=catalog->models[k].priority[task];
        /* Profile names are hints for ranking, never proof of capabilities. */
        if(task==AIMT_TASK_CODE && (strstr(id,"coder") || strstr(id,"coding") || strstr(id,"roocode")))fit=95;
        if(task==AIMT_TASK_TRANSLATE && (strstr(id,"translation") || strstr(id,"translategemma")))fit=95;
        if(task==AIMT_TASK_CHAT && (strstr(id,"aimt_main:") || strstr(id,"suzume")))fit=85;
        if(task==AIMT_TASK_VISION && (strstr(id,"qwen3-vl") || strstr(id,"aimt_video:")))fit=100;
        if(coordinator && (strstr(id,"translat") || strstr(id,"suzume")))fit=10;
        int score=fit*10+(estimate<=vram?200:0)+(m->capabilities_known?20:0);
        if(coordinator && m->capabilities_known && (m->capabilities&AIMT_MODEL_TOOLS))score+=600;
        if(best==SIZE_MAX || score>best_score || (score==best_score && strcmp(m->id,local->models[best].id)<0)) {best=i;best_score=score;}
    }
    return best;
}

static bool string_field(struct json_object *root, const char *key, char *out, size_t capacity)
{
    struct json_object *v;
    if (!json_object_object_get_ex(root, key, &v) || !json_object_is_type(v, json_type_string)) return false;
    size_t n = (size_t)json_object_get_string_len(v);
    if (!n || n >= capacity || !aimt_text_valid(json_object_get_string(v), n)) return false;
    memcpy(out, json_object_get_string(v), n + 1);
    return true;
}
static bool number_field(struct json_object *root, const char *key, unsigned *out)
{
    struct json_object *v;
    if (!json_object_object_get_ex(root, key, &v) || !json_object_is_type(v, json_type_int)) return false;
    int64_t n = json_object_get_int64(v);
    if (n < 1 || n > 262144) return false;
    *out = (unsigned)n;
    return true;
}
static bool safe_model_id(const char *s)
{
    if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= '0' && s[0] <= '9'))) return false;
    for (size_t i = 0; s[i]; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
            (s[i] >= '0' && s[i] <= '9') || strchr("_-.:/", s[i]))) return false;
    return !strstr(s, "-cloud") && !strstr(s, ":cloud");
}
bool aimt_catalog_load(const char *path, aimt_catalog *out)
{
    if (!path || !out) return false;
    memset(out, 0, sizeof(*out));
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat st;
    char data[65537];
    size_t used = 0;
    bool ok = false;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= 65536) {
        ssize_t n;
        while (used < sizeof(data) && (n = read(fd, data + used, sizeof(data) - used)) > 0) used += (size_t)n;
        ok = used < sizeof(data) && n == 0;
    }
    close(fd);
    if (!ok) return false;
    struct json_tokener *t = json_tokener_new_ex(12);
    if (!t) return false;
    json_tokener_set_flags(t, JSON_TOKENER_STRICT);
    struct json_object *root = json_tokener_parse_ex(t, data, (int)used), *array, *version;
    ok = json_tokener_get_error(t) == json_tokener_success && json_tokener_get_parse_end(t) == used &&
        json_object_is_type(root, json_type_object) &&
        json_object_object_get_ex(root, "schema_version", &version) &&
        json_object_is_type(version, json_type_int) && json_object_get_int64(version) == 1 &&
        string_field(root, "verified", out->verified, sizeof(out->verified)) &&
        json_object_object_get_ex(root, "models", &array) && json_object_is_type(array, json_type_array);
    if (ok) {
        out->count = json_object_array_length(array);
        ok = out->count > 0 && out->count <= AIMT_CATALOG_MAX;
    }
    for (size_t i = 0; ok && i < out->count; ++i) {
        struct json_object *entry = json_object_array_get_idx(array, i), *priorities;
        aimt_catalog_model *m = &out->models[i];
        ok = string_field(entry, "id", m->id, sizeof(m->id)) && safe_model_id(m->id) &&
            string_field(entry, "source", m->source, sizeof(m->source)) &&
            strncmp(m->source, "https://", 8) == 0 &&
            string_field(entry, "license", m->license, sizeof(m->license)) &&
            string_field(entry, "quantization", m->quantization, sizeof(m->quantization)) &&
            string_field(entry, "note", m->note, sizeof(m->note)) &&
            number_field(entry, "weights_mib", &m->weights_mib) &&
            number_field(entry, "reserve_mib", &m->reserve_mib) &&
            json_object_object_get_ex(entry, "priority", &priorities) &&
            json_object_is_type(priorities, json_type_array) && json_object_array_length(priorities) == AIMT_TASK_COUNT;
        for (size_t j = 0; ok && j < AIMT_TASK_COUNT; ++j) {
            struct json_object *v = json_object_array_get_idx(priorities, j);
            int64_t p = json_object_get_int64(v);
            ok = json_object_is_type(v, json_type_int) && p >= 0 && p <= 100;
            m->priority[j] = (int)p;
        }
        for (size_t j = 0; ok && j < i; ++j) if (!strcmp(m->id, out->models[j].id)) ok = false;
    }
    json_object_put(root); json_tokener_free(t);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}
aimt_task aimt_task_for_plugin(const char *id)
{
    if (id && !strcmp(id, "video")) return AIMT_TASK_VISION;
    if (id && (!strcmp(id, "mesh") || !strcmp(id, "images"))) return AIMT_TASK_PLAN;
    if (id && !strcmp(id, "coding")) return AIMT_TASK_CODE;
    if (id && !strcmp(id, "translation")) return AIMT_TASK_TRANSLATE;
    if (id && !strcmp(id, "specification")) return AIMT_TASK_PLAN;
    return AIMT_TASK_CHAT;
}
size_t aimt_catalog_recommend(const aimt_catalog *catalog, const aimt_discovery *local,
    aimt_task task, unsigned ram, unsigned vram, unsigned context,
    aimt_recommendation out[AIMT_CATALOG_MAX])
{
    if (!catalog || !out || catalog->count > AIMT_CATALOG_MAX ||
        (unsigned)task >= AIMT_TASK_COUNT || context > 131072 || ram > 131072 || vram > 131072 ||
        (local && local->count > AIMT_MODELS_MAX)) return 0;
    size_t count = 0;
    for (size_t i = 0; i < catalog->count; ++i) {
        const aimt_catalog_model *m = &catalog->models[i];
        if (!m->priority[task]) continue;
        uint64_t weights = m->weights_mib;
        bool installed = false;
        if (local) for (size_t j = 0; j < local->count; ++j) {
            if (!strcmp(m->id, local->models[j].id) && !strcmp(local->models[j].provider, "Ollama")) {
                installed = true;
                if (local->models[j].disk_bytes) {
                    uint64_t bytes = local->models[j].disk_bytes;
                    weights = bytes / 1048576u + (bytes % 1048576u != 0);
                }
                break;
            }
        }
        /* Reserve scaled with requested context; conservative generic heuristic,
         * not architecture-specific KV sizing. Images may need more. */
        uint64_t estimate = weights + m->reserve_mib + (uint64_t)m->reserve_mib * context / 4096u;
        if (estimate > 524288) estimate = 524288;
        bool gpu = estimate <= vram, cpu = estimate <= ram;
        int score = m->priority[task] + (gpu ? 100 : cpu ? 0 : -200) + (installed ? 15 : 0);
        out[count++] = (aimt_recommendation){i, score, installed, gpu, cpu, (unsigned)estimate};
    }
    for (size_t i = 0; i < count; ++i) for (size_t j = i + 1; j < count; ++j)
        if (out[j].score > out[i].score) { aimt_recommendation tmp = out[i]; out[i] = out[j]; out[j] = tmp; }
    return count;
}
