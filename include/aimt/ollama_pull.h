#ifndef AIMT_OLLAMA_PULL_H
#define AIMT_OLLAMA_PULL_H
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
bool aimt_ollama_pull_json(const char *model, char *out, size_t size);
/* Only explicit calls download. Progress concerns the current layer; cancellation
 * closes this request, remote server cleanup is owned by Ollama. */
bool aimt_ollama_pull(const char *endpoint, const char *model,
    bool (*cancelled)(void *), void (*progress)(void *, int), void *userdata,
    char *notice, size_t notice_size);
#ifdef __cplusplus
}
#endif
#endif
