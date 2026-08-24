#include "pci.h"
#include <lib/std/stdio.h>

uint32_t pci_config_read32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
) {
    uint32_t address =
        (1U << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
) {
    uint32_t value = pci_config_read32(
        bus,
        device,
        function,
        offset
    );

    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_config_read8(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
) {
    uint32_t value = pci_config_read32(
        bus,
        device,
        function,
        offset
    );

    return (uint8_t)((value >> ((offset & 3) * 8)) & 0xFF);
}

void pci_config_write32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint32_t value
) {
    uint32_t address =
        (1U << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write8(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint8_t value
) {
    uint32_t address =
        0x80000000 |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);

    outl(0xCF8, address);

    uint32_t shift = (offset & 3) * 8;
    uint32_t current = inl(0xCFC);

    current &= ~(0xFF << shift);
    current |= ((uint32_t)value << shift);

    outl(0xCFC, current);
}

void pci_config_write16(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint16_t value
) {
    uint32_t address =
        0x80000000 |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);

    outl(0xCF8, address);

    uint32_t shift = (offset & 2) * 8;
    uint32_t current = inl(0xCFC);

    current &= ~(0xFFFF << shift);
    current |= ((uint32_t)value << shift);

    outl(0xCFC, current);
}

uint32_t pci_config_address(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
) {
    return 0x80000000 |
           ((uint32_t)bus      << 16) |
           ((uint32_t)device   << 11) |
           ((uint32_t)function << 8)  |
           (offset & 0xFC);
}