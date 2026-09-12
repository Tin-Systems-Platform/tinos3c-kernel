#include "pci.h"

static void pci_scan(
    uint8_t bus,
    uint8_t device,
    uint8_t function
) {
    uint16_t vendor = pci_config_read16(bus, device, function, 0x00);

    if (vendor == 0xFFFF)
        return;

    uint16_t device_id =
        pci_config_read16(bus, device, function, 0x02);

    uint8_t revision =
        pci_config_read8(bus, device, function, 0x08);

    uint8_t prog_if =
        pci_config_read8(bus, device, function, 0x09);

    uint8_t subclass =
        pci_config_read8(bus, device, function, 0x0A);

    uint8_t class_code =
        pci_config_read8(bus, device, function, 0x0B);

    uint8_t header =
        pci_config_read8(bus, device, function, 0x0E);

    /*
     * At this point you have discovered a PCI device.
     *
     * Example:
     *
     * vendor      = 0x8086
     * device_id   = ...
     * class_code  = 0x01
     * subclass    = 0x06
     */
}

