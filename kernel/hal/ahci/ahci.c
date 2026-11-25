#include "ahci.h"

#include <hal/pci/pci.h>
#include <hal/pci/pci_mmio.h>
#include <core/mm/dma.h>
#include <lib/std/stdio.h>

#define AHCI_PROG_IF 0x01
#define AHCI_ABAR_INDEX 5
#define AHCI_HBA_LENGTH 0x1100ULL
#define AHCI_GHC_AE (1U << 31)
#define PCI_COMMAND 0x04
#define PCI_COMMAND_MEMORY_SPACE (1U << 1)
#define PCI_COMMAND_BUS_MASTER (1U << 2)
#define AHCI_DMA_PAGES 4U
#define AHCI_TABLE_OFFSET 8192U
#define AHCI_TABLE_SIZE 256U
#define AHCI_BOUNCE_OFFSET 12288U
#define AHCI_PORT_OFFSET 0x100U
#define AHCI_PORT_STRIDE 0x80U
#define AHCI_PORT_SSTS 0x28U
#define AHCI_PORT_SERR 0x30U
#define AHCI_PORT_CMD 0x18U
#define AHCI_PORT_TFD 0x20U
#define AHCI_PORT_IE 0x14U
#define AHCI_PORT_CMD_ST (1U << 0)
#define AHCI_PORT_CMD_FRE (1U << 4)
#define AHCI_PORT_CMD_FR (1U << 14)
#define AHCI_PORT_CMD_CR (1U << 15)
#define AHCI_TFD_BSY (1U << 7)
#define AHCI_TFD_DRQ (1U << 3)
#define AHCI_TFD_ERR (1U << 0)
#define AHCI_SSTS_PRESENT_ACTIVE 0x103U
#define AHCI_TIMEOUT 10000000U
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA

typedef struct {
    volatile ahci_hba_mem_t *hba;
    uint32_t port;
    dma_buffer_t dma;
    struct block_device bdev;
    uint8_t busy;
} ahci_port_context_t;

static ahci_port_context_t port_contexts[32];
static uint32_t next_disk_number;

/** Resolve a port register address after checking port and offset bounds. @param hba Mapped HBA. @param port Port index. @param offset Register byte offset. @return Register pointer or NULL. @copyright See LICENSE.md. @date 2026-11-25 */
static volatile uint32_t *port_reg(volatile ahci_hba_mem_t *hba,
                                   uint32_t port, uint32_t offset)
{
    if (hba == 0 || port >= 32 || offset > AHCI_PORT_STRIDE - 4)
        return 0;
    return (volatile uint32_t *)((volatile uint8_t *)hba + AHCI_PORT_OFFSET +
                                  port * AHCI_PORT_STRIDE + offset);
}

/** Get command table memory for a command slot. @param context Initialized port state. @param slot Command slot index. @return Table pointer in the port DMA allocation. @copyright See LICENSE.md. @date 2026-11-25 */
static ahci_command_table_t *command_table(ahci_port_context_t *context,
                                          uint32_t slot)
{
    return (ahci_command_table_t *)((uint8_t *)context->dma.virtual_address +
                                     AHCI_TABLE_OFFSET + slot * AHCI_TABLE_SIZE);
}

/** Wait until selected port status bits clear. @param reg Register to poll. @param mask Bits that must clear. @return 0 on success, -1 on timeout. @copyright See LICENSE.md. @date 2026-11-25 */
static int wait_clear(volatile uint32_t *reg, uint32_t mask)
{
    for (uint32_t i = 0; i < AHCI_TIMEOUT; ++i)
        if ((*reg & mask) == 0)
            return 0;
    return -1;
}

/** Submit one synchronous, single-slot ATA command with an optional one-sector DMA buffer. @param context Port state. @param command ATA command byte. @param lba Sector address. @param sectors Sector count (zero for flush). @param write Nonzero for device-to-memory direction bit. @param data Kernel buffer. @return 0 on success, -1 on timeout or device error. @copyright See LICENSE.md. @date 2026-11-25 */
static int issue_command(ahci_port_context_t *context, uint8_t command,
                         uint64_t lba, uint16_t sectors, int write,
                         uint8_t *data)
{
    volatile ahci_port_t *port = &context->hba->ports[context->port];
    volatile uint32_t *tfd = port_reg(context->hba, context->port, AHCI_PORT_TFD);
    ahci_command_header_t *header = (ahci_command_header_t *)context->dma.virtual_address;
    ahci_command_table_t *table = command_table(context, 0);
    uint8_t *fis = table->command_fis;
    uint64_t table_phys = context->dma.physical_address + AHCI_TABLE_OFFSET;
    uint8_t *bounce = (uint8_t *)context->dma.virtual_address + AHCI_BOUNCE_OFFSET;

    if (context->busy || tfd == 0 || sectors > 1 || (sectors != 0 && data == 0))
        return -1;
    context->busy = 1;
    if (wait_clear(tfd, AHCI_TFD_BSY | AHCI_TFD_DRQ) != 0)
        goto fail;
    for (uint32_t i = 0; i < 64; ++i) fis[i] = 0;
    for (uint32_t i = 0; i < sizeof(table->prdt); ++i)
        ((uint8_t *)table->prdt)[i] = 0;
    if (sectors != 0) {
        if (write)
            for (uint32_t i = 0; i < BLKDEV_SECTOR_SIZE; ++i) bounce[i] = data[i];
        table->prdt[0].dba = (uint32_t)(context->dma.physical_address + AHCI_BOUNCE_OFFSET);
        table->prdt[0].dbau = (uint32_t)((context->dma.physical_address + AHCI_BOUNCE_OFFSET) >> 32);
        table->prdt[0].dbc = BLKDEV_SECTOR_SIZE - 1U | (1U << 31);
    }
    table->prdt[0].reserved = 0;
    table->prdt[0].dbc = sectors == 0 ? 0 : (BLKDEV_SECTOR_SIZE - 1U) | (1U << 31);
    header[0].options = 5U | (write ? (1U << 6) : 0) | (sectors ? (1U << 16) : 0);
    header[0].prdbc = 0;
    header[0].ctba = (uint32_t)table_phys;
    header[0].ctbau = (uint32_t)(table_phys >> 32);
    fis[0] = 0x27;
    fis[1] = 1U << 7;
    fis[2] = command;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 1U << 6;
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    fis[12] = (uint8_t)sectors;
    fis[13] = (uint8_t)(sectors >> 8);
    port->is = UINT32_MAX;
    port->serr = UINT32_MAX;
    port->ci = 1;
    for (uint32_t i = 0; i < AHCI_TIMEOUT; ++i) {
        if ((port->ci & 1U) == 0) {
            if ((*tfd & AHCI_TFD_ERR) != 0)
                goto fail;
            if (sectors != 0 && !write)
                for (uint32_t j = 0; j < BLKDEV_SECTOR_SIZE; ++j) data[j] = bounce[j];
            context->busy = 0;
            return 0;
        }
    }
fail:
    context->busy = 0;
    return -1;
}

/** Block-device read callback for an AHCI port. @param bdev Device descriptor. @param lba Sector to read. @param buffer 512-byte destination. @return 0 on success, -1 on command failure. @copyright See LICENSE.md. @date 2026-11-25 */
static int ahci_read_sector(struct block_device *bdev, uint64_t lba, void *buffer)
{
    ahci_port_context_t *context = (ahci_port_context_t *)bdev->private_data;
    return issue_command(context, ATA_CMD_READ_DMA_EXT, lba, 1, 0, buffer);
}

/** Block-device write callback for an AHCI port. @param bdev Device descriptor. @param lba Sector to write. @param buffer 512-byte source. @return 0 on success, -1 on command or flush failure. @copyright See LICENSE.md. @date 2026-11-25 */
static int ahci_write_sector(struct block_device *bdev, uint64_t lba,
                             const void *buffer)
{
    ahci_port_context_t *context = (ahci_port_context_t *)bdev->private_data;
    int result = issue_command(context, ATA_CMD_WRITE_DMA_EXT, lba, 1, 1,
                               (uint8_t *)buffer);
    if (result == 0)
        result = issue_command(context, ATA_CMD_FLUSH_CACHE_EXT, 0, 0, 0, 0);
    return result;
}

/** Return the number of addressable sectors. @param bdev Device descriptor. @return Sector count, or zero for a missing descriptor. @copyright See LICENSE.md. @date 2026-11-25 */
static uint64_t ahci_get_capacity(struct block_device *bdev)
{
    return bdev == 0 ? 0 : bdev->capacity_lba;
}

static struct block_device_ops ahci_ops = {
    .read_sector = ahci_read_sector,
    .write_sector = ahci_write_sector,
    .get_capacity = ahci_get_capacity
};

/** Validate an AHCI memory BAR before mapping. @param bar PCI BAR descriptor. @return Nonzero when the BAR is usable. @copyright See LICENSE.md. @date 2026-11-25 */
static int port_is_valid(const pci_bar_t *bar)
{
    return bar != 0 && !bar->is_io && bar->base != 0 &&
        (bar->base & 0xFFFULL) == 0 && bar->size >= AHCI_HBA_LENGTH &&
        bar->base <= ~0ULL - AHCI_HBA_LENGTH;
}

/** Stop, allocate DMA state, identify, and register a connected SATA port. @param hba Mapped HBA. @param index Port number. @return None; failed ports remain unregistered. @copyright See LICENSE.md. @date 2026-11-25 */
static void initialize_port(volatile ahci_hba_mem_t *hba, uint32_t index)
{
    volatile ahci_port_t *port = &hba->ports[index];
    volatile uint32_t *cmd = port_reg(hba, index, AHCI_PORT_CMD);
    volatile uint32_t *ssts = port_reg(hba, index, AHCI_PORT_SSTS);
    volatile uint32_t *ie = port_reg(hba, index, AHCI_PORT_IE);
    ahci_port_context_t *context = &port_contexts[index];
    ahci_command_header_t *headers;
    uint64_t table_phys;
    uint8_t identify[BLKDEV_SECTOR_SIZE];
    uint64_t capacity;

    if (cmd == 0 || ssts == 0 || ie == 0 || (*ssts & 0x103U) != AHCI_SSTS_PRESENT_ACTIVE)
        return;
    *ie = 0;
    *cmd &= ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);
    if (wait_clear(cmd, AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR) != 0) {
        printf("[AHCI] Port %u engine did not stop\n", index);
        return;
    }
    if (!dma_alloc_pages(AHCI_DMA_PAGES, &context->dma)) {
        printf("[AHCI] Port %u DMA allocation failed\n", index);
        return;
    }
    headers = (ahci_command_header_t *)context->dma.virtual_address;
    table_phys = context->dma.physical_address + AHCI_TABLE_OFFSET;
    for (uint32_t slot = 0; slot < 32; ++slot) {
        headers[slot].ctba = (uint32_t)(table_phys + slot * AHCI_TABLE_SIZE);
        headers[slot].ctbau = (uint32_t)((table_phys + slot * AHCI_TABLE_SIZE) >> 32);
    }
    port->clb = (uint32_t)context->dma.physical_address;
    port->clbu = (uint32_t)(context->dma.physical_address >> 32);
    port->fb = (uint32_t)(context->dma.physical_address + 4096);
    port->fbu = (uint32_t)((context->dma.physical_address + 4096) >> 32);
    context->hba = hba;
    context->port = index;
    *cmd |= AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_ST;
    if (issue_command(context, ATA_CMD_IDENTIFY, 0, 1, 0, identify) != 0) {
        printf("[AHCI] Port %u identify failed\n", index);
        dma_free_pages(&context->dma);
        *cmd &= ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);
        return;
    }
    capacity = (uint64_t)identify[200] | ((uint64_t)identify[201] << 8) |
               ((uint64_t)identify[202] << 16) | ((uint64_t)identify[203] << 24) |
               ((uint64_t)identify[204] << 32) | ((uint64_t)identify[205] << 40) |
               ((uint64_t)identify[206] << 48) | ((uint64_t)identify[207] << 56);
    if (capacity == 0) {
        dma_free_pages(&context->dma);
        *cmd &= ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);
        return;
    }
    context->bdev.name[0] = 's'; context->bdev.name[1] = 'd';
    context->bdev.name[2] = 'a' + (char)(next_disk_number++ % 26);
    context->bdev.name[3] = 0;
    context->bdev.capacity_lba = capacity;
    context->bdev.ops = &ahci_ops;
    context->bdev.private_data = context;
    if (blkdev_register(&context->bdev) != 0)
        printf("[AHCI] Could not register port %u\n", index);
    else
        printf("[AHCI] Port %u: %s, %u sectors\n", index,
               context->bdev.name, capacity);
}

/** Enable PCI access and AHCI mode, then probe implemented ports. @param device PCI AHCI device. @return None; failures are reported and skipped. @copyright See LICENSE.md. @date 2026-11-25 */
static void probe_controller(pci_device_t *device)
{
    const pci_bar_t *bar;
    volatile ahci_hba_mem_t *hba;
    uint16_t command;
    if (device->bar_count <= AHCI_ABAR_INDEX)
        return;
    bar = &device->bars[AHCI_ABAR_INDEX];
    if (!port_is_valid(bar)) {
        printf("[AHCI] Controller has invalid ABAR\n");
        return;
    }
    command = pci_config_read16(device->bus, device->device, device->function, PCI_COMMAND);
    command |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(device->bus, device->device, device->function, PCI_COMMAND, command);
    command = pci_config_read16(device->bus, device->device, device->function, PCI_COMMAND);
    if ((command & (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) !=
        (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) {
        printf("[AHCI] PCI memory or bus mastering unavailable\n");
        return;
    }
    hba = (volatile ahci_hba_mem_t *)pci_mmio_map(bar->base, AHCI_HBA_LENGTH);
    if (hba == 0) {
        printf("[AHCI] ABAR mapping failed\n");
        return;
    }
    hba->ghc |= AHCI_GHC_AE;
    if ((hba->ghc & AHCI_GHC_AE) == 0)
        return;
    for (uint32_t p = 0; p < 32; ++p)
        if ((hba->pi & (1U << p)) != 0)
            initialize_port(hba, p);
}

/** Discover AHCI controllers and register usable SATA disks. @return None. @copyright See LICENSE.md. @date 2026-11-25 */
void ahci_init(void)
{
    pci_device_t *device;
    for (device = pci_get_devices(); device != 0; device = device->next)
        if (device->class_code == PCI_CLASS_MASS_STORAGE &&
            device->subclass == PCI_SUBCLASS_SATA && device->prog_if == AHCI_PROG_IF)
            probe_controller(device);
}
