#ifndef AIMT_PROJECT_BOOK_H
#define AIMT_PROJECT_BOOK_H
#include <stdbool.h>
#include <stddef.h>
#define AIMT_PROJECT_BOOK_MAX 32
typedef struct { char name[96],path[4096];bool allow_write; } aimt_project_record;
typedef struct { size_t count;aimt_project_record projects[AIMT_PROJECT_BOOK_MAX]; } aimt_project_book;
bool aimt_project_book_add(aimt_project_book *book,const char *path);
size_t aimt_project_book_find(const aimt_project_book *book,const char *canonical_path);
bool aimt_project_book_save(const char *directory,const aimt_project_book *book);
bool aimt_project_book_load(const char *directory,aimt_project_book *book);
#endif
