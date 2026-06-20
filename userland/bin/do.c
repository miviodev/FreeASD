/* do — run command as root (wheel users only) for OpenASD.
 *
 * Usage: do <command> [args...]
 *
 * Checks if the current user is in the wheel group by attempting
 * setuid(0) — the kernel allows this for wheel users. If successful,
 * executes the specified command with root privileges.
 */
#include <asd/syscall.h>
#include <stdint.h>

static void wstr(int fd, const char *s) {
    int n = 0; while (s[n]) n++;
    if (n) asd_write(fd, s, (size_t)n);
}

int main(int argc, const char **argv) {
    if (argc < 2) {
        wstr(2, "usage: do <command> [args...]\n");
        wstr(2, "  Runs command as root. Requires wheel group membership.\n");
        asd_exit(1);
    }

    unsigned int uid = asd_getuid();
    if (uid != 0) {
        /* Attempt privilege escalation via setuid(0).
         * The kernel grants this only if the calling process's user
         * has the USR_FLAG_WHEEL bit set in the user database. */
        if (asd_setuid(0) != 0) {
            wstr(2, "do: permission denied: not in wheel group\n");
            asd_exit(1);
        }
    }

    /* Resolve the command path */
    char binpath[256];
    const char *cmd = argv[1];
    int is_abs = (cmd[0] == '/');
    if (is_abs) {
        int n = 0; while (cmd[n] && n < 255) { binpath[n] = cmd[n]; n++; } binpath[n] = 0;
    } else {
        /* try /bin/<cmd> first */
        int n = 0;
        const char *prefix = "/bin/";
        while (*prefix && n < 250) binpath[n++] = *prefix++;
        while (*cmd && n < 255) binpath[n++] = *cmd++;
        binpath[n] = 0;
    }

    /* Build argv: skip argv[0] ("do"), pass argv[1..] to child */
    const char **child_argv = argv + 1;

    int pid = asd_spawn(binpath, child_argv, 0);
    if (pid < 0) {
        wstr(2, "do: command not found: ");
        wstr(2, binpath);
        wstr(2, "\n");
        asd_exit(127);
    }

    asd_exit_info_t info;
    asd_wait(pid, &info);
    asd_exit(info.exit_code);
    return 0;
}
