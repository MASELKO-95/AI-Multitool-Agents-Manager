#include "aimt/expert_protocol.h"
#include "aimt/app.h"

#include <json-c/json.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct aimt_expert_message {
    json_object *root;
    aimt_expert_kind kind;
};

/* This is a vocabulary, not a model router or an execution capability. */
static const struct { const char *role, *operation; } vocabulary[] = {
    {"planner", "plan"}, {"coordinator", "route"},
    {"project_analysis", "analyze_project"}, {"api_contracts", "define_api"},
    {"implement_c", "implement"}, {"implement_cpp", "implement"},
    {"implement_python", "implement"}, {"build_diagnosis", "diagnose_build"},
    {"behavior_diagnosis", "diagnose_behavior"}, {"tests", "propose_tests"},
    {"code_review", "review"}, {"memory_concurrency", "analyze_memory"},
    {"performance", "analyze_performance"}
};

static void diagnostic(char *out, size_t capacity, const char *text)
{
    if (out && capacity) snprintf(out, capacity, "%s", text);
}

static json_object *property(json_object *root, const char *key)
{
    json_object *value = NULL;
    json_object_object_get_ex(root, key, &value);
    return value;
}

static const char *string(json_object *root, const char *key)
{
    json_object *value = property(root, key);
    return json_object_is_type(value, json_type_string) ? json_object_get_string(value) : "";
}

static bool text(json_object *value, size_t minimum, size_t maximum)
{
    if (!json_object_is_type(value, json_type_string)) return false;
    size_t n = (size_t)json_object_get_string_len(value);
    return n >= minimum && n <= maximum && aimt_text_valid(json_object_get_string(value), n);
}

static bool number(json_object *value, int64_t minimum, int64_t maximum)
{
    if (!json_object_is_type(value, json_type_int)) return false;
    int64_t n = json_object_get_int64(value);
    return n >= minimum && n <= maximum;
}

static bool alphanumeric(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static bool identifier(json_object *value, size_t maximum, bool revision)
{
    if (!text(value, 1, maximum)) return false;
    const unsigned char *s = (const unsigned char *)json_object_get_string(value);
    if (!alphanumeric(s[0])) return false;
    for (size_t i = 1; s[i]; ++i)
        if (!alphanumeric(s[i]) && s[i] != '_' && s[i] != '-' && s[i] != '.' &&
            !(revision && s[i] == ':')) return false;
    return true;
}

static bool whitespace(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Called only after json-c has checked the four hexadecimal digits. */
static unsigned utf16_unit(const char *s)
{
    unsigned value = 0;
    for (size_t i = 0; i < 4; ++i) {
        unsigned char c = (unsigned char)s[i];
        unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c - '0') :
            c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) : (unsigned)(c - 'A' + 10);
        value = value * 16 + digit;
    }
    return value;
}

/* json-c overwrites duplicate keys. Count keys in the original valid JSON and
 * compare with the parsed tree so duplicates at any depth cannot disappear.
 * v1 field names are literal ASCII; escaped keys are deliberately refused. */
static bool original_keys(const char *json, size_t length, size_t *count)
{
    *count = 0;
    for (size_t i = 0; i < length; ++i) {
        if (json[i] != '"') continue;
        bool escaped = false;
        size_t begin = ++i;
        while (i < length && json[i] != '"') {
            if (json[i] != '\\') { ++i; continue; }
            escaped = true;
            if (i + 1 >= length) return false;
            if (json[i + 1] != 'u') { i += 2; continue; }
            if (length - i < 6) return false;
            unsigned unit = utf16_unit(json + i + 2);
            if (unit >= 0xdc00 && unit <= 0xdfff) return false;
            i += 6;
            if (unit >= 0xd800 && unit <= 0xdbff) {
                if (length - i < 6 || json[i] != '\\' || json[i + 1] != 'u') return false;
                unsigned low = utf16_unit(json + i + 2);
                if (low < 0xdc00 || low > 0xdfff) return false;
                i += 6;
            }
        }
        if (i >= length) return false;
        size_t next = i + 1;
        while (next < length && whitespace((unsigned char)json[next])) ++next;
        if (next < length && json[next] == ':') {
            if (escaped) return false;
            for (size_t k = begin; k < i; ++k)
                if ((unsigned char)json[k] < 0x20 || (unsigned char)json[k] >= 0x7f) return false;
            ++*count;
        }
    }
    return true;
}

static bool tree(json_object *value, unsigned depth, size_t *keys)
{
    if (depth > 12) return false;
    if (json_object_is_type(value, json_type_string)) return text(value, 0, AIMT_EXPERT_MESSAGE_MAX);
    if (json_object_is_type(value, json_type_object)) {
        *keys += (size_t)json_object_object_length(value);
        json_object_object_foreach(value, key, child) {
            (void)key;
            if (!tree(child, depth + 1, keys)) return false;
        }
    } else if (json_object_is_type(value, json_type_array)) {
        for (size_t i = 0; i < json_object_array_length(value); ++i)
            if (!tree(json_object_array_get_idx(value, i), depth + 1, keys)) return false;
    }
    return true;
}

static bool fields(json_object *object, const char *const *required, size_t required_count,
    const char *const *optional, size_t optional_count)
{
    if (!json_object_is_type(object, json_type_object)) return false;
    for (size_t i = 0; i < required_count; ++i) {
        json_object *unused = NULL;
        if (!json_object_object_get_ex(object, required[i], &unused)) return false;
    }
    json_object_object_foreach(object, key, value) {
        (void)value;
        bool known = false;
        for (size_t i = 0; i < required_count; ++i) if (!strcmp(key, required[i])) known = true;
        for (size_t i = 0; i < optional_count; ++i) if (!strcmp(key, optional[i])) known = true;
        if (!known) return false;
    }
    return true;
}

static bool text_array(json_object *value, size_t minimum, size_t maximum, size_t item_maximum)
{
    if (!json_object_is_type(value, json_type_array)) return false;
    size_t n = json_object_array_length(value);
    if (n < minimum || n > maximum) return false;
    for (size_t i = 0; i < n; ++i)
        if (!text(json_object_array_get_idx(value, i), 1, item_maximum)) return false;
    return true;
}

/* Lexical check only. The file tool still has to enforce filesystem access,
 * refuse links and check the current version before reading/applying edits. */
static bool path(json_object *value)
{
    if (!text(value, 1, 511)) return false;
    const char *part = json_object_get_string(value);
    if (part[0] == '/' || strpbrk(part, "\\:\t\n")) return false;
    static const char *const protected[] = {
        ".git", ".codex", ".agents", ".aws", ".aimt-learning", ".aimt-projects"
    };
    for (size_t depth = 0; depth < 64; ++depth) {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (!n || n > 255 || (n == 1 && part[0] == '.') || (n == 2 && !memcmp(part, "..", 2))) return false;
        for (size_t i = 0; i < sizeof(protected) / sizeof(protected[0]); ++i)
            if (n == strlen(protected[i]) && !memcmp(part, protected[i], n)) return false;
        if (!end) return true;
        part = end + 1;
    }
    return false;
}

static bool sources(json_object *value)
{
    static const char *const keys[] = {"path", "revision", "first_line", "last_line"};
    if (!json_object_is_type(value, json_type_array) || json_object_array_length(value) > AIMT_EXPERT_SOURCES_MAX) return false;
    for (size_t i = 0; i < json_object_array_length(value); ++i) {
        json_object *row = json_object_array_get_idx(value, i);
        if (!fields(row, keys, 4, NULL, 0) || !path(property(row, "path")) ||
            !identifier(property(row, "revision"), 128, true) ||
            !number(property(row, "first_line"), 1, 10000000) ||
            !number(property(row, "last_line"), 1, 10000000) ||
            json_object_get_int64(property(row, "last_line")) < json_object_get_int64(property(row, "first_line"))) return false;
        for (size_t j = 0; j < i; ++j) {
            json_object *previous = json_object_array_get_idx(value, j);
            if (!strcmp(string(row, "path"), string(previous, "path")) &&
                (strcmp(string(row, "revision"), string(previous, "revision")) || json_object_equal(row, previous))) return false;
        }
    }
    return true;
}

static bool attempts(json_object *value)
{
    static const char *const keys[] = {"attempt", "input_revision", "failure", "change", "error"};
    static const char *const failures[] = {"invalid_output", "build_failed", "test_failed", "missing_context"};
    if (!json_object_is_type(value, json_type_array)) return false;
    size_t n = json_object_array_length(value);
    if (!n || n > AIMT_EXPERT_ATTEMPTS_MAX) return false;
    int64_t last = 0;
    for (size_t i = 0; i < n; ++i) {
        json_object *row = json_object_array_get_idx(value, i);
        if (!fields(row, keys, 5, NULL, 0) || !number(property(row, "attempt"), 1, 128) ||
            !identifier(property(row, "input_revision"), 128, true) ||
            !text(property(row, "change"), 1, 1024) || !text(property(row, "error"), 1, 1024)) return false;
        int64_t current = json_object_get_int64(property(row, "attempt"));
        if (current <= last) return false;
        last = current;
        bool known = false;
        for (size_t j = 0; j < sizeof(failures) / sizeof(failures[0]); ++j)
            if (!strcmp(string(row, "failure"), failures[j])) known = true;
        if (!known) return false;
    }
    return true;
}

bool aimt_expert_message_parse(const char *json, size_t length,
    aimt_expert_message **out, char *error, size_t error_capacity)
{
    if (out) *out = NULL;
    diagnostic(error, error_capacity, "");
    if (!out || !json || !length || length > AIMT_EXPERT_MESSAGE_MAX || memchr(json, 0, length)) {
        diagnostic(error, error_capacity, "Invalid message size or NUL in the message.");
        return false;
    }
    json_tokener *tok = json_tokener_new_ex(12);
    if (!tok) { diagnostic(error, error_capacity, "Cannot allocate parser memory."); return false; }
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    json_object *root = json_tokener_parse_ex(tok, json, (int)length);
    bool parsed = json_tokener_get_error(tok) == json_tokener_success &&
        json_tokener_get_parse_end(tok) == length && json_object_is_type(root, json_type_object);
    json_tokener_free(tok);
    const char *failure = "One complete UTF-8 JSON object is required.";
    if (!parsed) goto rejected;
    size_t original = 0, decoded = 0;
    failure = "Duplicate/escaped keys or disallowed text.";
    if (!original_keys(json, length, &original) || !tree(root, 0, &decoded) || original != decoded) goto rejected;
    failure = "Unsupported protocol version.";
    if (!number(property(root, "v"), 1, AIMT_EXPERT_PROTOCOL_VERSION)) goto rejected;
    failure = "Unknown role, operation or task identifier.";
    if (!identifier(property(root, "task_id"), 64, false) ||
        !identifier(property(root, "subtask_id"), 64, false)) goto rejected;
    bool vocabulary_match = false;
    for (size_t i = 0; i < sizeof(vocabulary) / sizeof(vocabulary[0]); ++i)
        if (!strcmp(string(root, "role"), vocabulary[i].role) &&
            !strcmp(string(root, "operation"), vocabulary[i].operation)) vocabulary_match = true;
    if (!vocabulary_match) goto rejected;
    failure = "Invalid source references.";
    if (!sources(property(root, "sources"))) goto rejected;

    static const char *const optional[] = {"pseudocode"};
    static const char *const request_keys[] = {
        "v", "kind", "task_id", "subtask_id", "role", "operation", "sources",
        "goal", "api", "constraints", "acceptance"
    };
    static const char *const proposal_keys[] = {
        "v", "kind", "task_id", "subtask_id", "role", "operation", "sources", "text", "missing"
    };
    static const char *const guidance_keys[] = {
        "v", "kind", "task_id", "subtask_id", "role", "operation", "sources", "attempts", "missing", "question"
    };
    aimt_expert_kind kind;
    failure = "Invalid message fields or limits exceeded.";
    const char *name = string(root, "kind");
    if (!strcmp(name, "request")) {
        kind = AIMT_EXPERT_REQUEST;
        if (!fields(root, request_keys, 11, optional, 1) || !text(property(root, "goal"), 1, 2048) ||
            !text_array(property(root, "api"), 0, 8, 1024) ||
            !text_array(property(root, "constraints"), 0, 8, 256) ||
            !text_array(property(root, "acceptance"), 1, 8, 512)) goto rejected;
    } else if (!strcmp(name, "proposal")) {
        kind = AIMT_EXPERT_PROPOSAL;
        if (!fields(root, proposal_keys, 9, optional, 1) || !text(property(root, "text"), 1, 8192) ||
            !text_array(property(root, "missing"), 0, 8, 512)) goto rejected;
    } else if (!strcmp(name, "needs_guidance")) {
        kind = AIMT_EXPERT_NEEDS_GUIDANCE;
        if (!strcmp(string(root, "role"), "coordinator") || !fields(root, guidance_keys, 10, NULL, 0) ||
            !attempts(property(root, "attempts")) || !text_array(property(root, "missing"), 0, 8, 512) ||
            !text(property(root, "question"), 1, 1024)) goto rejected;
    } else goto rejected;
    json_object *pseudocode = NULL;
    if (json_object_object_get_ex(root, "pseudocode", &pseudocode) && !text(pseudocode, 1, 4096)) goto rejected;
    aimt_expert_message *message = malloc(sizeof(*message));
    if (!message) { failure = "Cannot allocate message memory."; goto rejected; }
    *message = (aimt_expert_message){root, kind};
    *out = message;
    return true;
rejected:
    json_object_put(root);
    diagnostic(error, error_capacity, failure);
    return false;
}

void aimt_expert_message_destroy(aimt_expert_message *message)
{
    if (message) { json_object_put(message->root); free(message); }
}

aimt_expert_kind aimt_expert_message_kind(const aimt_expert_message *message) { return message->kind; }
const char *aimt_expert_message_role(const aimt_expert_message *message) { return message ? string(message->root, "role") : ""; }
const char *aimt_expert_message_operation(const aimt_expert_message *message) { return message ? string(message->root, "operation") : ""; }
const char *aimt_expert_message_text(const aimt_expert_message *message) { return message ? string(message->root, "text") : ""; }
const char *aimt_expert_message_pseudocode(const aimt_expert_message *message) { return message ? string(message->root, "pseudocode") : ""; }
const char *aimt_expert_message_question(const aimt_expert_message *message) { return message ? string(message->root, "question") : ""; }
const char *aimt_expert_message_json(const aimt_expert_message *message)
{
    return message ? json_object_to_json_string_ext(message->root, JSON_C_TO_STRING_PLAIN) : "";
}

bool aimt_expert_reply_matches(const aimt_expert_message *request, const aimt_expert_message *reply)
{
    if (!request || !reply || request->kind != AIMT_EXPERT_REQUEST || reply->kind == AIMT_EXPERT_REQUEST) return false;
    static const char *const keys[] = {"task_id", "subtask_id", "role", "operation", "sources"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (!json_object_equal(property(request->root, keys[i]), property(reply->root, keys[i]))) return false;
    return true;
}
