#include "ahci.h"

#include <drivers/pci/pci.h>
#include <drivers/pci/pci_mmio.h>
#include <lib/std/stdio.h>

#define AHCI_PROG_IF 0x01
#define AHCI_ABAR_INDEX 5
#define AHCI_HBA_LENGTH 0x1100ULL
#define AHCI_GHC_AE (1U << 31)
#define PCI_COMMAND 0x04
#define PCI_COMMAND_MEMORY_SPACE (1U << 1)
#define AHCI_PORT_OFFSET 0x100U
#define AHCI_PORT_STRIDE 0x80U
#define AHCI_PORT_SSTS 0x28U
#define AHCI_PORT_SERR 0x30U
#define AHCI_PORT_SCTL 0x2C
#define AHCI_PORT_CMD 0x18U
#define AHCI_PORT_IE 0x14U
#define AHCI_PORT_CMD_ST (1U << 0)
#define AHCI_PORT_CMD_FRE (1U << 4)
#define AHCI_PORT_CMD_CR (1U << 15)
#define AHCI_PORT_CMD_FR (1U << 14)
#define AHCI_PORT_SSTS_DET_MASK 0x0FU
#define AHCI_PORT_SSTS_IPM_MASK (0x0FU << 8)
#define AHCI_PORT_SSTS_DET_PRESENT 0x03U
#define AHCI_PORT_SSTS_IPM_ACTIVE (0x01U << 8)
#define AHCI_PORT_SSTS_PRESENT_ACTIVE \
    (AHCI_PORT_SSTS_DET_PRESENT | AHCI_PORT_SSTS_IPM_ACTIVE)
#define AHCI_PORT_INIT_TIMEOUT 1000000U

static int ahci_abar_is_valid(const pci_bar_t *bar)
{
    if (bar == 0 || bar->is_io ||
        (!bar->is_64bit && bar->base > 0xFFFFFFFFULL) ||
        bar->base == 0 || (bar->base & 0xFFFULL) != 0 ||
        bar->size < AHCI_HBA_LENGTH ||
        bar->base > ~0ULL - AHCI_HBA_LENGTH)
        return 0;
    return 1;
}

static volatile uint32_t *ahci_port_register(
    volatile ahci_hba_mem_t *hba,
    uint32_t port,
    uint32_t offset
)
{
    if (port >= 32 || offset > AHCI_PORT_STRIDE - sizeof(uint32_t))
        return 0;
    return (volatile uint32_t *)((volatile uint8_t *)hba +
        AHCI_PORT_OFFSET + port * AHCI_PORT_STRIDE + offset);
}

/**
 * Stop one AHCI port engine and report its link/signature state.
 *
 * This deliberately does not allocate command-list/FIS memory or enable the
 * engine, because this kernel has no verified DMA-safe contiguous allocator.
 *
 * @param hba Mapped AHCI controller register block.
 * @param port Port number from the HBA PI bitmap.
 * @return None. A timeout leaves the port disabled and is reported.
 * @date 2026-10-10
 */
static void ahci_initialize_port(volatile ahci_hba_mem_t *hba, uint32_t port)
{
    volatile uint32_t *cmd = ahci_port_register(hba, port, AHCI_PORT_CMD);
    volatile uint32_t *ssts = ahci_port_register(hba, port, AHCI_PORT_SSTS);
    volatile uint32_t *serr = ahci_port_register(hba, port, AHCI_PORT_SERR);
    volatile uint32_t *ie = ahci_port_register(hba, port, AHCI_PORT_IE);
    uint32_t command;
    uint32_t status;
    uint32_t timeout;

    if (cmd == 0 || ssts == 0 || serr == 0 || ie == 0) {
        printf("[AHCI] Port %u has an invalid register offset\n", port);
        return;
    }

    command = *cmd;
    *ie = 0;
    *cmd = command & ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);
    for (timeout = 0; timeout < AHCI_PORT_INIT_TIMEOUT; ++timeout) {
        if ((*cmd & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) == 0)
            break;
    }
    if (timeout == AHCI_PORT_INIT_TIMEOUT) {
        printf("[AHCI] Port %u command engine did not stop\n", port);
        return;
    }

    /* SERR is write-one-to-clear; do not write a read value to other regs. */
    *serr = UINT32_MAX;
    status = *ssts;
    printf("[AHCI] Port %u SSTS=%08x DET=%x IPM=%x\n", port, status,
           status & AHCI_PORT_SSTS_DET_MASK,
           (status & AHCI_PORT_SSTS_IPM_MASK) >> 8);
    if ((status & (AHCI_PORT_SSTS_DET_MASK | AHCI_PORT_SSTS_IPM_MASK)) ==
        AHCI_PORT_SSTS_PRESENT_ACTIVE) {
        printf("[AHCI] Port %u has an active link; command engine remains "
               "disabled pending DMA setup\n", port);
    }
}

/**
 * Inspect one AHCI controller and enable AHCI mode without touching ports.
 *
 * @param device PCI device identified as SATA/AHCI.
 * @return None. Errors are reported and leave the controller untouched.
 * @date 2026-10-10
 */
static void ahci_probe_controller(pci_device_t *device)
{
    const pci_bar_t *abar = &device->bars[AHCI_ABAR_INDEX];
    volatile ahci_hba_mem_t *hba;
    uint16_t command;
    uint32_t ghc;

    if (device->bar_count <= AHCI_ABAR_INDEX ||
        !ahci_abar_is_valid(abar)) {
        printf("[AHCI] %02x:%02x.%u has no usable ABAR\n",
               device->bus, device->device, device->function);
        return;
    }

    command = pci_config_read16(device->bus, device->device,
                                device->function, PCI_COMMAND);
    if ((command & PCI_COMMAND_MEMORY_SPACE) == 0) {
        /*
         * Firmware normally enables this before handing off the device.
         * Enabling memory decoding is safe here; bus mastering remains off.
         */
        pci_config_write16(device->bus, device->device, device->function,
                           PCI_COMMAND, command | PCI_COMMAND_MEMORY_SPACE);
        command = pci_config_read16(device->bus, device->device,
                                    device->function, PCI_COMMAND);
        if ((command & PCI_COMMAND_MEMORY_SPACE) == 0) {
            printf("[AHCI] PCI memory decoding is disabled on %02x:%02x.%u\n",
                   device->bus, device->device, device->function);
            return;
        }
    }

    /*
     * pci_mmio_map accepts a physical address and returns a virtual address;
     * do not use the ABAR value as a pointer. AHCI global registers and the
     * first port register block require at least 0x1100 bytes.
     */
    hba = (volatile ahci_hba_mem_t *)pci_mmio_map(
        abar->base, AHCI_HBA_LENGTH);
    if (hba == 0) {
        printf("[AHCI] Failed to map ABAR %llx for %02x:%02x.%u\n",
               (unsigned long long)abar->base, device->bus,
               device->device, device->function);
        return;
    }

    printf("[AHCI] %02x:%02x.%u ABAR=%llx CAP=%08x GHC=%08x IS=%08x "
           "PI=%08x VS=%08x\n",
           device->bus, device->device, device->function,
           (unsigned long long)abar->base, hba->cap, hba->ghc, hba->is,
           hba->pi, hba->vs);

    ghc = hba->ghc;
    if ((ghc & AHCI_GHC_AE) == 0) {
        hba->ghc = ghc | AHCI_GHC_AE;
        ghc = hba->ghc;
        if ((ghc & AHCI_GHC_AE) == 0) {
            printf("[AHCI] Controller refused AHCI Enable on %02x:%02x.%u\n",
                   device->bus, device->device, device->function);
            return;
        }
        printf("[AHCI] Enabled AHCI mode on %02x:%02x.%u\n",
               device->bus, device->device, device->function);
    } else {
        printf("[AHCI] AHCI mode already enabled on %02x:%02x.%u\n",
               device->bus, device->device, device->function);
    }

    for (uint32_t port = 0; port < 32; ++port) {
        if ((hba->pi & (1U << port)) != 0)
            ahci_initialize_port(hba, port);
    }
    printf("[AHCI] Controller and implemented ports initialized without "
           "command submission.\n");
}

/**
 * Discover PCI AHCI controllers and perform safe HBA initialization.
 *
 * @return None. Controllers with invalid BARs or failed mappings are skipped.
 * @date 2026-10-10
 */
void ahci_init(void)
{
    pci_device_t *device;
    uint32_t found = 0;

    for (device = pci_get_devices(); device != 0; device = device->next) {
        if (device->class_code != PCI_CLASS_MASS_STORAGE ||
            device->subclass != PCI_SUBCLASS_SATA ||
            device->prog_if != AHCI_PROG_IF)
            continue;
        ++found;
        ahci_probe_controller(device);
    }

    if (found == 0)
        printf("[AHCI] No AHCI controller found.\n");
}