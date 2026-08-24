#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>

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

    struct pci_device *next;
} pci_device_t;

static void pci_scan(
    uint8_t bus,
    uint8_t device,
    uint8_t function
);

void pci_init(void);

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


#endif // PCI_H