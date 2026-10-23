#define _XOPEN_SOURCE 700
#include "aimt/project.h"
#include "aimt/app.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool aimt_project_open(aimt_project *project, const char *path)
{
    char resolved[4096];
    if (project == NULL || path == NULL || realpath(path, resolved) == NULL) return false;
    int fd = open(resolved, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return false;
    if (project->root_fd >= 0) close(project->root_fd);
    project->root_fd = fd;
    memcpy(project->root, resolved, strlen(resolved) + 1u);
    return true;
}

void aimt_project_close(aimt_project *project)
{
    if (project != NULL) {
        if (project->root_fd >= 0) close(project->root_fd);
        project->root_fd = -1;
        project->root[0] = '\0';
    }
}

bool aimt_project_create_folder(const aimt_project *project, const char *name)
{
    if (!project || project->root_fd < 0 || !name || !name[0] ||
        name[0] == '.' || strlen(name) > 255 || strchr(name, '/') || strpbrk(name, "\t\n") ||
        !aimt_text_valid(name, strlen(name))) {
        errno = EINVAL;
        return false;
    }
    return mkdirat(project->root_fd, name, 0700) == 0;
}

static int open_relative(const aimt_project *project, const char *path, bool directory)
{
    if (project == NULL || project->root_fd < 0 || path == NULL || path[0] == '/' || strlen(path) >= 4096) return -1;
    int fd = openat(project->root_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    if (path[0] == '\0') { if (!directory) { close(fd); return -1; } return fd; }
    const char *part = path;
    for (size_t depth = 0; depth < 64; ++depth) {
        const char *end = strchr(part, '/');
        size_t n = end != NULL ? (size_t)(end - part) : strlen(part);
        char name[256];
        if (n == 0 || n >= sizeof(name) || (n == 1 && part[0] == '.') ||
            (n == 2 && part[0] == '.' && part[1] == '.')) { close(fd); return -1; }
        memcpy(name, part, n); name[n] = '\0';
        int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
        if (end != NULL || directory) flags |= O_DIRECTORY;
        int next = openat(fd, name, flags);
        close(fd);
        if (next < 0) return -1;
        fd = next;
        if (end == NULL) {
            struct stat st;
            if (fstat(fd, &st) != 0 || !(directory ? S_ISDIR(st.st_mode) : (S_ISREG(st.st_mode) && st.st_nlink==1))) {
                close(fd); return -1;
            }
            return fd;
        }
        part = end + 1;
    }
    close(fd);
    return -1;
}

static bool agent_path(const char *path)
{
    if (!path || !path[0] || path[0]=='/' || strlen(path)>=4096 || !aimt_text_valid(path,strlen(path))) return false;
    const char *part=path;
    for (size_t depth=0; depth<64; depth++) {
        const char *end=strchr(part,'/');size_t n=end?(size_t)(end-part):strlen(part);
        if (!n || n>=256 || (n==1 && part[0]=='.') || (n==2 && !memcmp(part,"..",2))) return false;
        const char *protected[]={".git",".codex",".agents",".aws",".aimt-learning",".aimt-projects"};
        for(size_t i=0;i<sizeof(protected)/sizeof(protected[0]);i++)
            if(n==strlen(protected[i]) && !memcmp(part,protected[i],n)) return false;
        if(!end)return true;
        part=end+1;
    }
    return false;
}

static bool write_text(const aimt_project *project,const char *relative,const char *text,
    size_t maximum,const struct stat *expected)
{
    if(!project || project->root_fd<0 || !agent_path(relative) || !text || strlen(text)>maximum ||
       !aimt_text_valid(text,strlen(text)))return false;
    char parent[4096];snprintf(parent,sizeof(parent),"%s",relative);
    char *slash=strrchr(parent,'/');const char *name=relative;int dir;
    if(slash){*slash=0;name=strrchr(relative,'/')+1;dir=open_relative(project,parent,true);}
    else dir=openat(project->root_fd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(dir<0)return false;
    struct stat st;bool exists=fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)==0;
    /* Full replacement must not destroy a larger, partially read source. */
    bool ok=exists?(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==geteuid() && st.st_size>=0 && (uint64_t)st.st_size<=maximum):(errno==ENOENT);
    if(expected)ok=ok && exists && st.st_dev==expected->st_dev && st.st_ino==expected->st_ino &&
        st.st_size==expected->st_size && st.st_mtim.tv_sec==expected->st_mtim.tv_sec && st.st_mtim.tv_nsec==expected->st_mtim.tv_nsec;
    char temporary[96];int fd=-1;
    for(unsigned i=0;ok && i<32;i++) {
        snprintf(temporary,sizeof(temporary),".aimt-write-%ld-%u",(long)getpid(),i);
        fd=openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd>=0 || errno!=EEXIST)break;
    }
    if(fd<0)ok=false;
    size_t size=strlen(text),written=0;
    while(ok && written<size) {ssize_t n=write(fd,text+written,size-written);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)ok=false;else written+=(size_t)n;
    }
    if(ok && exists && fchmod(fd,st.st_mode&0777)!=0)ok=false;
    if(ok && fsync(fd)!=0)ok=false;
    if(fd>=0 && close(fd)!=0)ok=false;
    if(ok && expected) {
        struct stat current;
        ok=!fstatat(dir,name,&current,AT_SYMLINK_NOFOLLOW) && current.st_dev==expected->st_dev &&
            current.st_ino==expected->st_ino && current.st_nlink==1 && current.st_size==expected->st_size &&
            current.st_mtim.tv_sec==expected->st_mtim.tv_sec && current.st_mtim.tv_nsec==expected->st_mtim.tv_nsec;
    }
    if(ok && renameat(dir,temporary,dir,name)!=0)ok=false;
    if(ok && fsync(dir)!=0)ok=false;
    if(fd>=0)unlinkat(dir,temporary,0);
    close(dir);return ok;
}

bool aimt_project_write(const aimt_project *project,const char *relative,const char *text)
{ return write_text(project,relative,text,8192,NULL); }

bool aimt_project_replace(const aimt_project *project,const char *relative,const char *before,const char *after)
{
    const size_t maximum=1024u*1024u;
    if(!agent_path(relative) || !before || !before[0] || !after || strlen(before)>8192 || strlen(after)>8192 ||
       !aimt_text_valid(before,strlen(before)) || !aimt_text_valid(after,strlen(after)))return false;
    int fd=open_relative(project,relative,false);if(fd<0)return false;
    struct stat original;
    bool ok=!fstat(fd,&original) && original.st_uid==geteuid() && original.st_size>0 && (uint64_t)original.st_size<=maximum;
    size_t length=ok?(size_t)original.st_size:0;
    char *source=ok?malloc(length+1):NULL,*result=NULL;
    if(!source)ok=false;
    size_t used=0;
    while(ok && used<length) {
        ssize_t n=read(fd,source+used,length-used);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)ok=false;else used+=(size_t)n;
    }
    char extra;if(ok && read(fd,&extra,1)!=0)ok=false;
    close(fd);
    if(ok){source[length]=0;ok=strlen(source)==length && aimt_text_valid(source,length);}
    char *match=ok?strstr(source,before):NULL;
    if(!match || strstr(match+1,before))ok=false;
    size_t old_size=strlen(before),new_size=strlen(after),result_size=0;
    if(ok) {
        result_size=length-old_size+new_size;
        ok=result_size<=maximum;
        result=ok?malloc(result_size+1):NULL;if(!result)ok=false;
    }
    if(ok) {
        size_t prefix=(size_t)(match-source);
        memcpy(result,source,prefix);memcpy(result+prefix,after,new_size);
        memcpy(result+prefix+new_size,match+old_size,length-prefix-old_size);
        result[result_size]=0;
        ok=write_text(project,relative,result,maximum,&original);
    }
    /* Verify the resulting file in bounded chunks; a successful write is
     * evidence of text replacement only, never compilation or behavior. */
    if(ok) {
        fd=open_relative(project,relative,false);ok=fd>=0;used=0;char chunk[4096];
        while(ok && used<result_size) {
            size_t count=result_size-used;if(count>sizeof(chunk))count=sizeof(chunk);
            ssize_t n=read(fd,chunk,count);
            if(n<0 && errno==EINTR)continue;
            if(n<=0 || memcmp(chunk,result+used,(size_t)n))ok=false;else used+=(size_t)n;
        }
        if(ok && read(fd,&extra,1)!=0)ok=false;
        if(fd>=0)close(fd);
    }
    free(result);free(source);return ok;
}

bool aimt_project_agent_read(const aimt_project *project,const char *relative,
    char *text,size_t capacity,bool *truncated)
{ return agent_path(relative) && aimt_project_read(project,relative,text,capacity,truncated); }

bool aimt_project_agent_list(const aimt_project *project,const char *relative,
    aimt_project_entry *entries,size_t *count,bool *truncated)
{ return relative && (!relative[0] || agent_path(relative)) && aimt_project_list(project,relative,entries,count,truncated); }

bool aimt_project_agent_read_range(const aimt_project *project,const char *relative,
    uint64_t offset,char *text,size_t capacity,bool *truncated,uint64_t *next)
{
    if(!agent_path(relative) || !text || capacity<2 || capacity>8193 || !truncated || !next || offset>16u*1024u*1024u)return false;
    text[0]=0;*truncated=false;*next=offset;
    int fd=open_relative(project,relative,false);if(fd<0)return false;
    struct stat st;bool ok=!fstat(fd,&st) && st.st_size>=0 && (uint64_t)st.st_size>=offset;
    if(!ok || lseek(fd,(off_t)offset,SEEK_SET)<0){close(fd);return false;}
    size_t used=0;
    while(used<capacity-1){ssize_t n=read(fd,text+used,capacity-1-used);if(n<0 && errno==EINTR)continue;if(n<0){close(fd);return false;}if(!n)break;used+=(size_t)n;}
    close(fd);size_t leading=0;
    while(offset && leading<used && ((unsigned char)text[leading]&0xc0)==0x80 && leading<3)leading++;
    if(leading){memmove(text,text+leading,used-leading);used-=leading;offset+=leading;}
    size_t removed=0;
    if(offset+used<(uint64_t)st.st_size)
        while(used && !aimt_text_valid(text,used) && removed++<3)used--;
    if(!aimt_text_valid(text,used) || (!used && offset<(uint64_t)st.st_size)){text[0]=0;return false;}
    text[used]=0;*next=offset+used;*truncated=*next<(uint64_t)st.st_size;return true;
}

bool aimt_project_agent_find(const aimt_project *project,const char *relative,
    const char *needle,uint64_t *offset)
{
    if(!agent_path(relative) || !needle || !needle[0] || strlen(needle)>128 || !aimt_text_valid(needle,strlen(needle)) || !offset)return false;
    int fd=open_relative(project,relative,false);if(fd<0)return false;
    char chunk[4096+128];size_t carry=0,total=0,width=strlen(needle);bool found=false;
    while(total<16u*1024u*1024u) {
        ssize_t n=read(fd,chunk+carry,4096);if(n<0 && errno==EINTR)continue;if(n<=0)break;
        size_t length=carry+(size_t)n;
        for(size_t i=0;i+width<=length;i++)if(!memcmp(chunk+i,needle,width)){*offset=total-carry+i;found=true;break;}
        if(found)break;
        total+=(size_t)n;carry=length<width-1?length:width-1;
        memmove(chunk,chunk+length-carry,carry);
    }
    close(fd);return found;
}

static int compare(const void *a, const void *b)
{
    const aimt_project_entry *first = a, *second = b;
    if (first->directory != second->directory) return first->directory ? -1 : 1;
    return strcmp(first->name, second->name);
}

bool aimt_project_list(const aimt_project *project, const char *relative,
    aimt_project_entry *entries, size_t *count, bool *truncated)
{
    if (entries == NULL || count == NULL || truncated == NULL) return false;
    *count = 0; *truncated = false;
    int fd = open_relative(project, relative, true);
    if (fd < 0) return false;
    DIR *dir = fdopendir(fd);
    if (dir == NULL) { close(fd); return false; }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        size_t length = strlen(entry->d_name);
        if (length >= sizeof(entries[0].name) || !aimt_text_valid(entry->d_name, length)) continue;
        if (*count == AIMT_PROJECT_ENTRIES_MAX) { *truncated = true; break; }
        struct stat st;
        if (fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
        if (!(S_ISDIR(st.st_mode) || S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) continue;
        aimt_project_entry *target = &entries[(*count)++];
        memcpy(target->name, entry->d_name, length + 1u);
        target->directory = S_ISDIR(st.st_mode);
        target->symlink = S_ISLNK(st.st_mode);
    }
    closedir(dir);
    qsort(entries, *count, sizeof(entries[0]), compare);
    return true;
}

bool aimt_project_read(const aimt_project *project, const char *relative,
    char *text, size_t capacity, bool *truncated)
{
    if (text == NULL || capacity < 2 || truncated == NULL) return false;
    text[0] = '\0'; *truncated = false;
    int fd = open_relative(project, relative, false);
    if (fd < 0) return false;
    size_t used = 0;
    ssize_t n = 0;
    while (used < capacity - 1 && (n = read(fd, text + used, capacity - 1 - used)) > 0) used += (size_t)n;
    if (n < 0) { close(fd); text[0] = '\0'; return false; }
    char extra;
    n = read(fd, &extra, 1);
    close(fd);
    if (n < 0) { text[0] = '\0'; return false; }
    *truncated = n > 0;
    /* Avoid rejecting an otherwise valid preview for splitting the last UTF-8 character. */
    if (*truncated) {
        size_t shortened = 0;
        while (used > 0 && !aimt_text_valid(text, used) && shortened++ < 3) used--;
    }
    if (!aimt_text_valid(text, used)) { text[0] = '\0'; return false; }
    text[used] = '\0';
    return true;
}
