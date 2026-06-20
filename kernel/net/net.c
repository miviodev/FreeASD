/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * Minimal network stack for OpenASD:
 *   Ethernet → ARP (reply + cache) + IPv4 → UDP
 *
 * All state is static (no heap). Packet reception is polled from
 * the virtio-net ISR; transmission is synchronous.
 */

#include "net.h"
#include "../drv/virtio_net.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Minimal serial debug — uses I/O port helpers defined later in this file */
static inline uint8_t net_in8(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void net_out8(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port));
}
static void net_dbg(const char *s) {
    for (; *s; s++) {
        for (int t = 100000; t && !(net_in8(0x3F8+5) & 0x20); t--)
            __asm__ volatile("pause");
        net_out8(0x3F8, (uint8_t)*s);
    }
}

/* ------------------------------------------------------------------ */
/* Utility                                                              */
/* ------------------------------------------------------------------ */

static uint16_t htons(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}
static uint32_t htonl(uint32_t v) {
    return ((v & 0xFF000000u) >> 24) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x000000FFu) << 24);
}
#define ntohs htons
#define ntohl htonl

/* ------------------------------------------------------------------ */
/* Ethernet                                                             */
/* ------------------------------------------------------------------ */

#define ETH_ALEN 6
#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IP   0x0800

typedef struct {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t type;
} __attribute__((packed)) eth_hdr_t;

static uint8_t g_mac[ETH_ALEN];

static const uint8_t g_bcast[ETH_ALEN] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

/* ------------------------------------------------------------------ */
/* ARP                                                                  */
/* ------------------------------------------------------------------ */

#define ARP_CACHE_SIZE 8

typedef struct {
    uint8_t  hwtype_hi, hwtype_lo;   /* 0x0001 */
    uint8_t  proto_hi,  proto_lo;    /* 0x0800 */
    uint8_t  hwlen;                  /* 6 */
    uint8_t  prlen;                  /* 4 */
    uint8_t  op_hi, op_lo;
    uint8_t  sha[ETH_ALEN];
    uint8_t  spa[4];
    uint8_t  tha[ETH_ALEN];
    uint8_t  tpa[4];
} __attribute__((packed)) arp_pkt_t;

typedef struct {
    uint32_t ip;
    uint8_t  mac[ETH_ALEN];
    int      valid;
} arp_entry_t;

static arp_entry_t g_arp_cache[ARP_CACHE_SIZE];

static void arp_cache_store(uint32_t ip, const uint8_t *mac) {
    /* Update existing */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            memcpy(g_arp_cache[i].mac, mac, ETH_ALEN);
            return;
        }
    }
    /* Find empty slot */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp_cache[i].valid) {
            g_arp_cache[i].ip = ip;
            memcpy(g_arp_cache[i].mac, mac, ETH_ALEN);
            g_arp_cache[i].valid = 1;
            return;
        }
    }
    /* Evict slot 0 (simple LRU approximation) */
    g_arp_cache[0].ip = ip;
    memcpy(g_arp_cache[0].mac, mac, ETH_ALEN);
    g_arp_cache[0].valid = 1;
}

static int arp_lookup(uint32_t ip, uint8_t *mac_out) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            memcpy(mac_out, g_arp_cache[i].mac, ETH_ALEN);
            return 0;
        }
    }
    return -1;
}

static uint8_t g_frame_buf[1536];

static void arp_send_request(uint32_t target_ip) {
    uint8_t *p = g_frame_buf;
    eth_hdr_t *eth = (eth_hdr_t *)p;
    memcpy(eth->dst, g_bcast, ETH_ALEN);
    memcpy(eth->src, g_mac,   ETH_ALEN);
    eth->type = htons(ETHERTYPE_ARP);

    arp_pkt_t *arp = (arp_pkt_t *)(p + sizeof(eth_hdr_t));
    arp->hwtype_hi = 0; arp->hwtype_lo = 1;
    arp->proto_hi  = 0x08; arp->proto_lo = 0x00;
    arp->hwlen     = ETH_ALEN;
    arp->prlen     = 4;
    arp->op_hi     = 0; arp->op_lo = 1;  /* request */
    memcpy(arp->sha, g_mac, ETH_ALEN);
    uint32_t src_ip = htonl(NET_IP_ADDR);
    memcpy(arp->spa, &src_ip, 4);
    memset(arp->tha, 0, ETH_ALEN);
    uint32_t tgt = htonl(target_ip);
    memcpy(arp->tpa, &tgt, 4);

    virtio_net_send(g_frame_buf,
                    (uint16_t)(sizeof(eth_hdr_t) + sizeof(arp_pkt_t)));
}

static void arp_handle(const uint8_t *data, uint16_t len) {
    if (len < (uint16_t)sizeof(arp_pkt_t)) return;
    const arp_pkt_t *arp = (const arp_pkt_t *)data;

    uint32_t spa;
    memcpy(&spa, arp->spa, 4);
    spa = ntohl(spa);
    arp_cache_store(spa, arp->sha);

    /* Is it a request for our IP? */
    uint32_t tpa;
    memcpy(&tpa, arp->tpa, 4);
    tpa = ntohl(tpa);
    if (arp->op_lo != 1) return;                /* not a request */
    if (tpa != NET_IP_ADDR) return;

    /* Send ARP reply */
    uint8_t *p = g_frame_buf;
    eth_hdr_t *eth = (eth_hdr_t *)p;
    memcpy(eth->dst, arp->sha, ETH_ALEN);
    memcpy(eth->src, g_mac,    ETH_ALEN);
    eth->type = htons(ETHERTYPE_ARP);

    arp_pkt_t *rep = (arp_pkt_t *)(p + sizeof(eth_hdr_t));
    rep->hwtype_hi = 0; rep->hwtype_lo = 1;
    rep->proto_hi  = 0x08; rep->proto_lo = 0x00;
    rep->hwlen     = ETH_ALEN;
    rep->prlen     = 4;
    rep->op_hi     = 0; rep->op_lo = 2;  /* reply */
    memcpy(rep->sha, g_mac,    ETH_ALEN);
    uint32_t my_ip = htonl(NET_IP_ADDR);
    memcpy(rep->spa, &my_ip, 4);
    memcpy(rep->tha, arp->sha, ETH_ALEN);
    memcpy(rep->tpa, arp->spa, 4);

    virtio_net_send(g_frame_buf,
                    (uint16_t)(sizeof(eth_hdr_t) + sizeof(arp_pkt_t)));
}

/* ------------------------------------------------------------------ */
/* IPv4 / UDP                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  ihl_ver;
    uint8_t  dscp;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed)) ip4_hdr_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_hdr_t;

#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17

static uint16_t g_ip_id;  /* global IP packet ID counter, shared by UDP and ICMP */

static uint16_t ip_checksum(const void *ptr, size_t len) {
    const uint16_t *p = (const uint16_t *)ptr;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* ------------------------------------------------------------------ */
/* UDP receive ring buffer                                              */
/* ------------------------------------------------------------------ */

#define UDP_RING_SIZE   16
#define UDP_PAYLOAD_MAX 1472

typedef struct {
    uint8_t  data[UDP_PAYLOAD_MAX];
    uint16_t len;
    uint32_t src_ip;
    uint16_t src_port;
    uint8_t  valid;
} udp_slot_t;

static udp_slot_t g_udp_ring[UDP_RING_SIZE];
static uint16_t   g_udp_head;   /* next slot to write */
static uint16_t   g_udp_tail;   /* next slot to read */

static void udp_ring_push(uint32_t src_ip, uint16_t src_port,
                           const uint8_t *data, uint16_t len) {
    if (len > UDP_PAYLOAD_MAX) len = UDP_PAYLOAD_MAX;
    udp_slot_t *s = &g_udp_ring[g_udp_head % UDP_RING_SIZE];
    if (s->valid) return; /* overflow — drop */
    memcpy(s->data, data, len);
    s->len      = len;
    s->src_ip   = src_ip;
    s->src_port = src_port;
    s->valid    = 1;
    g_udp_head++;
}

int net_udp_recv(void *buf, uint16_t buf_sz,
                 uint32_t *src_ip, uint16_t *src_port) {
    /* Poll the virtio-net ISR to get fresh packets */
    virtio_net_rx_poll();

    udp_slot_t *s = &g_udp_ring[g_udp_tail % UDP_RING_SIZE];
    if (!s->valid) return -1;

    uint16_t n = s->len < buf_sz ? s->len : buf_sz;
    memcpy(buf, s->data, n);
    if (src_ip)   *src_ip   = s->src_ip;
    if (src_port) *src_port = s->src_port;
    s->valid = 0;
    g_udp_tail++;
    return (int)n;
}

/* ------------------------------------------------------------------ */
/* ICMP echo (ping)                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_hdr_t;

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

static volatile uint16_t g_ping_reply_id;
static volatile uint16_t g_ping_reply_seq;
static volatile int      g_ping_reply_ready;

static void icmp_handle(const uint8_t *data, uint16_t len) {
    if (len < (uint16_t)sizeof(icmp_hdr_t)) return;
    const icmp_hdr_t *ic = (const icmp_hdr_t *)data;
    if (ic->type != ICMP_ECHO_REPLY) return;
    g_ping_reply_id    = ntohs(ic->id);
    g_ping_reply_seq   = ntohs(ic->seq);
    g_ping_reply_ready = 1;
}

static int icmp_send_echo(uint32_t dst_ip, uint16_t id, uint16_t seq) {
    if (!virtio_net_ready()) return -1;
    uint8_t dst_mac[ETH_ALEN];
    uint32_t nexthop = ((dst_ip & NET_MASK) == (NET_IP_ADDR & NET_MASK))
                       ? dst_ip : NET_GW_ADDR;
    if (arp_lookup(nexthop, dst_mac) != 0) {
        arp_send_request(nexthop);
        /* Yield CPU to QEMU so it can process our ARP request and reply */
        for (int retry = 0; retry < 50; retry++) {
            __asm__ volatile("sti; hlt; cli" ::: "memory");
            virtio_net_rx_poll();
            if (arp_lookup(nexthop, dst_mac) == 0) break;
        }
        if (arp_lookup(nexthop, dst_mac) != 0) return -1;
    }

    static uint8_t pf[sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(icmp_hdr_t) + 8];
    uint8_t *p = pf;

    eth_hdr_t *eth = (eth_hdr_t *)p;
    memcpy(eth->dst, dst_mac, ETH_ALEN);
    memcpy(eth->src, g_mac,   ETH_ALEN);
    eth->type = htons(ETHERTYPE_IP);

    ip4_hdr_t *ip = (ip4_hdr_t *)(p + sizeof(eth_hdr_t));
    uint16_t ip_total = (uint16_t)(sizeof(ip4_hdr_t) + sizeof(icmp_hdr_t) + 8);
    ip->ihl_ver   = 0x45;
    ip->dscp      = 0;
    ip->total_len = htons(ip_total);
    ip->id        = htons(g_ip_id++);
    ip->frag_off  = 0;
    ip->ttl       = 64;
    ip->proto     = IP_PROTO_ICMP;
    ip->checksum  = 0;
    ip->src_ip    = htonl(NET_IP_ADDR);
    ip->dst_ip    = htonl(dst_ip);
    ip->checksum  = ip_checksum(ip, sizeof(ip4_hdr_t));

    icmp_hdr_t *ic = (icmp_hdr_t *)(p + sizeof(eth_hdr_t) + sizeof(ip4_hdr_t));
    ic->type     = ICMP_ECHO_REQUEST;
    ic->code     = 0;
    ic->checksum = 0;
    ic->id       = htons(id);
    ic->seq      = htons(seq);
    static const uint8_t ping_data[8] = {'O','p','e','n','A','S','D',0};
    memcpy((uint8_t *)ic + sizeof(icmp_hdr_t), ping_data, 8);
    ic->checksum = ip_checksum(ic, sizeof(icmp_hdr_t) + 8);

    return virtio_net_send(pf, (uint16_t)sizeof(pf));
}

int net_icmp_ping(uint32_t dst_ip, uint32_t *rtt_ms) {
    static uint16_t s_id  = 0x4153;
    static uint16_t s_seq = 0;
    uint16_t id = s_id, seq = ++s_seq;

    g_ping_reply_ready = 0;
    /* Resolve ARP. The gateway MAC should already be in cache from net_init().
     * If not (first ping after boot), send ARP request and poll. */
    uint32_t nexthop = ((dst_ip & NET_MASK) == (NET_IP_ADDR & NET_MASK))
                       ? dst_ip : NET_GW_ADDR;
    uint8_t probe_mac[6];

    for (int attempt = 0; attempt < 30; attempt++) {
        if (arp_lookup(nexthop, probe_mac) == 0) break;
            arp_send_request(nexthop);
        /* Poll RX: give QEMU time to process ARP and send reply */
        for (int h = 0; h < 10; h++) {
            __asm__ volatile("sti; hlt; cli" ::: "memory");
            virtio_net_rx_poll();
            if (arp_lookup(nexthop, probe_mac) == 0) goto arp_ok;
        }
    }
    if (arp_lookup(nexthop, probe_mac) != 0) return -1;
arp_ok:
    if (icmp_send_echo(dst_ip, id, seq) != 0) return -1;

    /*
     * Wait for reply.  Use "sti; hlt; cli" to yield the CPU to QEMU
     * so it can process our ICMP echo and deliver the reply.
     * The PIT fires at 100 Hz, so each hlt sleeps at most 10 ms.
     * 100 iterations = ~1 second timeout per ping.
     */
    for (uint32_t i = 0; i < 100; i++) {
        __asm__ volatile("sti; hlt; cli" ::: "memory");
        virtio_net_rx_poll();
        if (g_ping_reply_ready &&
            g_ping_reply_id == id && g_ping_reply_seq == seq) {
            g_ping_reply_ready = 0;
            /* Each iteration ≈ 1 timer tick = 10 ms at 100 Hz */
            if (rtt_ms) *rtt_ms = i * 10 < 1 ? 1 : i * 10;
            return 0;
        }
    }
    g_ping_reply_ready = 0;
    return -1;
}

/* ------------------------------------------------------------------ */
/* IPv4 dispatch                                                        */
/* ------------------------------------------------------------------ */

static void ip4_handle(const uint8_t *data, uint16_t len) {
    if (len < (uint16_t)sizeof(ip4_hdr_t)) return;
    const ip4_hdr_t *ip = (const ip4_hdr_t *)data;
    uint8_t ihl = (ip->ihl_ver & 0x0F) << 2;
    if (ihl < sizeof(ip4_hdr_t) || ihl > len) return;

    uint32_t dst = ntohl(ip->dst_ip);
    if (dst != NET_IP_ADDR && dst != 0xFFFFFFFFu) return;

    const uint8_t *pl = data + ihl;
    uint16_t plen = (uint16_t)(len - ihl);

    if (ip->proto == IP_PROTO_ICMP) {
        icmp_handle(pl, plen);
        return;
    }
    if (ip->proto != IP_PROTO_UDP) return;

    if (plen < (uint16_t)sizeof(udp_hdr_t)) return;
    const udp_hdr_t *udp = (const udp_hdr_t *)pl;
    uint16_t payload_len = (uint16_t)(ntohs(udp->length) - sizeof(udp_hdr_t));
    if (payload_len > plen - sizeof(udp_hdr_t))
        payload_len = (uint16_t)(plen - sizeof(udp_hdr_t));
    udp_ring_push(ntohl(ip->src_ip), ntohs(udp->src_port),
                  pl + sizeof(udp_hdr_t), payload_len);
}

/* ------------------------------------------------------------------ */
/* Ethernet dispatch (called from virtio_net_rx_poll)                  */
/* ------------------------------------------------------------------ */

void net_rx_dispatch(const void *frame, uint16_t len) {
    if (len < (uint16_t)sizeof(eth_hdr_t)) return;
    const eth_hdr_t *eth = (const eth_hdr_t *)frame;
    uint16_t type = ntohs(eth->type);
    const uint8_t *payload = (const uint8_t *)frame + sizeof(eth_hdr_t);
    uint16_t plen  = (uint16_t)(len - sizeof(eth_hdr_t));

    if (type == ETHERTYPE_ARP) {
        arp_handle(payload, plen);
    } else if (type == ETHERTYPE_IP) {
        ip4_handle(payload, plen);
    }
}

/* ------------------------------------------------------------------ */
/* UDP send                                                             */
/* ------------------------------------------------------------------ */

int net_udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const void *data, uint16_t len) {
    if (!virtio_net_ready()) return -1;
    if (len > UDP_PAYLOAD_MAX) return -1;

    /* Resolve destination MAC */
    uint8_t dst_mac[ETH_ALEN];
    uint32_t nexthop = dst_ip;
    /* If dst is not on our subnet, use gateway */
    if ((dst_ip & NET_MASK) != (NET_IP_ADDR & NET_MASK))
        nexthop = NET_GW_ADDR;

    if (arp_lookup(nexthop, dst_mac) != 0) {
        /* Send ARP request and hope the reply arrives before next poll */
        arp_send_request(nexthop);
        /* Poll RX to pick up the ARP reply */
        virtio_net_rx_poll();
        if (arp_lookup(nexthop, dst_mac) != 0)
            return -1;  /* no reply yet */
    }

    /* Build frame */
    uint8_t *p = g_frame_buf;
    eth_hdr_t *eth = (eth_hdr_t *)p;
    memcpy(eth->dst, dst_mac, ETH_ALEN);
    memcpy(eth->src, g_mac,   ETH_ALEN);
    eth->type = htons(ETHERTYPE_IP);

    ip4_hdr_t *ip = (ip4_hdr_t *)(p + sizeof(eth_hdr_t));
    uint16_t ip_total = (uint16_t)(sizeof(ip4_hdr_t) + sizeof(udp_hdr_t) + len);
    ip->ihl_ver   = 0x45;
    ip->dscp      = 0;
    ip->total_len = htons(ip_total);
    ip->id        = htons(g_ip_id++);
    ip->frag_off  = 0;
    ip->ttl       = 64;
    ip->proto     = IP_PROTO_UDP;
    ip->checksum  = 0;
    ip->src_ip    = htonl(NET_IP_ADDR);
    ip->dst_ip    = htonl(dst_ip);
    ip->checksum  = ip_checksum(ip, sizeof(ip4_hdr_t));

    udp_hdr_t *udp = (udp_hdr_t *)(p + sizeof(eth_hdr_t) + sizeof(ip4_hdr_t));
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons((uint16_t)(sizeof(udp_hdr_t) + len));
    udp->checksum = 0;  /* optional for UDP/IPv4 */
    memcpy((uint8_t *)udp + sizeof(udp_hdr_t), data, len);

    uint16_t frame_len = (uint16_t)(sizeof(eth_hdr_t) + ip_total);
    return virtio_net_send(g_frame_buf, frame_len);
}

/* ------------------------------------------------------------------ */
/* IRQ routing hook (called from virtio_net_init)                       */
/* ------------------------------------------------------------------ */

static uint8_t g_net_irq = 0xFF;

void net_set_irq(uint8_t irq) {
    g_net_irq = irq;
}

uint8_t net_get_irq(void) {
    return g_net_irq;
}

/* ------------------------------------------------------------------ */
/* Init                                                                 */
/* ------------------------------------------------------------------ */

void net_init(void) {
    virtio_net_get_mac(g_mac);
    memset(g_arp_cache, 0, sizeof(g_arp_cache));
    memset(g_udp_ring,  0, sizeof(g_udp_ring));
    g_udp_head = 0;
    g_udp_tail = 0;
    g_ip_id    = 1;

    /* Probe ARP for gateway so it's in cache immediately */
    if (virtio_net_ready())
        arp_send_request(NET_GW_ADDR);
}
