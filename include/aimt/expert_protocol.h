#ifndef AIMT_EXPERT_PROTOCOL_H
#define AIMT_EXPERT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIMT_EXPERT_PROTOCOL_VERSION 1u
#define AIMT_EXPERT_MESSAGE_MAX 32768u
#define AIMT_EXPERT_SOURCES_MAX 8u
#define AIMT_EXPERT_ATTEMPTS_MAX 8u

typedef enum {
    AIMT_EXPERT_REQUEST,
    AIMT_EXPERT_PROPOSAL,
    AIMT_EXPERT_NEEDS_GUIDANCE
} aimt_expert_kind;

typedef struct aimt_expert_message aimt_expert_message;

/* Data only: parsing never grants access, applies edits or executes tools.
 * Sets *out to NULL on failure. The caller owns a successful result.
 * error, if supplied, receives a fixed diagnostic without untrusted input. */
bool aimt_expert_message_parse(const char *json, size_t length,
    aimt_expert_message **out, char *error, size_t error_capacity);
void aimt_expert_message_destroy(aimt_expert_message *message);

/* Borrowed strings remain valid until destroy; absent optional fields are "".
 * Only call kind with a non-NULL, successfully parsed message. */
aimt_expert_kind aimt_expert_message_kind(const aimt_expert_message *message);
const char *aimt_expert_message_role(const aimt_expert_message *message);
const char *aimt_expert_message_operation(const aimt_expert_message *message);
const char *aimt_expert_message_text(const aimt_expert_message *message);
const char *aimt_expert_message_pseudocode(const aimt_expert_message *message);
const char *aimt_expert_message_question(const aimt_expert_message *message);
const char *aimt_expert_message_json(const aimt_expert_message *message);

/* Correlates IDs, role, operation and the exact ordered source references.
 * Does not verify actual files, tool results, privileges or attempt counters. */
bool aimt_expert_reply_matches(const aimt_expert_message *request,
    const aimt_expert_message *reply);

#ifdef __cplusplus
}
#endif

#endif
