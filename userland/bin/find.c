/* find — recursive directory search for OpenASD */
#include <asd/syscall.h>
#include <stdint.h>

static char g_path[512];
static int  flag_type = 0;   /* 0=any, 'f'=file, 'd'=dir */
static const char *name_pat  = 0;

static void wstr(const char *s) {
    int n = 0; while (s[n]) n++;
    if (n) asd_write(1, s, (size_t)n);
}

static void wline(const char *s) { wstr(s); asd_write(1, "\n", 1); }

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int smatch(const char *name, const char *pat) {
    /* simple glob: '*' matches anything */
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return 1;
            while (*name) {
                if (smatch(name, pat)) return 1;
                name++;
            }
            return 0;
        }
        if (!*name || *name != *pat) return 0;
        name++; pat++;
    }
    return *name == 0;
}

static void do_find(char *path, int depth) {
    if (depth > 64) return;
    asd_dirent_t ents[64];
    uint32_t n = 0;
    if (asd_readdir(path, ents, 64, &n) != 0) return;
    int plen = slen(path);
    for (uint32_t i = 0; i < n; i++) {
        const char *nm = ents[i].name;
        if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
        /* build full path */
        char full[512];
        int nl = slen(nm);
        if (plen + 1 + nl >= 511) continue;
        int k = 0;
        for (int j = 0; j < plen; j++) full[k++] = path[j];
        if (plen > 1 || path[0] != '/') full[k++] = '/';
        for (int j = 0; j < nl; j++) full[k++] = nm[j];
        full[k] = 0;

        int is_dir = (ents[i].kind == ASD_NODE_DIR);
        int type_ok = (flag_type == 0)
                   || (flag_type == 'f' && !is_dir)
                   || (flag_type == 'd' && is_dir);
        int name_ok = (!name_pat) || smatch(nm, name_pat);

        if (type_ok && name_ok) wline(full);
        if (is_dir) do_find(full, depth + 1);
    }
}

int main(int argc, const char **argv) {
    const char *root = ".";
    int argi = 1;

    /* first non-flag arg may be dir */
    if (argi < argc && argv[argi][0] != '-') root = argv[argi++];

    while (argi < argc) {
        if (argi + 1 < argc) {
            if (!__builtin_strcmp(argv[argi], "-name"))  { name_pat = argv[argi+1]; argi += 2; continue; }
            if (!__builtin_strcmp(argv[argi], "-type"))  {
                flag_type = argv[argi+1][0]; argi += 2; continue;
            }
        }
        wstr("find: unknown option: "); wline(argv[argi]);
        argi++;
    }

    /* resolve "." to actual cwd */
    char cwd[256];
    if (root[0] == '.' && root[1] == 0) {
        int r = asd_getcwd(cwd, sizeof(cwd));
        if (r > 0) { cwd[r] = 0; root = cwd; }
    }

    int rl = slen(root);
    if (rl >= (int)sizeof(g_path) - 1) { wline("find: path too long"); asd_exit(1); }
    for (int i = 0; i < rl; i++) g_path[i] = root[i];
    g_path[rl] = 0;

    /* print the root itself if it matches */
    {
        asd_stat_t st;
        if (asd_stat(g_path, &st) == 0) {
            int is_dir = (st.kind == ASD_NODE_DIR);
            int type_ok = (flag_type == 0)
                       || (flag_type == 'f' && !is_dir)
                       || (flag_type == 'd' && is_dir);
            const char *nm = g_path;
            /* get basename */
            int gl = slen(g_path);
            for (int i = gl - 1; i >= 0; i--) if (g_path[i] == '/') { nm = g_path + i + 1; break; }
            int name_ok = (!name_pat) || smatch(nm, name_pat);
            if (type_ok && name_ok) wline(g_path);
        }
    }

    do_find(g_path, 0);
    asd_exit(0);
    return 0;
}
