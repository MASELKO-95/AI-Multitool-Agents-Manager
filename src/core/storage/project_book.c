#define _XOPEN_SOURCE 700
#include "aimt/project_book.h"
#include "aimt/app.h"
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define BOOK_BYTES (192u*1024u)
static bool text_valid(const char *s,size_t capacity)
{ const char *end=memchr(s,0,capacity);return end && end!=s && aimt_text_valid(s,(size_t)(end-s)); }
static bool valid(const aimt_project_book *b)
{
    if(!b || b->count>AIMT_PROJECT_BOOK_MAX)return false;
    for(size_t i=0;i<b->count;i++) {
        const aimt_project_record *p=&b->projects[i];
        if(!text_valid(p->path,sizeof(p->path)) || p->path[0]!='/' || !text_valid(p->name,sizeof(p->name)))return false;
        for(size_t k=0;k<i;k++)if(!strcmp(p->path,b->projects[k].path))return false;
    }
    return true;
}
size_t aimt_project_book_find(const aimt_project_book *b,const char *path)
{
    if(!valid(b) || !path)return SIZE_MAX;
    for(size_t i=0;i<b->count;i++)if(!strcmp(b->projects[i].path,path))return i;
    return SIZE_MAX;
}
bool aimt_project_book_add(aimt_project_book *b,const char *path)
{
    char resolved[4096];struct stat st;
    if(!valid(b) || !path || !realpath(path,resolved) || stat(resolved,&st) || !S_ISDIR(st.st_mode))return false;
    if(aimt_project_book_find(b,resolved)!=SIZE_MAX)return true;
    if(b->count==AIMT_PROJECT_BOOK_MAX)return false;
    const char *name=strrchr(resolved,'/');name=name && name[1]?name+1:resolved;
    if(strlen(name)>=sizeof(b->projects[0].name))return false;
    aimt_project_record *p=&b->projects[b->count++];
    memcpy(p->name,name,strlen(name)+1);snprintf(p->path,sizeof(p->path),"%s",resolved);p->allow_write=false;
    return true;
}
static int open_directory(const char *path,bool create)
{
    if(!path || (create && mkdir(path,0700) && errno!=EEXIST))return -1;
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(fd>=0 && (fstat(fd,&st) || st.st_uid!=geteuid() || (st.st_mode&0077))){close(fd);return -1;}
    return fd;
}
bool aimt_project_book_save(const char *directory,const aimt_project_book *b)
{
    if(!valid(b))return false;
    json_object *root=json_object_new_object(),*projects=json_object_new_array();
    if(!root || !projects){json_object_put(root);json_object_put(projects);return false;}
    json_object_object_add(root,"schema_version",json_object_new_int(1));json_object_object_add(root,"projects",projects);
    for(size_t i=0;i<b->count;i++) {
        json_object *row=json_object_new_object();if(!row){json_object_put(root);return false;}
        json_object_object_add(row,"name",json_object_new_string(b->projects[i].name));
        json_object_object_add(row,"path",json_object_new_string(b->projects[i].path));
        json_object_object_add(row,"allow_write",json_object_new_boolean(b->projects[i].allow_write));json_object_array_add(projects,row);
    }
    const char *s=json_object_to_json_string_ext(root,JSON_C_TO_STRING_PRETTY);size_t length=s?strlen(s):0;
    int dir=length && length<=BOOK_BYTES?open_directory(directory,true):-1;
    if(dir<0){json_object_put(root);return false;}
    struct stat st;bool exists=fstatat(dir,"projects.json",&st,AT_SYMLINK_NOFOLLOW)==0;
    bool ok=exists?(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==geteuid() && !(st.st_mode&0077)):(errno==ENOENT);
    int fd=-1;char temporary[64];
    for(unsigned i=0;ok && i<32;i++) {
        snprintf(temporary,sizeof(temporary),".projects-%ld-%u.tmp",(long)getpid(),i);
        fd=openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd>=0 || errno!=EEXIST)break;
    }
    if(fd<0)ok=false;
    size_t written=0;
    while(ok && written<length){ssize_t n=write(fd,s+written,length-written);if(n<0 && errno==EINTR)continue;if(n<=0)ok=false;else written+=(size_t)n;}
    if(ok && fsync(fd))ok=false;
    if(fd>=0 && close(fd))ok=false;
    if(ok && renameat(dir,temporary,dir,"projects.json"))ok=false;
    if(ok && fsync(dir))ok=false;
    if(fd>=0)unlinkat(dir,temporary,0);
    close(dir);json_object_put(root);return ok;
}
static bool copy(json_object *row,const char *key,char *out,size_t capacity)
{
    json_object *v=NULL;if(!json_object_object_get_ex(row,key,&v) || !json_object_is_type(v,json_type_string))return false;
    const char *s=json_object_get_string(v);size_t n=(size_t)json_object_get_string_len(v);
    if(!n || n>=capacity || strlen(s)!=n || !aimt_text_valid(s,n))return false;
    memcpy(out,s,n+1);return true;
}
bool aimt_project_book_load(const char *directory,aimt_project_book *b)
{
    if(!b)return false;
    int dir=open_directory(directory,false);if(dir<0)return false;
    int fd=openat(dir,"projects.json",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);close(dir);if(fd<0)return false;
    struct stat st;bool ok=!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==geteuid() && !(st.st_mode&0077) && st.st_size>0 && st.st_size<=BOOK_BYTES;
    char *s=ok?malloc(BOOK_BYTES+1):NULL;size_t used=0;if(!s)ok=false;
    while(ok && used<BOOK_BYTES){ssize_t n=read(fd,s+used,BOOK_BYTES-used);if(n<0 && errno==EINTR)continue;if(n<0){ok=false;break;}if(!n)break;used+=(size_t)n;}
    char extra;if(ok && read(fd,&extra,1)!=0)ok=false;close(fd);if(!ok){free(s);return false;}
    json_tokener *tok=json_tokener_new_ex(8);aimt_project_book *draft=calloc(1,sizeof(*draft));
    if(!tok || !draft){if(tok)json_tokener_free(tok);free(draft);free(s);return false;}
    json_tokener_set_flags(tok,JSON_TOKENER_STRICT);json_object *root=json_tokener_parse_ex(tok,s,(int)used),*version=NULL,*rows=NULL;
    ok=json_tokener_get_error(tok)==json_tokener_success && json_tokener_get_parse_end(tok)==used &&
        json_object_object_get_ex(root,"schema_version",&version) && json_object_is_type(version,json_type_int) && json_object_get_int64(version)==1 &&
        json_object_object_get_ex(root,"projects",&rows) && json_object_is_type(rows,json_type_array) && json_object_array_length(rows)<=AIMT_PROJECT_BOOK_MAX;
    if(ok)draft->count=json_object_array_length(rows);
    for(size_t i=0;ok && i<draft->count;i++) {
        json_object *row=json_object_array_get_idx(rows,i),*write=NULL;
        ok=copy(row,"name",draft->projects[i].name,sizeof(draft->projects[i].name)) && copy(row,"path",draft->projects[i].path,sizeof(draft->projects[i].path)) &&
            json_object_object_get_ex(row,"allow_write",&write) && json_object_is_type(write,json_type_boolean);
        if(ok)draft->projects[i].allow_write=json_object_get_boolean(write);
    }
    if(ok)ok=valid(draft);
    if(ok)*b=*draft;
    json_object_put(root);json_tokener_free(tok);free(draft);free(s);return ok;
}
