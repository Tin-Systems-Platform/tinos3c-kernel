#include "pci.h"
#include <lib/std/stdio.h>

static pci_device_t devices[256 * 32 * 8];
static pci_device_t *device_list;
static uint32_t device_count;

static void pci_scan(
    uint8_t bus,
    uint8_t device,
    uint8_t function
) {
    pci_device_t *dev;
    uint16_t vendor = pci_config_read16(bus, device, function, 0x00);
    uint8_t header;

    if (vendor == 0xFFFF || device_count >= (sizeof(devices) / sizeof(devices[0])))
        return;

    dev = &devices[device_count++];
    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->vendor_id = vendor;
    dev->device_id = pci_config_read16(bus, device, function, 0x02);
    dev->revision = pci_config_read8(bus, device, function, 0x08);
    dev->prog_if = pci_config_read8(bus, device, function, 0x09);
    dev->subclass = pci_config_read8(bus, device, function, 0x0A);
    dev->class_code = pci_config_read8(bus, device, function, 0x0B);
    header = pci_config_read8(bus, device, function, 0x0E);
    dev->header_type = header & 0x7F;
    dev->irq_line = pci_config_read8(bus, device, function, 0x3C);
    dev->irq_pin = pci_config_read8(bus, device, function, 0x3D);
    dev->next = device_list;
    device_list = dev;

    pci_read_bars(dev);
    pci_parse_capabilities(dev);
    pci_bind_drivers(dev);
    printf("PCI: %02x:%02x.%u vendor=%04x device=%04x class=%02x:%02x\n",
           bus, device, function, dev->vendor_id, dev->device_id,
           dev->class_code, dev->subclass);
}

void pci_init(void)
{
    device_count = 0;
    device_list = 0;
    printf("PCI: enumerating devices\n");
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

    printf("PCI: enumeration complete (%u devices)\n", device_count);
}

pci_device_t *pci_get_devices(void)
{
    return device_list;
}
