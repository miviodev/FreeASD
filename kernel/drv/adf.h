/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 */

#ifndef ASD_ADF_H
#define ASD_ADF_H

#include <stdint.h>
#include <stddef.h>
#include "../mm/mm.h"
#include "../ipc/ringbuf.h"
#include "../ipc/port.h"

/* ------------------------------------------------------------------------- */

#define ADF_ENTRY_MAGIC    0xADF00001U
#define ADF_API_VERSION    1
#define ADF_META_MAGIC     0x4144464D45544131ULL  /* "ADFMETA1" */
#define ADF_META_VERSION   1
#define ADF_MAX_COMPAT     8
#define ADF_NAME_LEN       64
#define MAX_MODULES        64

/* Bus types */
#define ADF_BUS_PCI        1
#define ADF_BUS_VIRTIO     2
#define ADF_BUS_PLATFORM   3

/* Device states */
#define ADF_STATE_PROBING   0
#define ADF_STATE_ACTIVE    1
#define ADF_STATE_DETACHED  2
#define ADF_STATE_SUSPENDED 3

/* Driver flags */
#define ADF_FLAG_PCI        (1u << 0)
#define ADF_FLAG_VIRTIO     (1u << 1)
#define ADF_FLAG_PLATFORM   (1u << 2)
#define ADF_FLAG_NETWORK    (1u << 3)
#define ADF_FLAG_STORAGE    (1u << 4)
#define ADF_FLAG_INPUT      (1u << 5)
#define ADF_FLAG_GRAPHICS   (1u << 6)

/* ------------------------------------------------------------------------- */

typedef struct {
    uint32_t bus_type;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t class_code;
    uint32_t subsystem_vendor;
    uint32_t subsystem_device;
    uint64_t _reserved;
} adf_compat_t;

/* ------------------------------------------------------------------------- */

typedef struct {
    uint64_t magic;
    uint16_t version;
    uint16_t api_version;
    uint32_t flags;
    char     name[ADF_NAME_LEN];
    char     author[64];
    uint32_t driver_version;
    uint32_t compat_count;
    uint32_t rx_slot_size;
    uint32_t rx_slot_count;
    uint32_t tx_slot_size;
    uint32_t tx_slot_count;
    uint32_t ctrl_shm_size;
    uint8_t  _reserved[84];
    /* adf_compat_t compat[] follows */
} adf_meta_t;

/* ------------------------------------------------------------------------- */

typedef struct adf_driver adf_driver_t;

typedef struct adf_dev {
    uint32_t       mod_id;
    uint32_t       bus_type;
    uint64_t       bus_addr;

    vaddr_t        rx_ring_va;
    vaddr_t        tx_ring_va;
    vaddr_t        ctrl_shm_va;
    port_t         ctrl_port;

    void          *priv;

    adf_driver_t  *entry;
    char           name[ADF_NAME_LEN];
    uint32_t       state;
    uint32_t       _pad;
} adf_dev_t;

/* ------------------------------------------------------------------------- */

struct adf_driver {
    uint32_t    adf_magic;
    uint16_t    adf_version;
    uint16_t    _pad;
    char        name[ADF_NAME_LEN];

    int       (*probe)  (adf_dev_t *dev);
    int       (*attach) (adf_dev_t *dev);
    void      (*detach) (adf_dev_t *dev);
    int       (*suspend)(adf_dev_t *dev);
    int       (*resume) (adf_dev_t *dev);
};

/* ------------------------------------------------------------------------- */

void       adf_init(void);
uint32_t   adf_load(const char *path);
int        adf_unload(uint32_t mod_id);
adf_dev_t *adf_find(const char *name);
void       adf_foreach(void (*cb)(adf_dev_t *dev, void *arg), void *arg);
void       adf_dispatch_irq(uint32_t vector);

#endif /* ASD_ADF_H */
