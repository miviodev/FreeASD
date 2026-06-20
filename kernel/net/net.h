#ifndef ASD_NET_H
#define ASD_NET_H

#include <stdint.h>
#include <stddef.h>

/* Static IP configuration for QEMU user networking (10.0.2.x) */
#define NET_IP_ADDR   0x0A00020FU   /* 10.0.2.15  */
#define NET_GW_ADDR   0x0A000202U   /* 10.0.2.2   */
#define NET_MASK      0xFFFFFF00U   /* /24        */

/* Called from virtio_net.c */
void net_rx_dispatch(const void *frame, uint16_t len);
void net_set_irq(uint8_t irq);

/* Network stack init — call after virtio_net_init() */
void net_init(void);

/* Send a UDP datagram */
int  net_udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                  const void *data, uint16_t len);

/* Receive a UDP datagram (kernel ring buffer → caller buffer).
 * Returns payload length or -1 if empty. */
int  net_udp_recv(void *buf, uint16_t buf_sz,
                  uint32_t *src_ip, uint16_t *src_port);

/* Send ICMP echo and wait for reply.
 * dst_ip in host byte order.  Fills *rtt_ms if non-NULL.
 * Returns 0 on success, -1 on timeout / no reply. */
int  net_icmp_ping(uint32_t dst_ip, uint32_t *rtt_ms);

#endif
