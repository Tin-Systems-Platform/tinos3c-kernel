#include "pci.h"
#include "pci_classes.h"
#include <lib/std/stdio.h>

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

}

void pci_init(void)
{
    printf("PCI: Starting PCI Device driver registration\n");
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {

            uint16_t vendor =
                pci_config_read16(bus, device, 0, 0x00);

            if (vendor == 0xFFFF)
                continue;

            uint8_t header =
                pci_config_read8(bus, device, 0, 0x0E);

            uint8_t functions =
                (header & 0x80) ? 8 : 1;

            for (uint8_t function = 0;
                 function < functions;
                 function++) {

                pci_scan(
                    bus,
                    device,
                    function
                );
                 }
        }
    }

    printf("PCI: PCI Device driver registration complete\n");
}