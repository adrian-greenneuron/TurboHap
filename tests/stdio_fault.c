#define _GNU_SOURCE
/* Test-only stdio fault injection. Never linked into a production executable. */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__)
#define FAULT_WRITE turbo_fault_fwrite
#define FAULT_FLUSH turbo_fault_fflush
#define FAULT_CLOSE turbo_fault_fclose
#else
#define FAULT_WRITE fwrite
#define FAULT_FLUSH fflush
#define FAULT_CLOSE fclose
#endif
static int fired;
static int target(FILE *stream, const char *stage) {
    const char *wanted=getenv("TURBOHAP_FAULT_FILE"), *mode=getenv("TURBOHAP_FAULT_STAGE");
    if(fired || !wanted || !mode || strcmp(stage,mode)) return 0;
    char proc[80],path[4096];
#if defined(__APPLE__)
    if(fcntl(fileno(stream),F_GETPATH,path)<0)return 0;
#else
    snprintf(proc,sizeof(proc),"/proc/self/fd/%d",fileno(stream));
    ssize_t n=readlink(proc,path,sizeof(path)-1);
    if(n<0)return 0;
    path[n]=0;
#endif
    const char *base=strrchr(path,'/');
    return !strcmp(base?base+1:path,wanted);
}

size_t FAULT_WRITE(const void *data,size_t size,size_t count,FILE *stream) {
#if defined(__APPLE__)
    size_t (*real_write)(const void*,size_t,size_t,FILE*)=fwrite;
    int (*real_flush)(FILE*)=fflush;
#else
    size_t (*real_write)(const void*,size_t,size_t,FILE*)=dlsym(RTLD_NEXT,"fwrite");
    int (*real_flush)(FILE*)=dlsym(RTLD_NEXT,"fflush");
#endif
    if(target(stream,"write")) {
        fired=1;
        real_flush(stream);
        int fd=fileno(stream),saved=dup(fd);
#if defined(__APPLE__)
        int full=open("/dev/null",O_RDONLY); /* a real failing write, preserving ferror */
#else
        int full=open("/dev/full",O_WRONLY);
#endif
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

int FAULT_FLUSH(FILE *stream) {
#if defined(__APPLE__)
    int (*real_flush)(FILE*)=fflush;
#else
    int (*real_flush)(FILE*)=dlsym(RTLD_NEXT,"fflush");
#endif
    if(stream && target(stream,"flush")) {
        fired=1;real_flush(stream);errno=EIO;return EOF;
    }
    return real_flush(stream);
}

int FAULT_CLOSE(FILE *stream) {
#if defined(__APPLE__)
    int (*real_close)(FILE*)=fclose;
#else
    int (*real_close)(FILE*)=dlsym(RTLD_NEXT,"fclose");
#endif
    if(target(stream,"close")) {
        fired=1;real_close(stream);errno=EIO;return EOF;
    }
    return real_close(stream);
}

#if defined(__APPLE__)
#undef fwrite
#undef fflush
#undef fclose
// Native Mach-O interposition also works with chained-fixup executables.
__attribute__((used)) static const struct { const void *replacement, *original; }
interposers[] __attribute__((section("__DATA,__interpose"))) = {
    {(const void*)turbo_fault_fwrite, (const void*)fwrite},
    {(const void*)turbo_fault_fflush, (const void*)fflush},
    {(const void*)turbo_fault_fclose, (const void*)fclose}
};
#endif
