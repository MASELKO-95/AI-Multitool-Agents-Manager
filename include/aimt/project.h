#ifndef AIMT_PROJECT_H
#define AIMT_PROJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIMT_PROJECT_ENTRIES_MAX 256
typedef struct { char name[256]; bool directory; bool symlink; } aimt_project_entry;
typedef struct { int root_fd; char root[4096]; } aimt_project;

bool aimt_project_open(aimt_project *project, const char *path);
void aimt_project_close(aimt_project *project);
/* Explicit user action: create one child directory, without replacing entries.
 * Failure sets errno; name must be a single visible UTF-8 filename. */
bool aimt_project_create_folder(const aimt_project *project, const char *name);
bool aimt_project_list(const aimt_project *project, const char *relative,
    aimt_project_entry *entries, size_t *count, bool *truncated);
bool aimt_project_read(const aimt_project *project, const char *relative,
    char *text, size_t capacity, bool *truncated);
/* Text-only atomic replace. Refuses protected metadata, links and traversal. */
bool aimt_project_write(const aimt_project *project, const char *relative, const char *text);
/* Replace one unique exact anchor in an existing text file <= 1 MiB.
 * Both fragments <= 8192 bytes; refuses missing/ambiguous anchors and links. */
bool aimt_project_replace(const aimt_project *project,const char *relative,
    const char *before,const char *after);
bool aimt_project_agent_read(const aimt_project *project, const char *relative,
    char *text, size_t capacity, bool *truncated);
bool aimt_project_agent_read_range(const aimt_project *project,const char *relative,
    uint64_t offset,char *text,size_t capacity,bool *truncated,uint64_t *next);
bool aimt_project_agent_find(const aimt_project *project,const char *relative,
    const char *needle,uint64_t *offset);
bool aimt_project_agent_list(const aimt_project *project, const char *relative,
    aimt_project_entry *entries, size_t *count, bool *truncated);

#ifdef __cplusplus
}
#endif

#endif
