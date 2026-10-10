#ifndef AHCI_H
#define AHCI_H

#include <lib/std/stdint.h>
#include <lib/std/stdio.h>


typedef struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
} __attribute__((packed)) ahci_port_t;

typedef struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint8_t  reserved[0x8C];
    uint8_t  vendor[0x60];

    ahci_port_t ports[32];
} __attribute__((packed)) ahci_hba_mem_t;

/**
 * Discover AHCI controllers after PCI enumeration.
 * @date 2026-10-10
 */
void ahci_init(void);

#endif
