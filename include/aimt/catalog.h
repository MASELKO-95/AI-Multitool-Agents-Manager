#ifndef AIMT_CATALOG_H
#define AIMT_CATALOG_H
#include "aimt/discovery.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AIMT_CATALOG_MAX 32
#define AIMT_TASK_COUNT 6
typedef enum { AIMT_TASK_CHAT, AIMT_TASK_CODE, AIMT_TASK_PLAN,
    AIMT_TASK_TRANSLATE, AIMT_TASK_VISION, AIMT_TASK_EMBED } aimt_task;
typedef struct {
    char id[128], source[256], license[64], quantization[32], note[384];
    unsigned weights_mib, reserve_mib;
    int priority[AIMT_TASK_COUNT];
} aimt_catalog_model;
typedef struct {
    char verified[32];
    size_t count;
    aimt_catalog_model models[AIMT_CATALOG_MAX];
} aimt_catalog;
typedef struct {
    size_t index;
    int score;
    bool installed, gpu_candidate, ram_candidate;
    unsigned estimated_mib;
} aimt_recommendation;
bool aimt_catalog_load(const char *path, aimt_catalog *out);
/* Heuristic shortlist; estimates are not runtime measurements or benchmarks. */
size_t aimt_catalog_recommend(const aimt_catalog *catalog, const aimt_discovery *local,
    aimt_task task, unsigned ram_mib, unsigned vram_mib, unsigned context_tokens,
    aimt_recommendation out[AIMT_CATALOG_MAX]);
aimt_task aimt_task_for_plugin(const char *id);
/* Installed Ollama only. Heuristic role fit, bounded memory estimate, no downloads.
 * SIZE_MAX means no eligible candidate. Unknown text models may be fallback. */
size_t aimt_model_select(const aimt_catalog *catalog, const aimt_discovery *local,
    aimt_task task, unsigned ram_mib, unsigned vram_mib, bool coordinator);
aimt_task aimt_task_from_text(const char *text, aimt_task fallback);
#ifdef __cplusplus
}
#endif
#endif
