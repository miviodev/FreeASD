/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef uint32_t port_t;
typedef uint32_t mod_id_t;

#define DCTL_MSG_LOAD    1
#define DCTL_MSG_UNLOAD  2
#define DCTL_MSG_LIST    3
#define DCTL_MSG_STATUS  4
#define DCTL_MSG_PROBE   5

#define DCTL_REPLY_OK    0x80
#define DCTL_REPLY_ERR   0x81

#define DCTL_PORT_NAME   "kernel.drvmgr"
#define MSG_PAYLOAD_MAX  200

typedef struct {
    uint32_t type;
    uint32_t len;
    char     payload[MSG_PAYLOAD_MAX];
} dctl_msg_t;

typedef struct {
    uint32_t type;
    uint32_t code;     /* 0 = success */
    char     data[200];
} dctl_reply_t;

typedef struct {
    const char *path;
    const char *name;
} dctl_args_t;

static void parse_args(int argc, char **argv, dctl_args_t *out) {
    memset(out, 0, sizeof(*out));
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], ":path") == 0 && i + 1 < argc)
            out->path = argv[++i];
        else if (strcmp(argv[i], ":name") == 0 && i + 1 < argc)
            out->name = argv[++i];
    }
}

static port_t dctl_open_port(void) {
    /* asd_port_open(DCTL_PORT_NAME, PORT_OPEN) */
    /* Stub: return a non-zero handle */
    return 1;
}

static int dctl_send(port_t p, const dctl_msg_t *msg) {
    (void)p; (void)msg;
    /* asd_port_send(p, msg, sizeof(*msg)) */
    return 0;
}

static int dctl_recv(port_t p, dctl_reply_t *reply) {
    (void)p;
    /* asd_port_recv(p, reply, sizeof(*reply), NULL) */
    /* Stub: fabricate a success reply */
    reply->type = DCTL_REPLY_OK;
    reply->code = 0;
    reply->data[0] = '\0';
    return 0;
}

static int cmd_list(port_t p) {
    dctl_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = DCTL_MSG_LIST;
    msg.len  = 0;

    if (dctl_send(p, &msg) != 0) {
        fprintf(stderr, "dctl: send failed\n");
        return 1;
    }

    dctl_reply_t reply;
    if (dctl_recv(p, &reply) != 0 || reply.code != 0) {
        fprintf(stderr, "dctl: list failed: %s\n", reply.data);
        return 1;
    }

    /* Reply data is a newline-separated list of "id name state" triplets */
    if (reply.data[0] == '\0') {
        printf("(no modules loaded)\n");
    } else {
        printf("%-6s %-24s %s\n", "ID", "NAME", "STATE");
        printf("%-6s %-24s %s\n", "------", "------------------------",
               "----------");
        printf("%s\n", reply.data);
    }
    return 0;
}

static int cmd_load(port_t p, const char *path) {
    if (!path) {
        fprintf(stderr, "dctl: load requires :path\n");
        return 2;
    }

    dctl_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = DCTL_MSG_LOAD;
    strncpy(msg.payload, path, MSG_PAYLOAD_MAX - 1);
    msg.len  = (uint32_t)strlen(msg.payload) + 1;

    if (dctl_send(p, &msg) != 0) {
        fprintf(stderr, "dctl: send failed\n");
        return 1;
    }

    dctl_reply_t reply;
    if (dctl_recv(p, &reply) != 0) {
        fprintf(stderr, "dctl: recv failed\n");
        return 1;
    }
    if (reply.code != 0) {
        fprintf(stderr, "dctl: load failed: %s\n", reply.data);
        return 1;
    }

    printf("dctl: loaded %s (id %s)\n", path, reply.data);
    return 0;
}

static int cmd_unload(port_t p, const char *name) {
    if (!name) {
        fprintf(stderr, "dctl: unload requires :name\n");
        return 2;
    }

    dctl_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = DCTL_MSG_UNLOAD;
    strncpy(msg.payload, name, MSG_PAYLOAD_MAX - 1);
    msg.len  = (uint32_t)strlen(msg.payload) + 1;

    if (dctl_send(p, &msg) != 0) {
        fprintf(stderr, "dctl: send failed\n");
        return 1;
    }

    dctl_reply_t reply;
    if (dctl_recv(p, &reply) != 0) {
        fprintf(stderr, "dctl: recv failed\n");
        return 1;
    }
    if (reply.code != 0) {
        fprintf(stderr, "dctl: unload failed: %s\n", reply.data);
        return 1;
    }

    printf("dctl: unloaded %s\n", name);
    return 0;
}

static int cmd_status(port_t p, const char *name) {
    if (!name) {
        fprintf(stderr, "dctl: status requires :name\n");
        return 2;
    }

    dctl_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = DCTL_MSG_STATUS;
    strncpy(msg.payload, name, MSG_PAYLOAD_MAX - 1);
    msg.len  = (uint32_t)strlen(msg.payload) + 1;

    if (dctl_send(p, &msg) != 0) {
        fprintf(stderr, "dctl: send failed\n");
        return 1;
    }

    dctl_reply_t reply;
    if (dctl_recv(p, &reply) != 0) {
        fprintf(stderr, "dctl: recv failed\n");
        return 1;
    }
    if (reply.code != 0) {
        fprintf(stderr, "dctl: status failed: %s\n", reply.data);
        return 1;
    }

    printf("%s\n", reply.data);
    return 0;
}

static int cmd_probe(port_t p) {
    dctl_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = DCTL_MSG_PROBE;
    msg.len  = 0;

    if (dctl_send(p, &msg) != 0) {
        fprintf(stderr, "dctl: send failed\n");
        return 1;
    }

    dctl_reply_t reply;
    if (dctl_recv(p, &reply) != 0) {
        fprintf(stderr, "dctl: recv failed\n");
        return 1;
    }
    if (reply.code != 0) {
        fprintf(stderr, "dctl: probe failed: %s\n", reply.data);
        return 1;
    }

    printf("dctl: probe complete\n%s", reply.data);
    return 0;
}

static void usage(void) {
    fprintf(stderr,
        "usage:\n"
        "  dctl list\n"
        "  dctl load   :path /lib/drv/<name>.adf\n"
        "  dctl unload :name <name>\n"
        "  dctl status :name <name>\n"
        "  dctl probe\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 2; }

    const char *cmd = argv[1];

    port_t p = dctl_open_port();
    if (!p) {
        fprintf(stderr, "dctl: cannot open kernel.drvmgr port\n");
        return 1;
    }

    dctl_args_t args;
    parse_args(argc, argv, &args);

    int rc;
    if (strcmp(cmd, "list") == 0)
        rc = cmd_list(p);
    else if (strcmp(cmd, "load") == 0)
        rc = cmd_load(p, args.path);
    else if (strcmp(cmd, "unload") == 0)
        rc = cmd_unload(p, args.name);
    else if (strcmp(cmd, "status") == 0)
        rc = cmd_status(p, args.name);
    else if (strcmp(cmd, "probe") == 0)
        rc = cmd_probe(p);
    else {
        fprintf(stderr, "dctl: unknown command: %s\n", cmd);
        usage();
        rc = 2;
    }

    return rc;
}
