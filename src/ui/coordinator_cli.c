#define _POSIX_C_SOURCE 200809L
#include "aimt/local_generate.h"
#include "aimt/app.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void interrupt(int signal_number) { (void)signal_number;stopped=1; }
typedef struct { FILE *file; bool live; size_t used;atomic_bool failed;struct timespec started; } journal;
static double elapsed(const journal *j)
{ struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);return now.tv_sec-j->started.tv_sec+(now.tv_nsec-j->started.tv_nsec)/1e9; }
static void record(void *data,const char *event,unsigned step,const char *text)
{
    journal *j=data;if(atomic_load(&j->failed))return;
    json_object *row=json_object_new_object();if(!row){atomic_store(&j->failed,true);return;}
    json_object_object_add(row,"event",json_object_new_string(event));
    json_object_object_add(row,"step",json_object_new_int64(step));
    json_object_object_add(row,"elapsed_seconds",json_object_new_double(elapsed(j)));
    json_object_object_add(row,"text",json_object_new_string(text));
    const char *s=json_object_to_json_string_ext(row,JSON_C_TO_STRING_PLAIN);
    size_t n=s?strlen(s):0;
    if(!n || n+1>4u*1024u*1024u-j->used || fprintf(j->file,"%s\n",s)<0 || fflush(j->file))atomic_store(&j->failed,true);
    else {
    if (j->live && s) {
        fprintf(stderr, "%s\n", s);
        fflush(stderr);
    }
    j->used+=n+1;
}
    json_object_put(row);
}
static bool number(const char *s,unsigned *out,unsigned maximum)
{
    if(!s || *s<'0' || *s>'9')return false;
    char *end=NULL;errno=0;unsigned long n=strtoul(s,&end,10);
    if(errno || *end || !n || n>maximum)return false;
    *out=(unsigned)n;return true;
}
static bool task_read(const char *path,char out[2049])
{
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return false;
    struct stat st;size_t used=0;bool ok=fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size>0 && st.st_size<=2048;
    while(ok && used<2048){ssize_t n=read(fd,out+used,2048-used);if(n<0 && errno==EINTR)continue;if(n<0){ok=false;break;}if(!n)break;used+=(size_t)n;}
    char extra;if(ok && read(fd,&extra,1)!=0)ok=false;close(fd);
    out[used]=0;return ok && strlen(out)==used && aimt_text_valid(out,used);
}
int main(int argc,char **argv)
{
    const char *project=NULL,*coordinator=NULL,*model=NULL,*task_path=NULL,*output=NULL,*endpoint="http://127.0.0.1:11434";
    unsigned steps=12,timeout=300;bool edit=false,chat=false,live=false;
    aimt_local_options options={0};
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--edit"))edit=true;
        else if(!strcmp(argv[i],"--chat"))chat=true;
else if(!strcmp(argv[i],"--live"))live=true;
        else if(!strcmp(argv[i],"--unload"))options.unload=true;
        else if(!strcmp(argv[i],"--file-briefs"))options.file_briefs=true;
        else if(!strcmp(argv[i],"--help")) {
            puts("ai-multitool-agents-manager-coordinate --project DIR --coordinator MODEL --model MODEL --task-file TEXT --output NEW.jsonl [--edit] [--chat] [--live] [--steps 1..128] [--timeout 1..3600] [--coordinator-context 2048..131072] [--worker-context 2048..131072] [--threads 1..64] [--request-timeout 1..3600] [--unload] [--file-briefs] [--endpoint URL]");return 0;
        } else if(i+1<argc) {
            const char *key=argv[i++],*v=argv[i];
            if(!strcmp(key,"--project"))project=v;
            else if(!strcmp(key,"--coordinator"))coordinator=v;
            else if(!strcmp(key,"--model"))model=v;
            else if(!strcmp(key,"--task-file"))task_path=v;
            else if(!strcmp(key,"--output"))output=v;
            else if(!strcmp(key,"--endpoint"))endpoint=v;
            else if(!strcmp(key,"--steps")){if(!number(v,&steps,128))return 2;}
            else if(!strcmp(key,"--timeout")){if(!number(v,&timeout,3600))return 2;}
            else if(!strcmp(key,"--coordinator-context")){if(!number(v,&options.coordinator_context,131072) || options.coordinator_context<2048)return 2;}
            else if(!strcmp(key,"--worker-context")){if(!number(v,&options.worker_context,131072) || options.worker_context<2048)return 2;}
            else if(!strcmp(key,"--threads")){if(!number(v,&options.threads,64))return 2;}
            else if(!strcmp(key,"--request-timeout")){if(!number(v,&options.request_timeout,3600))return 2;}
            else return 2;
        } else return 2;
    }
    char task[2049];
    if(!project || !coordinator || !model || !task_path || !output || !task_read(task_path,task)) {fputs("Missing arguments or invalid task (2048-byte limit).\n",stderr);return 2;}
    int fd=open(output,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0){perror("New report file");return 2;}
    journal j={.file=fdopen(fd,"w"), .live=live};atomic_init(&j.failed,false);clock_gettime(CLOCK_MONOTONIC,&j.started);
    if(!j.file){close(fd);return 1;}
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){fclose(j.file);return 1;}
    signal(SIGINT,interrupt);signal(SIGTERM,interrupt);
    record(&j,"task",0,task);record(&j,"coordinator",0,coordinator);record(&j,"worker",0,model);
    options.max_steps=steps;options.observer=record;options.observer_data=&j;options.chat=chat;
    aimt_local_job *job=aimt_local_coordinate_options(endpoint,coordinator,model,project,task,edit,&options);
    if(!job){record(&j,"error",0,"The task was not started.");fclose(j.file);curl_global_cleanup();return 2;}
    while(!aimt_local_done(job)) {
        if(stopped || elapsed(&j)>=timeout || atomic_load(&j.failed))aimt_local_cancel(job);
        struct timespec pause={0,100000000};nanosleep(&pause,NULL);
    }
    bool success=false;const char *result=aimt_local_result(job,&success);
    record(&j,success?"complete":"blocked",0,result?result:"");
    printf("%s\nTime: %.2f s; model result: %s. Journal: %s\n",result?result:"No result",elapsed(&j),success?"complete":"blocked",output);
    aimt_local_destroy(job);bool valid=!atomic_load(&j.failed);
    if(fclose(j.file))valid=false;
    curl_global_cleanup();return success && valid?0:1;
}
