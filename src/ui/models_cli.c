#include "aimt/catalog.h"
#include "aimt/discovery.h"
#include "aimt/ollama_profile.h"
#include "aimt/plugin.h"
#include "aimt/ollama_pull.h"
#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool number(const char *text, unsigned *out)
{
    if (!text || text[0] < '0' || text[0] > '9') return false;
    char *end; errno = 0; unsigned long n = strtoul(text, &end, 10);
    if (errno || *end || n > 131072) return false;
    *out = (unsigned)n; return true;
}
static void help(void)
{
    puts("Local models / terminal without SDL or GUI");
    puts("ai-multitool-agents-manager-models list");
    puts("ai-multitool-agents-manager-models pull MODEL (download weights through the local Ollama server)");
    puts("ai-multitool-agents-manager-models recommend chat|code|plan|translate|vision|embed RAM_MiB VRAM_MiB CONTEXT");
    puts("ai-multitool-agents-manager-models profile BASE_MODEL aimt_name:tag CONTEXT OUTPUT TEMPERATURE");
    puts("ai-multitool-agents-manager-models module-set MODULE_ID KEY VALUE [SETTINGS_DIRECTORY]");
    puts("Address: OLLAMA_HOST=http://127.0.0.1:11434 (loopback only)");
    puts("Profiles set num_ctx, num_predict and temperature. They do not download models or measure stability.");
}
int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "--help")) { help(); return 0; }
    const char *endpoint = getenv("OLLAMA_HOST");
    if (!endpoint) endpoint = "http://127.0.0.1:11434";
    if (!aimt_local_endpoint_valid(endpoint)) { fputs("Disallowed service address.\n", stderr); return 2; }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
    int result = 0;
    if (!strcmp(argv[1], "pull") && argc == 3) {
        char notice[512];
        bool ok = aimt_ollama_pull(endpoint, argv[2], NULL, NULL, NULL, notice, sizeof(notice));
        puts(notice); result = ok ? 0 : 1;
    } else if (!strcmp(argv[1], "module-set") && (argc == 5 || argc == 6)) {
        aimt_plugin_registry *registry = calloc(1, sizeof(*registry));
        if (!registry) { result = 1; goto done; }
        const char *settings = argc == 6 ? argv[5] : AIMT_DEFAULT_SETTINGS;
        bool ok = false;
        if (aimt_plugins_load(AIMT_DEFAULT_PLUGINS, registry)) for (size_t i = 0; i < registry->count; ++i) {
            aimt_plugin *plugin = &registry->plugins[i];
            if (strcmp(plugin->id, argv[2])) continue;
            aimt_plugin_settings_load(settings, plugin);
            aimt_setting *s = aimt_plugin_setting(plugin, argv[3]);
            if (!s) break;
            char *end;
            if (s->type == AIMT_SETTING_TEXT_TYPE) {
                if (strlen(argv[4]) >= sizeof(s->text)) break;
                snprintf(s->text, sizeof(s->text), "%s", argv[4]);
            } else if (s->type == AIMT_SETTING_FLOAT) {
                errno = 0; s->decimal = strtof(argv[4], &end);
                if (errno || *end || end == argv[4]) break;
            } else {
                errno = 0; long n = strtol(argv[4], &end, 10);
                if (errno || *end || end == argv[4] || n < -1000000 || n > 1000000) break;
                if (s->type == AIMT_SETTING_CHOICE) s->selected = (int)n; else s->integer = (int)n;
            }
            ok = aimt_plugin_settings_save(settings, plugin);
            break;
        }
        puts(ok ? "Module setting saved." : "Not saved: unknown module/field, invalid value or directory.");
        free(registry); result = ok ? 0 : 1;
    } else if (!strcmp(argv[1], "profile") && argc == 7) {
        aimt_ollama_profile p = {0};
        if (strlen(argv[2]) >= sizeof(p.base) || strlen(argv[3]) >= sizeof(p.name) ||
            !number(argv[4], &p.context) || !number(argv[5], &p.output)) { result = 2; goto done; }
        snprintf(p.base, sizeof(p.base), "%s", argv[2]); snprintf(p.name, sizeof(p.name), "%s", argv[3]);
        char *end; errno = 0; p.temperature = strtof(argv[6], &end);
        if (errno || *end || end == argv[6]) { result = 2; goto done; }
        char notice[512];
        bool ok = aimt_ollama_profile_create(endpoint, &p, notice, sizeof(notice));
        puts(notice); result = ok ? 0 : 1;
    } else if ((!strcmp(argv[1], "list") && argc == 2) || (!strcmp(argv[1], "recommend") && argc == 6)) {
        aimt_task task = AIMT_TASK_CHAT;
        unsigned ram = 0, vram = 0, context = 0;
        if (!strcmp(argv[1], "recommend")) {
            const char *tasks[] = {"chat", "code", "plan", "translate", "vision", "embed"};
            bool found = false;
            for (unsigned i = 0; i < AIMT_TASK_COUNT; ++i) if (!strcmp(argv[2], tasks[i])) { task = (aimt_task)i; found = true; }
            if (!found || !number(argv[3], &ram) || !number(argv[4], &vram) || !number(argv[5], &context) || context < 512 || ram < 256) {
                result = 2; goto done;
            }
        }
        aimt_discovery *local = calloc(1, sizeof(*local));
        if (!local) { result = 1; goto done; }
        aimt_discover(endpoint, "http://127.0.0.1:8080", local);
        if (!strcmp(argv[1], "list")) {
            puts(local->status);
            for (size_t i = 0; i < local->count; ++i)
                printf("%s | %s | %s | %.2f GiB file | completion=%d vision=%d embedding=%d confirmed=%d\n",
                    local->models[i].provider, local->models[i].id, local->models[i].quantization,
                    (double)local->models[i].disk_bytes / 1073741824.0,
                    (local->models[i].capabilities & AIMT_MODEL_COMPLETION) != 0,
                    (local->models[i].capabilities & AIMT_MODEL_VISION) != 0,
                    (local->models[i].capabilities & AIMT_MODEL_EMBEDDING) != 0,
                    local->models[i].capabilities_known);
        } else {
            aimt_catalog catalog;
            if (!aimt_catalog_load(AIMT_DEFAULT_CATALOG, &catalog)) { free(local); result = 1; goto done; }
            aimt_recommendation picks[AIMT_CATALOG_MAX];
            size_t n = aimt_catalog_recommend(&catalog, local, task, ram, vram, context, picks);
            printf("Sources: %s. Estimates require measurement; nothing was started.\n", catalog.verified);
            for (size_t i = 0; i < n; ++i) {
                aimt_catalog_model *m = &catalog.models[picks[i].index];
                printf("%s | %s | %s | reserve ~%.1f GiB\n  %s\n  %s\n", m->id,
                    picks[i].installed ? "local" : "available to download",
                    picks[i].gpu_candidate ? "GPU candidate" : picks[i].ram_candidate ? "CPU/offload" : "over budget",
                    (double)picks[i].estimated_mib / 1024, m->note, m->source);
            }
        }
        free(local);
    } else { help(); result = 2; }
done:
    curl_global_cleanup();
    return result;
}
