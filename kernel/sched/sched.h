/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 */

#ifndef ASDHED_SCHED_H
#define ASDHED_SCHED_H

#include <stdint.h>
#include "../mm/mm.h"
#include "../usr/usr.h"

#define MAX_PROCS           4096
#define MAX_CPUS            64
#define SCHED_STEAL_THRESHOLD   2
#define SCHED_STEAL_IDLE_NS     500000ULL
#define SCHED_QUANTUM_RT_NS     0ULL
#define SCHED_QUANTUM_INTR_NS   4000000ULL
#define SCHED_QUANTUM_BATCH_NS  50000000ULL
#define SCHED_BOOST             20
#define SCHED_NAME_LEN          32
#define MAX_FDS                 256

typedef uint8_t sched_class_t;
#define SCHED_RT    0
#define SCHED_INTR  1
#define SCHED_BATCH 2

#define PROC_NEW    0
#define PROC_READY  1
#define PROC_RUN    2
#define PROC_WAIT   3
#define PROC_DEAD   4

typedef struct rb_node {
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;
    int             color;
} rb_node_t;

typedef struct {
    rb_node_t *root;
} rb_root_t;

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr3;
    uint64_t fs_base;
    uint64_t cs;   /* code segment selector: 0x08 kernel / 0x1B user */
    uint64_t ss;   /* stack segment selector: 0x10 kernel / 0x23 user */
} cpu_context_t;

/* ------------------------------------------------------------------------- */

typedef uint32_t pid_t;
typedef uint32_t port_t;

struct vmap;   /* forward — defined in mm/mm.h */

typedef struct pcb {
    pid_t          pid;
    uint8_t        state;         /* PROC_* */
    sched_class_t  sched_class;
    uint8_t        cpu;
    uint8_t        _pad0;

    int16_t        priority;      /* effective (dynamic) */
    int16_t        base_pri;      /* set by asd_sched_set */

    uint64_t       cpu_ns;        /* total CPU time consumed */
    uint64_t       wake_ns;       /* monotonic ns of last I/O wakeup */
    uint64_t       deadline_ns;   /* absolute ns of next preemption */

    struct vmap   *va_map;
    port_t        *port_list;     /* head of owned ports */

    int32_t        exit_code;
    char           name[SCHED_NAME_LEN];

    /* Identity — set at spawn time, checked by VFS and IPC */
    uid_t          uid;            /* effective user ID  */
    gid_t          gid;            /* effective group ID */
    uint32_t       groups;         /* supplementary group bitmask (bit N = gid N) */

    /* Parent PID and signal state */
    pid_t          ppid;           /* parent PID (for SIGCHLD delivery) */
    uint32_t       sig_pending;    /* bitmask: bit N = signal N is pending */

    /* Current working directory — inherited by children, updated via SYS_CHDIR */
    char           cwd[256];

    /* File descriptor table — opaque pointers to vfsnode_t */
    void          *fd_table[MAX_FDS];

    /* Per-process heap state for sys_mmap / sys_brk */
    uint64_t       mmap_bump;   /* next anonymous mmap address */
    uint64_t       brk_cur;    /* current program break */

    /* Per-thread kernel stack used by SYSCALL and IRQ from ring 3 */
    uint8_t       *kstack;
    uint64_t       kstack_top;

    cpu_context_t  regs;

    rb_node_t      rq_node;       /* node in run queue red-black tree */

    struct pcb    *reap_next;     /* intrusive list for reaped procs */
} pcb_t;

/* ------------------------------------------------------------------------- */

typedef struct {
    uint32_t  lock;           /* spinlock */
    uint8_t   cpu_id;
    uint8_t   _pad[7];

    rb_root_t rt_tree;
    rb_root_t intr_tree;
    rb_root_t batch_tree;

    uint32_t  rt_count;
    uint32_t  intr_count;
    uint32_t  batch_count;

    pcb_t    *current;
    pcb_t    *idle_pcb;

    uint64_t  steal_ns;
    uint64_t  total_ns;
    uint64_t  idle_ns;
} runq_t;

/* ------------------------------------------------------------------------- */

#define SCHED_STATE_MAGIC 0x5348454DU  /* "SHED" */

typedef struct {
    uint32_t magic;
    uint32_t cpu_count;
    struct {
        uint32_t rt_count;
        uint32_t intr_count;
        uint32_t batch_count;
        uint32_t current_pid;
        uint64_t idle_ns;
        uint64_t busy_ns;
    } cpus[MAX_CPUS];
    uint32_t total_procs;
    uint32_t seq;   /* seqlock counter — odd while being written */
} sched_state_t;

/* ------------------------------------------------------------------------- */

typedef struct {
    const char    *binary;
    const char   **argv;
    const char   **envp;
    sched_class_t  sched_class;
    int            priority;
    uint8_t        cpu_affinity;  /* 0xFF = no affinity */
    struct vmap   *va_map;        /* pre-built vmap from elf_spawn; NULL = alloc new */
} spawn_args_t;

/* ------------------------------------------------------------------------- */

typedef struct {
    int32_t  exit_code;
    uint64_t cpu_ns;
    uint64_t wall_ns;
} exit_info_t;

/* ------------------------------------------------------------------------- */

#define SCHED_KSTACK_SIZE           (64 * 1024)
/* PID 1 / installer: deep TUI call chains + IRQ frames on the same stack */
#define SCHED_BOOTSTRAP_KSTACK_SIZE (2 * 1024 * 1024)

/* Initialise scheduler — called once by kernel_main */
void sched_init(uint32_t cpu_count);

/* Run PID 1 (asdinit) on a real scheduler thread stack, not the boot stack. */
void sched_bootstrap_init_thread(void);
void sched_bootstrap_enter(void (*fn)(void)) __attribute__((noreturn));

/* Called by each AP after it comes online */
void sched_cpu_init(uint32_t cpu_id);

/* Create a new process; returns pid or 0 on failure */
pid_t sched_spawn(const spawn_args_t *args);

/* Terminate the calling process */
void  sched_exit(int code) __attribute__((noreturn));

/* Block until pid exits, fill *info; returns 0 or -1 */
int   sched_reap(pid_t pid, exit_info_t *info);

/* Change scheduling class / priority */
int   sched_set(pid_t pid, sched_class_t cls, int priority);

/* Yield the remaining quantum */
void  sched_yield(void);

/* Called from timer interrupt — may trigger preemption */
void  sched_tick(uint64_t now_ns);

/* Wake a process from PROC_WAIT (called by I/O subsystem) */
void  sched_wake(pcb_t *pcb);

/* Enqueue a PROC_READY process on the appropriate CPU's run queue */
void  sched_enqueue(pcb_t *pcb);

/* Dequeue and return the next process to run on this CPU */
pcb_t *sched_dequeue(runq_t *rq);

/* Work stealing — called when a CPU's run queue is empty */
void  sched_steal(uint32_t this_cpu);

/* Update shared state page */
void  sched_state_update(void);

/* Return pointer to the per-CPU run queue */
runq_t *sched_runq(uint32_t cpu_id);

/* Return the PCB for a given PID; returns NULL if not found */
pcb_t  *sched_find(pid_t pid);

/* Return the current process on this CPU */
pcb_t  *sched_current(void);

#endif /* ASDHED_SCHED_H */
