#define _GNU_SOURCE
#include <linux/landlock.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <linux/audit.h>
/* Invoked inside Bubblewrap. Write policy follows credentials through nested namespaces. */
static int allow(int rules,const char *path,uint64_t rights)
{
    int fd=open(path,O_PATH|O_CLOEXEC);if(fd<0)return -1;
    struct landlock_path_beneath_attr rule={.allowed_access=rights,.parent_fd=fd};
    int result=(int)syscall(SYS_landlock_add_rule,rules,LANDLOCK_RULE_PATH_BENEATH,&rule,0);close(fd);return result;
}
#define DENY(n) BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,(n),0,1), BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM)
static bool restrict_files(const char *project,const char *runtime,bool edit,bool server)
{
    int abi=(int)syscall(SYS_landlock_create_ruleset,NULL,0,LANDLOCK_CREATE_RULESET_VERSION);
    if(abi<3)return false;
    uint64_t reads=LANDLOCK_ACCESS_FS_READ_FILE|LANDLOCK_ACCESS_FS_READ_DIR|LANDLOCK_ACCESS_FS_EXECUTE;
    uint64_t writes=LANDLOCK_ACCESS_FS_WRITE_FILE|LANDLOCK_ACCESS_FS_REMOVE_DIR|LANDLOCK_ACCESS_FS_REMOVE_FILE|
        LANDLOCK_ACCESS_FS_MAKE_CHAR|LANDLOCK_ACCESS_FS_MAKE_DIR|LANDLOCK_ACCESS_FS_MAKE_REG|
        LANDLOCK_ACCESS_FS_MAKE_SOCK|LANDLOCK_ACCESS_FS_MAKE_FIFO|LANDLOCK_ACCESS_FS_MAKE_BLOCK|
        LANDLOCK_ACCESS_FS_MAKE_SYM|LANDLOCK_ACCESS_FS_REFER|LANDLOCK_ACCESS_FS_TRUNCATE;
    struct landlock_ruleset_attr policy={.handled_access_fs=reads|writes};
    int fd=(int)syscall(SYS_landlock_create_ruleset,&policy,sizeof(policy),0);if(fd<0)return false;
    bool ok=true;const char *system[]={"/usr","/bin","/lib","/lib64","/etc"};
    for(size_t i=0;i<sizeof(system)/sizeof(system[0]);i++) {
        if(access(system[i],F_OK)==0 && allow(fd,system[i],reads)<0)ok=false;
    }
    ok=ok && allow(fd,project,reads|(edit?writes:0))==0 && allow(fd,"/tmp/aimt-runtime",reads|writes)==0;
    if(server)ok=ok && allow(fd,"/tmp/aimt-codex-state",reads|writes)==0;
    /* Regular binaries need only file rights, not directory rights. */
    uint64_t file_reads=LANDLOCK_ACCESS_FS_READ_FILE|LANDLOCK_ACCESS_FS_EXECUTE;
    ok=ok && allow(fd,runtime,file_reads)==0;
    char package[4096];snprintf(package,sizeof(package),"%s",runtime);char *bin=strrchr(package,'/');
    if(bin && !strcmp(bin,"/codex")) {
        *bin=0;bin=strrchr(package,'/');
        if(bin && !strcmp(bin,"/bin")) {
            *bin=0;char manifest[8192];struct stat st;snprintf(manifest,sizeof(manifest),"%s/codex-package.json",package);
            if(stat(manifest,&st)==0 && S_ISREG(st.st_mode))ok=ok && allow(fd,package,reads)==0;
        }
    }
    ok=ok && allow(fd,"/dev/null",LANDLOCK_ACCESS_FS_READ_FILE|LANDLOCK_ACCESS_FS_WRITE_FILE)==0 &&
        allow(fd,"/dev/zero",LANDLOCK_ACCESS_FS_READ_FILE|LANDLOCK_ACCESS_FS_WRITE_FILE)==0 &&
        allow(fd,"/dev/urandom",LANDLOCK_ACCESS_FS_READ_FILE)==0;
    char helper[4096];ssize_t n=readlink("/proc/self/exe",helper,sizeof(helper)-1);
    if(n<0 || n>=(ssize_t)sizeof(helper)-1)ok=false;
    else {helper[n]=0;ok=ok && allow(fd,helper,file_reads)==0;
        char *name=strrchr(helper,'/');if(!name)ok=false;else {
            if((size_t)(name-helper)+strlen("/codex-sandbox-tools/bash")>=sizeof(helper))ok=false;
            else if(strcmp(name,"/bash")){strcpy(name,"/codex-sandbox-tools/bash");ok=ok && allow(fd,helper,file_reads)==0;}
        }}
    ok=ok && prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)==0 && syscall(SYS_landlock_restrict_self,fd,0)==0;
    close(fd);return ok;
}
static bool metadata_filter(void)
{
    /* Landlock does not mediate these metadata changes. Deny them in every mode. */
    struct sock_filter filter[]={
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,arch)),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_X86_64,1,0),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),
        BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K,0x40000000,0,1),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS),
        DENY(SYS_socket),DENY(SYS_socketpair),
        DENY(SYS_mount),DENY(SYS_umount2),DENY(SYS_pivot_root),
#ifdef SYS_open_tree
        DENY(SYS_open_tree),DENY(SYS_move_mount),DENY(SYS_fsopen),DENY(SYS_fsconfig),DENY(SYS_fsmount),DENY(SYS_mount_setattr),
#endif
        DENY(SYS_ptrace),DENY(SYS_process_vm_readv),DENY(SYS_process_vm_writev),
#ifdef SYS_pidfd_getfd
        DENY(SYS_pidfd_getfd),
#endif
        DENY(SYS_setxattr),DENY(SYS_lsetxattr),DENY(SYS_fsetxattr),
        DENY(SYS_removexattr),DENY(SYS_lremovexattr),DENY(SYS_fremovexattr),
        DENY(SYS_chmod),DENY(SYS_fchmod),DENY(SYS_fchmodat),
#ifdef SYS_fchmodat2
        DENY(SYS_fchmodat2),
#endif
        DENY(SYS_chown),DENY(SYS_fchown),DENY(SYS_lchown),DENY(SYS_fchownat),
        DENY(SYS_utime),DENY(SYS_utimes),DENY(SYS_futimesat),DENY(SYS_utimensat),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW)};
    struct sock_fprog program={.len=(unsigned short)(sizeof(filter)/sizeof(filter[0])),.filter=filter};
    return prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)==0 && prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&program)==0;
}
int main(int argc,char **argv)
{
    const char *name=strrchr(argv[0],'/');name=name?name+1:argv[0];
    if(!strcmp(name,"bash") || (argc>=3 && !strcmp(argv[1],"--tool"))) {
        const char *project=getenv("AIMT_SANDBOX_ROOT"),*runtime=getenv("AIMT_SANDBOX_RUNTIME"),*mode=getenv("AIMT_SANDBOX_MODE");
        if(!project || !runtime || !mode || !restrict_files(project,runtime,!strcmp(mode,"edit"),false) || !metadata_filter())return 1;
        if(!strcmp(name,"bash")){argv[0]="/bin/bash";execv(argv[0],argv);}
        else execv(argv[2],argv+2);
        perror("Tool exec");return 1;
    }
    if(argc<5 || (strcmp(argv[1],"read") && strcmp(argv[1],"edit")))return 2;
    umask(077);
    if(mkdir("/tmp/aimt-runtime",0700)!=0 && errno!=EEXIST)return 1;
    if(mkdir("/tmp/aimt-codex-state",0700)!=0 && errno!=EEXIST)return 1;
    if(chmod("/tmp/aimt-runtime",0700)!=0 || chmod("/tmp/aimt-codex-state",0700)!=0)return 1;
    if(mkdir("/tmp/aimt-runtime/home",0700)!=0 && errno!=EEXIST)return 1;
    setenv("HOME","/tmp/aimt-runtime/home",1);
    setenv("XDG_CACHE_HOME","/tmp/aimt-runtime/cache",1);
    setenv("XDG_CONFIG_HOME","/tmp/aimt-runtime/config",1);
    setenv("TMPDIR","/tmp/aimt-runtime",1);setenv("XDG_RUNTIME_DIR","/tmp/aimt-runtime",1);
    setenv("CODEX_HOME","/tmp/aimt-codex-state",1);
    setenv("AIMT_SANDBOX_ROOT",argv[2],1);setenv("AIMT_SANDBOX_RUNTIME",argv[3],1);setenv("AIMT_SANDBOX_MODE",argv[1],1);
    if(!restrict_files(argv[2],argv[3],!strcmp(argv[1],"edit"),true)) {
        fprintf(stderr,"Required Landlock policy unavailable; refusing execution.\n");return 1;
    }
    execv(argv[3],argv+3);perror("Codex exec");return 1;
}
