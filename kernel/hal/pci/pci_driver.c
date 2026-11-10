#include "pci.h"

#include <lib/std/stdio.h>

static pci_driver_t *drivers;

static uint8_t bar_count_for_header(uint8_t header_type)
{
    if (header_type == 0x00)
        return 6;
    if (header_type == 0x01)
        return 2;
    return 0;
}

void pci_read_bars(pci_device_t *dev)
{
    uint8_t index;
    uint8_t count;

    if (dev == 0)
        return;

    count = bar_count_for_header(dev->header_type);
    dev->bar_count = count;
    for (index = 0; index < count; ++index) {
        uint8_t offset = (uint8_t)(0x10 + index * 4);
        uint8_t bar_index = index;
        uint32_t original = pci_config_read32(
            dev->bus, dev->device, dev->function, offset);
        uint32_t mask;
        uint64_t base;
        uint64_t size;

        dev->bars[bar_index].base = 0;
        dev->bars[bar_index].size = 0;
        dev->bars[bar_index].is_io = (original & 1U) != 0;
        dev->bars[bar_index].is_64bit = false;
        dev->bars[bar_index].prefetchable = false;

        if (dev->bars[bar_index].is_io) {
            pci_config_write32(dev->bus, dev->device, dev->function,
                               offset, 0xFFFFFFFFU);
            mask = pci_config_read32(
                dev->bus, dev->device, dev->function, offset) & ~0x3U;
            pci_config_write32(dev->bus, dev->device, dev->function,
                               offset, original);
            base = original & ~0x3U;
            size = mask == 0 ? 0 : (~(uint64_t)mask + 1ULL);
        } else {
            uint8_t type = (uint8_t)((original >> 1) & 3U);
            uint32_t upper = 0;
            uint32_t upper_mask = 0;

            dev->bars[bar_index].prefetchable = (original & 8U) != 0;
            dev->bars[bar_index].is_64bit = type == 2;
            if (dev->bars[bar_index].is_64bit && index + 1 < count) {
                upper = pci_config_read32(
                    dev->bus, dev->device, dev->function, offset + 4);
                pci_config_write32(dev->bus, dev->device, dev->function,
                                   offset, 0xFFFFFFFFU);
                pci_config_write32(dev->bus, dev->device, dev->function,
                                   offset + 4, 0xFFFFFFFFU);
                mask = pci_config_read32(
                    dev->bus, dev->device, dev->function, offset) & ~0xFU;
                upper_mask = pci_config_read32(
                    dev->bus, dev->device, dev->function, offset + 4);
                pci_config_write32(dev->bus, dev->device, dev->function,
                                   offset, original);
                pci_config_write32(dev->bus, dev->device, dev->function,
                                   offset + 4, upper);
                base = ((uint64_t)upper << 32) | (original & ~0xFU);
                size = ((uint64_t)upper_mask << 32) | mask;
                size = size == 0 ? 0 : ~size + 1ULL;
                ++index;
            } else {
                pci_config_write32(dev->bus, dev->device, dev->function,
                                   offset, 0xFFFFFFFFU);
                mask = pci_config_read32(
                    dev->bus, dev->device, dev->function, offset) & ~0xFU;
                pci_config_write32(dev->bus, dev->device, dev->function,
                                   offset, original);
                base = original & ~0xFU;
                size = mask == 0 ? 0 : (~(uint64_t)mask + 1ULL);
            }
        }
        dev->bars[bar_index].base = base;
        dev->bars[bar_index].size = size;
    }
}

void pci_parse_capabilities(pci_device_t *dev)
{
    uint8_t pointer;
    uint8_t visited[48] = { 0 };
    uint8_t steps = 0;

    if (dev == 0 || (pci_config_read16(dev->bus, dev->device, dev->function,
                                       0x06) & (1U << 4)) == 0)
        return;

    pointer = pci_config_read8(dev->bus, dev->device, dev->function, 0x34);
    while (pointer >= 0x40 && steps < sizeof(visited)) {
        uint8_t slot = (uint8_t)((pointer - 0x40) / 4);
        uint8_t id;
        uint8_t next;

        if ((pointer & 3U) != 0 || visited[slot] != 0)
            break;
        visited[slot] = 1;
        id = pci_config_read8(dev->bus, dev->device, dev->function, pointer);
        next = pci_config_read8(dev->bus, dev->device, dev->function,
                                pointer + 1);
        if (id == 0x05) {
            uint16_t control = pci_config_read16(
                dev->bus, dev->device, dev->function, pointer + 2);
            dev->msi_capability = pointer;
            dev->msi_64bit = (control & (1U << 7)) != 0;
            dev->msi_multiple_message_capable =
                (uint8_t)((control >> 1) & 7U);
            break;
        }
        pointer = next;
        ++steps;
    }
}

static int driver_matches(const pci_driver_t *driver,
                          const pci_device_t *device)
{
    return (driver->vendor_id == 0xFFFF || driver->vendor_id == device->vendor_id) &&
           (driver->device_id == 0xFFFF || driver->device_id == device->device_id) &&
           (driver->class_code == 0xFF || driver->class_code == device->class_code) &&
           (driver->subclass == 0xFF || driver->subclass == device->subclass);
}

void pci_bind_drivers(pci_device_t *device)
{
    pci_driver_t *driver;

    for (driver = drivers; driver != 0; driver = driver->next) {
        if (!driver_matches(driver, device))
            continue;
        device->driver = driver;
        if (driver->probe == 0 || driver->probe(device) == 0) {
            printf("PCI: bound %s to %02x:%02x.%u\n",
                   driver->name == 0 ? "unnamed driver" : driver->name,
                   device->bus, device->device, device->function);
            return;
        }
        device->driver = 0;
    }
}

int pci_register_driver(pci_driver_t *driver)
{
    pci_device_t *device;
    pci_driver_t *current;

    if (driver == 0 || driver->probe == 0)
        return -1;
    for (current = drivers; current != 0; current = current->next)
        if (current == driver)
            return -1;

    driver->next = drivers;
    drivers = driver;
    for (device = pci_get_devices(); device != 0; device = device->next)
        if (device->driver == 0)
            pci_bind_drivers(device);
    return 0;
}

pci_driver_t *pci_device_driver(const pci_device_t *device)
{
    return device == 0 ? 0 : device->driver;
}
