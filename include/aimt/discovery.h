#ifndef AIMT_DISCOVERY_H
#define AIMT_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include "aimt/model_capability.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

#define AIMT_MODELS_MAX 64
#define AIMT_DISCOVERY_BODY_MAX (2u * 1024u * 1024u)

typedef struct {
    char id[256];
    char provider[32];
    char quantization[64];
    char parameters[64];
    uint64_t disk_bytes;
    unsigned capabilities;
    bool capabilities_known;
} aimt_discovered_model;

typedef struct {
    char endpoint[256];
    char llama_endpoint[256];
    char ollama_binary[1024];
    char llama_binary[1024];
    char codex_binary[1024];
    bool ollama_online;
    bool llama_online;
    size_t count;
    size_t omitted;
    aimt_discovered_model models[AIMT_MODELS_MAX];
    char status[512];
} aimt_discovery;

/* Explicitly local-only endpoints, no proxy, no redirects, bounded response. */
bool aimt_local_endpoint_valid(const char *endpoint);
bool aimt_discovery_parse_models(const char *body, size_t length, const char *provider,
                                  aimt_discovery *out);
void aimt_discover(const char *ollama_endpoint, const char *llama_endpoint, aimt_discovery *out);
bool aimt_discovery_parse_capabilities(const char *body, size_t length, aimt_discovered_model *model);
/* True only when every required bit is reported by some installed adapter/model. */
bool aimt_discovery_supports(const aimt_discovery *local, unsigned required);

#ifdef __cplusplus
}
#endif

#endif
