#include "pci_classes.h"

int pci_is_mass_storage(uint8_t class_code)
{
    return class_code == PCI_MASS_STORAGE_CONTROLLER;
}
