#include "common.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    puts("OpenASD Microkernel OS");
    puts("Version: 1.0");
    puts("Build Date: May 13, 2026");
    
    uint64_t t = asd_time();
    printf("System Uptime: %lld ms\n", (long long)(t / 1000000));
    
    int pid = asd_getpid();
    printf("Current PID: %d\n", pid);
    
    return 0;
}


// added getcwd syscall
int sys_getcwd(char *buf, int size) {
    const char *cwd = "/"; // TODO real per-process cwd
    int i=0;
    while (cwd[i] && i < size-1) { buf[i]=cwd[i]; i++; }
    buf[i]=0;
    return i;
}
