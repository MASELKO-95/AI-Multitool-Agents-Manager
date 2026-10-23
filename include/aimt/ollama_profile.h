#ifndef AIMT_OLLAMA_PROFILE_H
#define AIMT_OLLAMA_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    char base[256], name[128];
    unsigned context, output;
    float temperature;
} aimt_ollama_profile;
/* Only managed local aliases, bounded parameters, no shell or downloads. */
bool aimt_ollama_profile_json(const aimt_ollama_profile *profile, char *out, size_t size);
bool aimt_ollama_profile_create(const char *endpoint, const aimt_ollama_profile *profile,
    char *notice, size_t notice_size);
#ifdef __cplusplus
}
#endif
#endif
