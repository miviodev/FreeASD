# Writing ADF Kernel Drivers

ADF (ASD Driver Framework) is the kernel's interface for loadable device drivers. A driver is a Mach-O binary with a special metadata section (`__ADF_META`) that the kernel reads to identify and load it.

---

## Overview

```
┌─────────────────────────────────────────────┐
│              Driver Binary (.adf)           │
│                                             │
│  __ADF_META section  ─── metadata struct   │
│  __text              ─── probe/attach/...  │
│  __data              ─── driver state      │
└──────────────────────────────┬──────────────┘
                               │ adf_load(path)
┌──────────────────────────────▼──────────────┐
│              ADF Subsystem (kernel)         │
│                                             │
│  adf_load() → parse meta → probe device    │
│  adf_dev_t  → holds device state           │
│  IRQ dispatch → adf_dispatch_irq()         │
└─────────────────────────────────────────────┘
```

---

## Driver structure

### Metadata (`adf_meta_t`)

Every driver binary must have an `adf_meta_t` at the start of the `__ADF_META` section:

```c
#define ADF_META_MAGIC  0x4144464D45544131ULL  /* "ADFMETA1" */
#define ADF_NAME_LEN    64

typedef struct {
    uint64_t magic;          /* ADF_META_MAGIC */
    uint16_t version;        /* ADF_META_VERSION (1) */
    uint16_t api_version;    /* ADF_API_VERSION (1) */
    uint32_t flags;          /* ADF_FLAG_* */
    char     name[ADF_NAME_LEN];
    char     author[64];
    uint32_t driver_version;
    uint32_t compat_count;   /* number of adf_compat_t entries following */
    uint32_t rx_slot_size;   /* ringbuf slot size for receive */
    uint32_t rx_slot_count;  /* ringbuf slot count for receive */
    uint32_t tx_slot_size;   /* ringbuf slot size for transmit */
    uint32_t tx_slot_count;  /* ringbuf slot count for transmit */
    uint32_t ctrl_shm_size;  /* control shared memory size */
    uint8_t  _reserved[84];
    /* adf_compat_t compat[compat_count] follows immediately */
} adf_meta_t;
```

### Compatibility table (`adf_compat_t`)

Immediately after the metadata struct, list the devices your driver supports:

```c
typedef struct {
    uint32_t bus_type;       /* ADF_BUS_PCI=1, ADF_BUS_VIRTIO=2, ADF_BUS_PLATFORM=3 */
    uint16_t vendor_id;      /* PCI vendor ID (0 = any) */
    uint16_t device_id;      /* PCI device ID (0 = any) */
    uint32_t subsystem;      /* PCI subsystem (0 = any) */
    uint32_t _pad;
} adf_compat_t;
```

### Driver entry (`adf_driver_t`)

```c
typedef struct {
    uint32_t  adf_magic;            /* ADF_DRIVER_MAGIC */
    uint16_t  adf_version;
    uint16_t  _pad;
    char      name[ADF_NAME_LEN];

    int     (*probe)  (adf_dev_t *dev);  /* 0 = this driver handles the device */
    int     (*attach) (adf_dev_t *dev);  /* 0 = success */
    void    (*detach) (adf_dev_t *dev);
    int     (*suspend)(adf_dev_t *dev);
    int     (*resume) (adf_dev_t *dev);
} adf_driver_t;
```

### Device context (`adf_dev_t`)

The kernel passes an `adf_dev_t` to every callback:

```c
typedef struct adf_dev {
    uint32_t   mod_id;       /* module ID assigned by kernel */
    uint32_t   bus_type;
    uint64_t   bus_addr;     /* PCI config address or MMIO base */

    vaddr_t    rx_ring_va;   /* kernel VA of receive ringbuf */
    vaddr_t    tx_ring_va;   /* kernel VA of transmit ringbuf */
    vaddr_t    ctrl_shm_va;  /* kernel VA of control shared memory */
    port_t     ctrl_port;    /* IPC port for control messages */

    void      *priv;         /* driver-private data (set in attach) */
    adf_driver_t *entry;
    char       name[ADF_NAME_LEN];
    uint32_t   state;        /* ADF_STATE_PROBING/ACTIVE/DETACHED/SUSPENDED */
} adf_dev_t;
```

**States:**
- `ADF_STATE_PROBING (0)` — probe in progress
- `ADF_STATE_ACTIVE (1)` — driver loaded and running
- `ADF_STATE_DETACHED (2)` — driver unloaded
- `ADF_STATE_SUSPENDED (3)` — device suspended

**Flags:**
```c
#define ADF_FLAG_PCI        (1 << 0)
#define ADF_FLAG_VIRTIO     (1 << 1)
#define ADF_FLAG_PLATFORM   (1 << 2)
#define ADF_FLAG_NETWORK    (1 << 8)
#define ADF_FLAG_STORAGE    (1 << 9)
#define ADF_FLAG_INPUT      (1 << 10)
#define ADF_FLAG_GRAPHICS   (1 << 11)
```

---

## Kernel symbols available to drivers

Drivers can call these kernel functions (resolved via ELF relocation):

```c
/* Memory */
void *kmalloc(size_t size);
void  kfree(void *ptr);

/* Ring buffers */
size_t ringbuf_required_size(uint32_t slot_size, uint32_t slot_count);
int    ringbuf_init(void *mem, uint32_t slot_size, uint32_t slot_count);
int    ringbuf_push(ringbuf_t *rb, const void *data, uint32_t size);
int    ringbuf_pop(ringbuf_t *rb, void *buf, uint32_t size);
int    ringbuf_empty(const ringbuf_t *rb);
int    ringbuf_full(const ringbuf_t *rb);

/* Shared memory */
vaddr_t asd_shmap(const char *name, size_t size, int flags);
void    asd_shunmap(vaddr_t va);

/* IPC ports */
port_t asd_port_open(const char *name, int flags);
void   asd_port_close(port_t port);
int    asd_port_send(port_t port, const void *data, size_t len);
int    asd_port_recv(port_t port, void *buf, size_t len, size_t *got_out);

/* Time */
uint64_t asd_time_ns(void);

/* Logging */
void serial_puts(const char *s);   /* driver debug output */
```

---

## Minimal driver example

A simple platform driver that logs when attached:

```c
/* my_driver.c */
#include <adf.h>
#include <stdint.h>
#include <stddef.h>

/* === Metadata section === */
__attribute__((section("__ADF_META,__adf_meta"), used))
static const struct {
    adf_meta_t   meta;
    adf_compat_t compat[1];
} g_adf = {
    .meta = {
        .magic          = 0x4144464D45544131ULL, /* ADF_META_MAGIC */
        .version        = 1,
        .api_version    = 1,
        .flags          = ADF_FLAG_PLATFORM,
        .name           = "my_driver",
        .author         = "Your Name",
        .driver_version = 0x00010000,
        .compat_count   = 1,
        .rx_slot_size   = 0,
        .rx_slot_count  = 0,
        .tx_slot_size   = 0,
        .tx_slot_count  = 0,
        .ctrl_shm_size  = 0,
    },
    .compat = {
        { .bus_type = 3 /* ADF_BUS_PLATFORM */, 0, 0, 0, 0 },
    },
};

/* === Driver state === */
typedef struct {
    int initialized;
} my_state_t;

/* === Callbacks === */

static int my_probe(adf_dev_t *dev) {
    /* Return 0 if we handle this device, non-zero to reject */
    (void)dev;
    return 0;   /* accept any platform device */
}

static int my_attach(adf_dev_t *dev) {
    my_state_t *s = kmalloc(sizeof(*s));
    if (!s) return -1;

    s->initialized = 1;
    dev->priv = s;

    serial_puts("[my_driver] attached successfully\n");
    return 0;
}

static void my_detach(adf_dev_t *dev) {
    my_state_t *s = dev->priv;
    if (s) {
        kfree(s);
        dev->priv = NULL;
    }
    serial_puts("[my_driver] detached\n");
}

static int my_suspend(adf_dev_t *dev) { (void)dev; return 0; }
static int my_resume(adf_dev_t *dev)  { (void)dev; return 0; }

/* === Driver entry === */
__attribute__((section("__ADF_ENTRY,__adf_entry"), used))
static const adf_driver_t g_entry = {
    .adf_magic   = 0x41444643, /* "ADFC" — ADF_DRIVER_MAGIC */
    .adf_version = 1,
    .name        = "my_driver",
    .probe       = my_probe,
    .attach      = my_attach,
    .detach      = my_detach,
    .suspend     = my_suspend,
    .resume      = my_resume,
};
```

---

## Building a driver

```makefile
CC     = clang
LD     = ld64.lld
KDIR   = /path/to/OpenASD/kernel

CFLAGS = --target=x86_64-apple-macosx13.0 \
         -ffreestanding -fno-builtin -nostdlib \
         -mno-red-zone -mno-sse -mno-sse2 -mno-avx \
         -I$(KDIR)/drv -I$(KDIR)/include \
         -Os

LDFLAGS = -arch x86_64 \
          -platform_version macos 13.0 13.0 \
          -undefined dynamic_lookup

my_driver.adf: my_driver.o
	$(LD) $(LDFLAGS) $< -o $@

my_driver.o: my_driver.c
	$(CC) $(CFLAGS) -c $< -o $@
```

Note: `-undefined dynamic_lookup` allows the driver to reference kernel symbols (`kmalloc`, `serial_puts`, etc.) that are resolved at load time.

---

## Loading a driver

From the kernel shell:
```sh
# Not yet a shell command in v1 — call adf_load() from kernel code
```

From kernel C code:
```c
uint32_t mod_id = adf_load("/boot/drivers/my_driver.adf");
if (!mod_id) {
    serial_puts("driver load failed\n");
}
```

---

## Network driver example

For a virtio-net style network driver:

```c
static int net_attach(adf_dev_t *dev) {
    /* Set up TX and RX ring buffers */
    ringbuf_t *rx_rb = (ringbuf_t *)dev->rx_ring_va;
    ringbuf_t *tx_rb = (ringbuf_t *)dev->tx_ring_va;

    /* Initialize hardware via MMIO */
    volatile uint32_t *regs = (volatile uint32_t *)dev->bus_addr;
    regs[0] = 0x1;   /* enable */

    dev->priv = /* driver state */;
    return 0;
}

/* Called from IRQ handler via adf_dispatch_irq() */
static void net_irq(adf_dev_t *dev) {
    ringbuf_t *rx = (ringbuf_t *)dev->rx_ring_va;
    uint8_t pkt[1514];
    while (ringbuf_pop(rx, pkt, sizeof(pkt)) == 0) {
        /* process received packet */
    }
}
```

---

## Tips

- **Always null-check** `kmalloc()` return values
- **Use `serial_puts()`** for debug logging — appears in QEMU serial output
- **Don't block** in `probe()` or `attach()` — these run during early boot with IRQs potentially masked
- **Store per-device state** in `dev->priv` (kmalloc it in attach, kfree in detach)
- **Ring buffer sizes** must be powers of 2 for both slot_size and slot_count
</content>
