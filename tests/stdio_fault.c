#define _GNU_SOURCE
/* Test-only stdio fault injection. Never linked into a production executable. */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fired;
static int target(FILE *stream, const char *stage) {
    const char *wanted=getenv("TURBOHAP_FAULT_FILE"), *mode=getenv("TURBOHAP_FAULT_STAGE");
    if(fired || !wanted || !mode || strcmp(stage,mode)) return 0;
    char proc[80],path[4096];
    snprintf(proc,sizeof(proc),"/proc/self/fd/%d",fileno(stream));
    ssize_t n=readlink(proc,path,sizeof(path)-1);
    if(n<0)return 0;
    path[n]=0;
    const char *base=strrchr(path,'/');
    return !strcmp(base?base+1:path,wanted);
}

size_t fwrite(const void *data,size_t size,size_t count,FILE *stream) {
    size_t (*real_write)(const void*,size_t,size_t,FILE*)=dlsym(RTLD_NEXT,"fwrite");
    int (*real_flush)(FILE*)=dlsym(RTLD_NEXT,"fflush");
    if(target(stream,"write")) {
        fired=1;
        real_flush(stream);
        int fd=fileno(stream),saved=dup(fd),full=open("/dev/full",O_WRONLY);
        if(saved<0 || full<0 || dup2(full,fd)<0)_exit(97);
        real_write("x",1,1,stream);
        if(real_flush(stream)==0 || !ferror(stream))_exit(98);
        if(dup2(saved,fd)<0)_exit(99);
        close(full);close(saved);
        errno=EIO;
        return 0;
    }
    return real_write(data,size,count,stream);
}

int fflush(FILE *stream) {
    int (*real_flush)(FILE*)=dlsym(RTLD_NEXT,"fflush");
    if(stream && target(stream,"flush")) {
        fired=1;real_flush(stream);errno=EIO;return EOF;
    }
    return real_flush(stream);
}

int fclose(FILE *stream) {
    int (*real_close)(FILE*)=dlsym(RTLD_NEXT,"fclose");
    if(target(stream,"close")) {
        fired=1;real_close(stream);errno=EIO;return EOF;
    }
    return real_close(stream);
}
