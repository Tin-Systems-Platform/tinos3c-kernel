#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>
#include "pci_classes.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

typedef struct {
    uint64_t base;
    uint64_t size;

    bool is_io;
    bool is_64bit;
    bool prefetchable;
} pci_bar_t;

typedef struct pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t revision;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;

    uint8_t header_type;
    uint8_t irq_line;
    uint8_t irq_pin;

    pci_bar_t bars[6];
    uint8_t bar_count;

    uint8_t msi_capability;
    uint8_t msi_64bit;
    uint8_t msi_multiple_message_capable;
    uint8_t msi_enabled;

    struct pci_driver *driver;

    struct pci_device *next;
} pci_device_t;

typedef int (*pci_probe_fn)(pci_device_t *device);

typedef struct pci_driver {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    pci_probe_fn probe;
    const char *name;
    struct pci_driver *next;
} pci_driver_t;

/**
 * Enumerate PCI devices and bind registered drivers.
 * @date 2026-10-07
 */
void pci_init(void);

/**
 * Read a 32-bit PCI configuration register.
 * @return Register value, or 0xFFFFFFFF for an absent device.
 * @date 2026-10-07
 */
uint32_t pci_config_read32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
);

uint16_t pci_config_read16(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
);

uint8_t pci_config_read8(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
);

void pci_config_write32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint32_t value
);


void pci_config_write8(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint8_t value
);

void pci_config_write16(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint16_t value
);

/**
 * Probe and cache all memory and I/O BARs for a device.
 * @param dev Device returned by PCI enumeration.
 * @date 2026-10-07
 */
void pci_read_bars(pci_device_t *dev);

/**
 * Register a driver using 0xFFFF/0xFF fields as wildcards.
 * @return 0 on success, -1 for invalid input or duplicate registration.
 * @date 2026-10-07
 */
int pci_register_driver(pci_driver_t *driver);

/**
 * Parse the standard capability list and record MSI support.
 * @date 2026-10-07
 */
void pci_parse_capabilities(pci_device_t *device);

/**
 * Bind the first matching registered driver to a device.
 * @date 2026-10-07
 */
void pci_bind_drivers(pci_device_t *device);

/**
 * Return the first driver bound to a device, if any.
 * @date 2026-10-07
 */
pci_driver_t *pci_device_driver(const pci_device_t *device);

/**
 * Return the head of the discovered-device list.
 * @date 2026-10-07
 */
pci_device_t *pci_get_devices(void);

#endif // PCI_H