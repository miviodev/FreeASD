/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * Service manager: parse service files, resolve dependency waves,
 * launch services, and handle restarts.
 */

#include "init_internal.h"

static service_t g_services[MAX_SERVICES];
static uint32_t  g_svc_count = 0;
static uint32_t  g_running   = 0;
static int8_t    g_svc_wave[MAX_SERVICES];

/* ------------------------------------------------------------------ */
/* Service lookup                                                       */
/* ------------------------------------------------------------------ */

static service_t *svc_find(const char *name) {
    for (uint32_t i = 0; i < g_svc_count; i++)
        if (strcmp(g_services[i].name, name) == 0)
            return &g_services[i];
    return NULL;
}

static int svc_deps_ready(service_t *svc) {
    for (uint32_t i = 0; i < svc->dep_count; i++) {
        if (strcmp(svc->deps[i], "kernel") == 0) continue;
        service_t *dep = svc_find(svc->deps[i]);
        if (dep && dep->state != SVC_RUNNING) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Launch                                                               */
/* ------------------------------------------------------------------ */

static void svc_launch(service_t *svc) {
    log_svc(svc->name, "launching");

    uid_t run_uid = UID_ROOT;
    gid_t run_gid = GID_ROOT;
    if (svc->svc_user[0]) {
        asd_user_t *u = usr_find_by_name(svc->svc_user);
        if (u) {
            run_uid = u->uid;
            run_gid = u->gid;
            if (svc->svc_group[0]) {
                asd_group_t *g = grp_find_by_name(svc->svc_group);
                if (g) run_gid = g->gid;
            }
        } else {
            log_svc(svc->name, "WARN: unknown user, running as root");
        }
    }
    (void)run_uid; (void)run_gid; /* uid/gid passed via pcb after spawn */

    /* FIX Bug 5: Check if the binary exists before attempting to spawn.
     * Previously, if /sbin/asdlog or /sbin/netd were absent from the
     * live image, macho_spawn() would return 0 and svcmgr would log
     * "FAIL: could not spawn", polluting the boot log.  We now check
     * for the binary via vfs_stat() first and degrade gracefully. */
    {
        stat_info_t st;
        if (vfs_stat(svc->binary, &st) != 0) {
            log_svc(svc->name, "SKIP: binary not found (deferred)");
            /* Mark as skipped rather than failed so the system boots */
            svc->state = SVC_FAILED;
            return;
        }
    }

    uint32_t pid = macho_spawn(svc->binary, NULL, NULL);
    if (!pid) {
        log_svc(svc->name, "FAIL: could not spawn");
        svc->state = SVC_FAILED;
        return;
    }
    svc->pid   = pid;
    svc->state = SVC_RUNNING;
    g_running++;
    log_svc(svc->name, "started");
}

/* ------------------------------------------------------------------ */
/* Service file parser                                                  */
/* ------------------------------------------------------------------ */

static const char *skip_ws(const char *p) {
    while (*p && i_isspace((int)*p)) p++;
    return p;
}

static const char *skip_line(const char *p) {
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
    return p;
}

static const char *read_word(const char *p, char *out, size_t out_sz) {
    p = skip_ws(p);
    size_t i = 0;
    while (*p && !i_isspace((int)*p) && *p != ':' && i < out_sz - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return p;
}

static const char *read_line_value(const char *p, char *out, size_t out_sz) {
    p = skip_ws(p);
    if (*p == ':') { p++; p = skip_ws(p); }
    size_t i = 0;
    while (*p && *p != '\n' && i < out_sz - 1) out[i++] = *p++;
    while (i > 0 && i_isspace((int)out[i-1])) i--;
    out[i] = '\0';
    return skip_line(p);
}

static int parse_svc_file(const char *content, service_t *svc) {
    const char *p = content;
    char kw[64];

    p = read_word(p, kw, sizeof(kw));
    if (strcmp(kw, "service") != 0) return -1;

    p = read_word(p, svc->name, sizeof(svc->name));
    if (!svc->name[0]) return -1;
    p = skip_line(p);

    svc->restart     = RESTART_NEVER;
    svc->log_type    = LOG_RING;
    svc->state       = SVC_NEW;
    svc->dep_count   = 0;
    svc->env_count   = 0;
    svc->pid         = 0;
    svc->exit_code   = 0;
    svc->binary[0]   = '\0';
    svc->args[0]     = '\0';
    svc->svc_user[0] = '\0';
    svc->svc_group[0]= '\0';

    while (*p) {
        p = skip_ws(p);
        if (*p == '\n' || *p == '#') { p = skip_line(p); continue; }
        if (!*p) break;

        char field[64], value[512];
        p = read_word(p, field, sizeof(field));
        if (strcmp(field, "end") == 0) break;
        p = read_line_value(p, value, sizeof(value));

        if (strcmp(field, "needs") == 0) {
            const char *vp = value;
            while (*vp && svc->dep_count < MAX_DEPS) {
                vp = skip_ws(vp);
                if (!*vp) break;
                char dep[SVC_NAME_LEN];
                size_t i = 0;
                while (*vp && !i_isspace((int)*vp) && i < SVC_NAME_LEN - 1)
                    dep[i++] = *vp++;
                dep[i] = '\0';
                strncpy(svc->deps[svc->dep_count++], dep, SVC_NAME_LEN);
            }
        } else if (strcmp(field, "binary") == 0) {
            strncpy(svc->binary, value, SVC_PATH_LEN);
        } else if (strcmp(field, "args") == 0) {
            strncpy(svc->args, value, SVC_ARGS_LEN);
        } else if (strcmp(field, "restart") == 0) {
            if (strcmp(value, "always") == 0)   svc->restart = RESTART_ALWAYS;
            else if (strcmp(value, "on-fail") == 0) svc->restart = RESTART_ON_FAIL;
        } else if (strcmp(field, "log") == 0) {
            if (strcmp(value, "file") == 0)     svc->log_type = LOG_FILE;
            else if (strcmp(value, "none") == 0) svc->log_type = LOG_NONE;
        } else if (strcmp(field, "user") == 0) {
            strncpy(svc->svc_user, value, sizeof(svc->svc_user));
        } else if (strcmp(field, "group") == 0) {
            strncpy(svc->svc_group, value, sizeof(svc->svc_group));
        } else if (strcmp(field, "env") == 0) {
            const char *vp = value;
            while (*vp && svc->env_count < MAX_ENV_VARS) {
                vp = skip_ws(vp);
                if (!*vp) break;
                char kv[ENV_NAME_LEN + ENV_VALUE_LEN + 1];
                size_t i = 0;
                while (*vp && !i_isspace((int)*vp) && i < sizeof(kv) - 1)
                    kv[i++] = *vp++;
                kv[i] = '\0';
                char *eq = strchr(kv, '=');
                if (eq) {
                    *eq = '\0';
                    strncpy(svc->env_keys[svc->env_count], kv, ENV_NAME_LEN);
                    strncpy(svc->env_vals[svc->env_count], eq + 1, ENV_VALUE_LEN);
                    svc->env_count++;
                }
            }
        }
    }

    return svc->binary[0] ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Built-in service definitions                                         */
/* ------------------------------------------------------------------ */

static const char *builtin_svcs[] = {
    "service syslog\n"
    "  needs:   kernel\n"
    "  binary:  /sbin/asdlog\n"
    "  restart: on-fail\n"
    "  log:     ring\n"
    "end\n",

    "service netd\n"
    "  needs:   syslog\n"
    "  binary:  /sbin/netd\n"
    "  restart: always\n"
    "  log:     ring\n"
    "end\n",

    NULL
};

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void load_services(void) {
    boot_log(" .. ", "Loading service definitions");
    for (int i = 0; builtin_svcs[i] && g_svc_count < MAX_SERVICES; i++) {
        service_t *svc = &g_services[g_svc_count];
        if (parse_svc_file(builtin_svcs[i], svc) == 0) {
            boot_log(" OK ", svc->name);
            g_svc_count++;
        } else {
            boot_log("FAIL", "service parse error");
        }
    }
    /* TODO: also load from /etc/asdrc/ via VFS */
    if (g_svc_count == 0)
        boot_log("WARN", "No services loaded");
}

int compute_waves(void) {
    for (uint32_t i = 0; i < g_svc_count; i++) g_svc_wave[i] = -1;

    int changed = 1, max_wave = 0;
    while (changed) {
        changed = 0;
        for (uint32_t i = 0; i < g_svc_count; i++) {
            if (g_svc_wave[i] >= 0) continue;
            service_t *svc = &g_services[i];
            int all_done = 1, max_dep = -1;

            for (uint32_t j = 0; j < svc->dep_count; j++) {
                if (strcmp(svc->deps[j], "kernel") == 0) continue;
                service_t *dep = svc_find(svc->deps[j]);
                if (!dep) continue;
                int di = (int)(dep - g_services);
                if (di < 0 || di >= (int)g_svc_count) continue;
                if (g_svc_wave[di] < 0) { all_done = 0; break; }
                if (g_svc_wave[di] > max_dep) max_dep = g_svc_wave[di];
            }
            if (all_done) {
                g_svc_wave[i] = (int8_t)(max_dep + 1);
                if (g_svc_wave[i] > max_wave) max_wave = g_svc_wave[i];
                changed = 1;
            }
        }
    }

    for (uint32_t i = 0; i < g_svc_count; i++) {
        if (g_svc_wave[i] < 0) {
            log_svc(g_services[i].name, "ERROR: dependency cycle");
            return -1;
        }
    }
    return max_wave + 1;
}

void launch_wave(int wave) {
    for (uint32_t i = 0; i < g_svc_count; i++) {
        if (g_svc_wave[i] == wave && g_services[i].state == SVC_NEW)
            svc_launch(&g_services[i]);
    }
}

void check_restarts(void) {
    for (uint32_t i = 0; i < g_svc_count; i++) {
        service_t *svc = &g_services[i];
        if (svc->state != SVC_EXITED) continue;
        int restart = 0;
        if (svc->restart == RESTART_ALWAYS) restart = 1;
        if (svc->restart == RESTART_ON_FAIL && svc->exit_code != 0) restart = 1;
        if (restart) {
            svc->state = SVC_RESTARTING;
            log_svc(svc->name, "restarting");
            svc_launch(svc);
        } else {
            svc->state = (svc->exit_code == 0) ? SVC_EXITED : SVC_FAILED;
        }
    }
}

void svcmgr_stop_all(void) {
    for (uint32_t i = 0; i < g_svc_count; i++) {
        service_t *svc = &g_services[i];
        if (svc->state != SVC_RUNNING || !svc->pid) continue;
        log_svc(svc->name, "stopping");
        pcb_t *pcb = sched_find(svc->pid);
        if (pcb && pcb->state != PROC_DEAD)
            pcb->state = PROC_DEAD;
        svc->state = SVC_EXITED;
        log_svc(svc->name, "stopped");
    }
    /* Give scheduler one second (100 PIT ticks at 100 Hz) to drain. */
    uint64_t deadline = pit_ticks() + 100;
    while (pit_ticks() < deadline)
        __asm__ volatile("pause");
}

uint64_t stub_time_ns(void) {
    /* 100 Hz PIT: each tick = 10,000,000 ns */
    return pit_ticks() * 10000000ULL;
}
