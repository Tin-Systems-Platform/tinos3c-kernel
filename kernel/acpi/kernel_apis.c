#include <uacpi/kernel_api.h>

#include <drivers/pci/pci.h>
#include <lib/std/stdlib.h>
#include <lib/std/stdint.h>
#include <lib/std/stdio.h>
#include <mm/vmm.h>

#define ACPI_PAGE_SIZE          0x1000ULL
#define ACPI_MAP_WINDOW_BASE    0xFFFF800000000000ULL
#define ACPI_MAP_WINDOW_PAGES   16384U
#define ACPI_MAP_RECORDS        256U
#define ACPI_PAGE_FLAG_WRITABLE 0x002ULL

struct acpi_mapping {
    uint64_t virtual_base;
    uint64_t page_count;
};

static uint8_t map_slots[ACPI_MAP_WINDOW_PAGES / 8];
static struct acpi_mapping mappings[ACPI_MAP_RECORDS];

static int slot_is_used(uint32_t slot)
{
    return (map_slots[slot / 8] & (uint8_t)(1U << (slot % 8))) != 0;
}

static void set_slot(uint32_t slot)
{
    map_slots[slot / 8] |= (uint8_t)(1U << (slot % 8));
}

static void clear_slot(uint32_t slot)
{
    map_slots[slot / 8] &= (uint8_t)~(1U << (slot % 8));
}

static int reserve_slots(uint32_t page_count, uint32_t *out_start)
{
    uint32_t start;
    uint32_t index;

    if (out_start == 0 || page_count == 0 ||
        page_count > ACPI_MAP_WINDOW_PAGES)
        return 0;

    for (start = 0; start <= ACPI_MAP_WINDOW_PAGES - page_count; ++start) {
        for (index = 0; index < page_count; ++index) {
            if (slot_is_used(start + index))
                break;
        }

        if (index == page_count) {
            for (index = 0; index < page_count; ++index)
                set_slot(start + index);
            *out_start = start;
            return 1;
        }
    }

    return 0;
}

static void release_slots(uint32_t start, uint32_t page_count)
{
    uint32_t index;

    if (start >= ACPI_MAP_WINDOW_PAGES ||
        page_count > ACPI_MAP_WINDOW_PAGES - start)
        return;

    for (index = 0; index < page_count; ++index)
        clear_slot(start + index);
}

static struct acpi_mapping *reserve_mapping_record(void)
{
    uint32_t index;

    for (index = 0; index < ACPI_MAP_RECORDS; ++index) {
        if (mappings[index].page_count == 0)
            return &mappings[index];
    }

    return 0;
}

static struct acpi_mapping *find_mapping(uint64_t virtual_base,
                                         uint64_t page_count)
{
    uint32_t index;

    for (index = 0; index < ACPI_MAP_RECORDS; ++index) {
        if (mappings[index].virtual_base == virtual_base &&
            mappings[index].page_count == page_count)
            return &mappings[index];
    }

    return 0;
}

static int mapping_was_installed(uint64_t virtual_address,
                                 uint64_t physical_address)
{
    uint64_t *page_table;
    uint16_t page_index;

    page_table = get_page_table(virtual_address);
    if (page_table == 0)
        return 0;

    page_index = (uint16_t)((virtual_address >> 12) & 0x1FFULL);
    return (page_table[page_index] & 0x000FFFFFFFFFF000ULL) ==
           physical_address;
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
{
    uint64_t physical_address = (uint64_t)addr;
    uint64_t requested_length = (uint64_t)len;
    uint64_t physical_base;
    uint64_t offset;
    uint64_t mapped_length;
    uint64_t page_count_64;
    uint64_t virtual_base;
    uint32_t slot_start;
    uint32_t page_count;
    uint32_t index;
    struct acpi_mapping *record;

    if (requested_length == 0)
        return UACPI_MAP_FAILED;

    offset = physical_address & (ACPI_PAGE_SIZE - 1ULL);
    if (requested_length > ~(uint64_t)0 - offset)
        return UACPI_MAP_FAILED;
    mapped_length = requested_length + offset;
    if (mapped_length > ~(uint64_t)0 - (ACPI_PAGE_SIZE - 1ULL))
        return UACPI_MAP_FAILED;

    mapped_length = (mapped_length + ACPI_PAGE_SIZE - 1ULL) &
                    ~(ACPI_PAGE_SIZE - 1ULL);
    page_count_64 = mapped_length / ACPI_PAGE_SIZE;
    if (page_count_64 == 0 || page_count_64 > ACPI_MAP_WINDOW_PAGES)
        return UACPI_MAP_FAILED;

    physical_base = physical_address - offset;
    if (physical_base > ~(uint64_t)0 - mapped_length)
        return UACPI_MAP_FAILED;

    page_count = (uint32_t)page_count_64;
    record = reserve_mapping_record();
    if (record == 0 || !reserve_slots(page_count, &slot_start))
        return UACPI_MAP_FAILED;

    virtual_base = ACPI_MAP_WINDOW_BASE +
                   (uint64_t)slot_start * ACPI_PAGE_SIZE;
    for (index = 0; index < page_count; ++index) {
        map_page(virtual_base + (uint64_t)index * ACPI_PAGE_SIZE,
                 physical_base + (uint64_t)index * ACPI_PAGE_SIZE,
                 ACPI_PAGE_FLAG_WRITABLE);

        if (!mapping_was_installed(
                virtual_base + (uint64_t)index * ACPI_PAGE_SIZE,
                physical_base + (uint64_t)index * ACPI_PAGE_SIZE)) {
            while (index != 0) {
                --index;
                unmap_page(virtual_base + (uint64_t)index *
                           ACPI_PAGE_SIZE);
            }
            release_slots(slot_start, page_count);
            return UACPI_MAP_FAILED;
        }
    }

    record->virtual_base = virtual_base;
    record->page_count = page_count;
    return (void *)(uintptr_t)(virtual_base + offset);
}

void uacpi_kernel_unmap(void *addr, uacpi_size len)
{
    uint64_t virtual_address = (uint64_t)(uintptr_t)addr;
    uint64_t requested_length = (uint64_t)len;
    uint64_t offset;
    uint64_t mapped_length;
    uint64_t page_count;
    uint64_t virtual_base;
    uint64_t slot_start;
    uint32_t index;
    struct acpi_mapping *record;

    if (addr == 0 || addr == UACPI_MAP_FAILED || requested_length == 0)
        return;

    offset = virtual_address & (ACPI_PAGE_SIZE - 1ULL);
    if (requested_length > ~(uint64_t)0 - offset)
        return;
    mapped_length = requested_length + offset;
    if (mapped_length > ~(uint64_t)0 - (ACPI_PAGE_SIZE - 1ULL))
        return;
    mapped_length = (mapped_length + ACPI_PAGE_SIZE - 1ULL) &
                    ~(ACPI_PAGE_SIZE - 1ULL);
    page_count = mapped_length / ACPI_PAGE_SIZE;
    virtual_base = virtual_address - offset;

    if (virtual_base < ACPI_MAP_WINDOW_BASE ||
        page_count == 0 ||
        page_count > ACPI_MAP_WINDOW_PAGES ||
        virtual_base > ACPI_MAP_WINDOW_BASE +
                       (uint64_t)(ACPI_MAP_WINDOW_PAGES - page_count) *
                           ACPI_PAGE_SIZE)
        return;

    record = find_mapping(virtual_base, page_count);
    if (record == 0)
        return;

    slot_start = (virtual_base - ACPI_MAP_WINDOW_BASE) / ACPI_PAGE_SIZE;
    for (index = 0; index < page_count; ++index)
        unmap_page(virtual_base + (uint64_t)index * ACPI_PAGE_SIZE);

    release_slots((uint32_t)slot_start, (uint32_t)page_count);
    record->virtual_base = 0;
    record->page_count = 0;
}

void uacpi_kernel_log(uacpi_log_level, const uacpi_char*) {    
    print("[uACPI] ");
    print("log message\n");
}

typedef struct {
    uacpi_pci_address address;
} tinos_pci_handle_t;

uacpi_status uacpi_kernel_pci_device_open(
    uacpi_pci_address address,
    uacpi_handle *out_handle
) {
    if (out_handle == NULL) {
        return UACPI_STATUS_INVALID_ARGUMENT;
    }

    tinos_pci_handle_t *handle =
        uacpi_kernel_alloc(sizeof(tinos_pci_handle_t));

    if (handle == NULL) {
        return UACPI_STATUS_OUT_OF_MEMORY;
    }

    handle->address = address;

    *out_handle = (uacpi_handle)handle;

    return UACPI_STATUS_OK;
}


void uacpi_kernel_pci_device_close(
    uacpi_handle handle
) {
    if (handle == NULL) {
        return;
    }

    uacpi_kernel_free(handle);
}

static tinos_pci_handle_t *pci_handle(uacpi_handle handle)
{
    return (tinos_pci_handle_t *)handle;
}

uacpi_status uacpi_kernel_pci_read8(
    uacpi_handle device,
    uacpi_size offset,
    uacpi_u8 *value
) {
    if (device == NULL || value == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_pci_handle_t *pci = pci_handle(device);

    *value = pci_config_read8(
        pci->address.bus,
        pci->address.device,
        pci->address.function,
        (uint8_t)offset
    );

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(
    uacpi_handle device,
    uacpi_size offset,
    uacpi_u16 *value
) {
    if (device == NULL || value == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_pci_handle_t *pci = pci_handle(device);

    *value = pci_config_read16(
        pci->address.bus,
        pci->address.device,
        pci->address.function,
        (uint8_t)offset
    );

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(
    uacpi_handle device,
    uacpi_size offset,
    uacpi_u32 *value
) {
    if (device == NULL || value == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_pci_handle_t *pci = pci_handle(device);

    *value = pci_config_read32(
        pci->address.bus,
        pci->address.device,
        pci->address.function,
        (uint8_t)offset
    );

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(
    uacpi_handle device,
    uacpi_size offset,
    uacpi_u8 value
) {
    if (device == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_pci_handle_t *pci = pci_handle(device);

    pci_config_write8(
        pci->address.bus,
        pci->address.device,
        pci->address.function,
        (uint8_t)offset,
        value
    );

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(
    uacpi_handle device,
    uacpi_size offset,
    uacpi_u16 value
) {
    if (device == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_pci_handle_t *pci = pci_handle(device);

    pci_config_write16(
        pci->address.bus,
        pci->address.device,
        pci->address.function,
        (uint8_t)offset,
        value
    );

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(
    uacpi_handle device,
    uacpi_size offset,
    uacpi_u32 value
) {
    if (device == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_pci_handle_t *pci = pci_handle(device);

    pci_config_write32(
        pci->address.bus,
        pci->address.device,
        pci->address.function,
        (uint8_t)offset,
        value
    );

    return UACPI_STATUS_OK;
}

typedef struct {
    uacpi_io_addr base;
    uacpi_size len;
} tinos_io_handle_t;

uacpi_status uacpi_kernel_io_map(
    uacpi_io_addr base,
    uacpi_size len,
    uacpi_handle *out_handle
) {
    if (out_handle == NULL || len == 0)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_io_handle_t *io =
        uacpi_kernel_alloc(sizeof(tinos_io_handle_t));

    if (io == NULL)
        return UACPI_STATUS_OUT_OF_MEMORY;

    io->base = base;
    io->len = len;

    *out_handle = (uacpi_handle)io;

    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle)
{
    if (handle == NULL)
        return;

    uacpi_kernel_free(handle);
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

uacpi_status uacpi_kernel_io_read8(
    uacpi_handle handle,
    uacpi_size offset,
    uacpi_u8 *out_value
) {
    if (handle == NULL || out_value == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_io_handle_t *io =
        (tinos_io_handle_t *)handle;

    if (offset >= io->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    *out_value = inb((uint16_t)(io->base + offset));

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(
    uacpi_handle handle,
    uacpi_size offset,
    uacpi_u16 *out_value
) {
    if (handle == NULL || out_value == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_io_handle_t *io =
        (tinos_io_handle_t *)handle;

    if (offset + 2 > io->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    *out_value = inw((uint16_t)(io->base + offset));

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(
    uacpi_handle handle,
    uacpi_size offset,
    uacpi_u32 *out_value
) {
    if (handle == NULL || out_value == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_io_handle_t *io =
        (tinos_io_handle_t *)handle;

    if (offset + 4 > io->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    *out_value = inl((uint16_t)(io->base + offset));

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(
    uacpi_handle handle,
    uacpi_size offset,
    uacpi_u8 in_value
) {
    if (handle == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_io_handle_t *io =
        (tinos_io_handle_t *)handle;

    if (offset >= io->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    outb((uint16_t)(io->base + offset), in_value);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(
    uacpi_handle handle,
    uacpi_size offset,
    uacpi_u16 in_value
) {
    if (handle == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_io_handle_t *io =
        (tinos_io_handle_t *)handle;

    if (offset >= io->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    outw((uint16_t)(io->base + offset), in_value);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(
    uacpi_handle handle,
    uacpi_size offset,
    uacpi_u32 in_value
) {
    if (handle == NULL)
        return UACPI_STATUS_INVALID_ARGUMENT;

    tinos_io_handle_t *io =
        (tinos_io_handle_t *)handle;

    if (offset >= io->len)
        return UACPI_STATUS_INVALID_ARGUMENT;

    outl((uint16_t)(io->base + offset), in_value);

    return UACPI_STATUS_OK;
}

void *uacpi_kernel_alloc(uacpi_size size) {
    *malloc(size);
}

void uacpi_kernel_free(void *mem) {
    free(mem);
}