#include <uacpi/kernel_api.h>

#include <drivers/pci/pci.h>
#include <lib/std/stdlib.h>
#include <lib/std/stdint.h>
#include <lib/std/stdio.h>
#include <mm/vmm.h>
#include <uacpi/internal/stdlib.h>

#define ACPI_PAGE_SIZE          0x1000ULL
#define ACPI_MAP_WINDOW_BASE    0xFFFF800000000000ULL
#define ACPI_MAP_WINDOW_PAGES   16384U
#define ACPI_MAP_RECORDS        256U
#define ACPI_PAGE_FLAG_WRITABLE 0x002ULL
#define ACPI_IDENTITY_MAP_LIMIT (1024ULL * 1024ULL * 1024ULL)

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
    uint64_t offset;
    uint64_t mapped_length;
    uint64_t page_count;
    uint64_t physical_base;
    uint32_t slot_start;
    uint32_t index;
    struct acpi_mapping *record;

    if (requested_length == 0)
        return UACPI_MAP_FAILED;

    if (physical_address < ACPI_IDENTITY_MAP_LIMIT &&
        requested_length <= ACPI_IDENTITY_MAP_LIMIT - physical_address)
        return (void *)(uintptr_t)physical_address;

    offset = physical_address & (ACPI_PAGE_SIZE - 1ULL);
    if (requested_length > ~(uint64_t)0 - offset)
        return UACPI_MAP_FAILED;
    mapped_length = requested_length + offset;
    if (mapped_length > ~(uint64_t)0 - (ACPI_PAGE_SIZE - 1ULL))
        return UACPI_MAP_FAILED;
    mapped_length = (mapped_length + ACPI_PAGE_SIZE - 1ULL) &
                    ~(ACPI_PAGE_SIZE - 1ULL);
    page_count = mapped_length / ACPI_PAGE_SIZE;
    if (page_count == 0 || page_count > ACPI_MAP_WINDOW_PAGES)
        return UACPI_MAP_FAILED;

    record = reserve_mapping_record();
    if (record == 0 || !reserve_slots((uint32_t)page_count, &slot_start))
        return UACPI_MAP_FAILED;

    physical_base = physical_address - offset;
    for (index = 0; index < page_count; ++index) {
        uint64_t virtual_page = ACPI_MAP_WINDOW_BASE +
                                ((uint64_t)slot_start + index) *
                                    ACPI_PAGE_SIZE;
        uint64_t physical_page = physical_base +
                                 (uint64_t)index * ACPI_PAGE_SIZE;

        map_page(virtual_page, physical_page, ACPI_PAGE_FLAG_WRITABLE);
        if (!mapping_was_installed(virtual_page, physical_page)) {
            while (index != 0) {
                --index;
                unmap_page(ACPI_MAP_WINDOW_BASE +
                           ((uint64_t)slot_start + index) * ACPI_PAGE_SIZE);
            }
            release_slots(slot_start, (uint32_t)page_count);
            return UACPI_MAP_FAILED;
        }
    }

    record->virtual_base = ACPI_MAP_WINDOW_BASE +
                           (uint64_t)slot_start * ACPI_PAGE_SIZE;
    record->page_count = page_count;
    return (void *)(uintptr_t)(record->virtual_base + offset);

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

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *message) {    
    // Voit myös halutessasi suodattaa tason mukaan (esim. jos level == UACPI_LOG_ERROR)
    print("[uACPI] ");
    print(message); // Tulostetaan uACPI:n oikea viesti "log message" -tekstin sijaan
    
    // uACPI:n viestit eivät yleensä sisällä rivinvaihtoa lopussa, joten lisätään se varmuuden vuoksi
    print("\n"); 
    scroll_screen();
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
    void *ptr = malloc(size);
    
    // Jos muisti loppuu, huudetaan täysillä!
    if (ptr == NULL) {
        print("\n[uACPI OSL] CRITICAL: malloc(%d) returned NULL!\n");
        print_int((int)size);
    }
    return ptr;
}

void uacpi_kernel_free(void *mem) {
    free(mem);
}

static inline uint64_t rdtsc(void);

uint64_t time_get_ns(void) {
    static uint64_t start_ticks;
    uint64_t ticks;

    ticks = rdtsc();
    if (start_ticks == 0)
        start_ticks = ticks;

    return (ticks - start_ticks) / 3ULL;
}

static inline uint64_t rdtsc(void)
{
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile (
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );

    return ((uint64_t)hi << 32) | lo;
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
  return time_get_ns();
}

void uacpi_kernel_stall(uacpi_u8 usec)
{
    uint64_t start = time_get_ns();

    while ((time_get_ns() - start) < ((uint64_t)usec * 1000ULL)) {
        __asm__ volatile ("pause");
    }
}

void uacpi_kernel_sleep(uacpi_u64 msec)
{
    uacpi_u64 start = time_get_ns();
    uacpi_u64 duration = msec * 1000000ULL;

    while ((time_get_ns() - start) < duration) {
        __asm__ volatile ("pause");
    }
}

typedef struct {
    uint8_t unused;
} tinos_uacpi_mutex_t;

uacpi_handle uacpi_kernel_create_mutex(void)
{
    tinos_uacpi_mutex_t *mutex =
        uacpi_kernel_alloc(sizeof(*mutex));

    return (uacpi_handle)mutex;
}

void uacpi_kernel_free_mutex(uacpi_handle handle)
{
    uacpi_kernel_free(handle);
}

typedef struct {
    volatile bool signaled;
} tinos_uacpi_event_t;

uacpi_handle uacpi_kernel_create_event(void)
{
    tinos_uacpi_event_t *event =
        malloc(sizeof(*event));

    if (!event)
        return NULL;

    event->signaled = false;

    return (uacpi_handle)event;
}

void uacpi_kernel_free_event(uacpi_handle handle)
{
    if (!handle)
        return;

    free(handle);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void)
{
    return (uacpi_thread_id)(uintptr_t)1;
}

uacpi_interrupt_state uacpi_kernel_disable_interrupts(void)
{
    uint64_t flags;

    asm volatile (
        "pushfq\n"
        "popq %0"
        : "=r"(flags)
        :
        : "memory"
    );

    asm volatile ("cli" ::: "memory");

    return (uacpi_interrupt_state)flags;
}

void uacpi_kernel_restore_interrupts(uacpi_interrupt_state state)
{
    uint64_t flags = (uint64_t)state;

    if (flags & (1ULL << 9))
        asm volatile ("sti" ::: "memory");
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle, uacpi_u16) {
    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle) {} // This doesn't need anything since we don't use mutexes in the kernel

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
     if (!handle) return UACPI_STATUS_INVALID_ARGUMENT;
    
    tinos_uacpi_event_t *ev = (tinos_uacpi_event_t*)handle;

    if (ev->signaled) {
        ev->signaled = UACPI_FALSE; 
        return UACPI_STATUS_OK;
    }

    if (timeout == 0) {
        return UACPI_STATUS_TIMEOUT;
    }
    return UACPI_STATUS_TIMEOUT;
}

void uacpi_kernel_signal_event(uacpi_handle handle) {
    if (!handle) {
        return; // Just return empty for void
    }
    
    tinos_uacpi_event_t *ev = (tinos_uacpi_event_t*)handle;
    ev->signaled = UACPI_TRUE;
}

void uacpi_kernel_reset_event(uacpi_handle handle) {
    if (handle) {
        tinos_uacpi_event_t *ev = (tinos_uacpi_event_t*)handle;
        ev->signaled = UACPI_FALSE;
    }
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *request) {
    // Tell uACPI we don't support firmware requests yet
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx,
    uacpi_handle *out_irq_handle
) {
    // For now, just allocate a small dummy handle or pass back a fake ID.
    // We treat the IRQ number itself as the handle for simplicity.
    *out_irq_handle = (uacpi_handle)(uintptr_t)irq;
    
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(
    uacpi_interrupt_handler handler, uacpi_handle irq_handle
) {
    // Nothing to do since the installation was a stub
    return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {

    return (uacpi_handle)0xBAD010C;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
    (void)handle;
}

// NOTE: These return a CPU status/flags type. 
// If uACPI expects them to return void or a type, adjust accordingly.
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
    return 0; // Return dummy flags
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
    // Nothing to do
}

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
    // Replace 0x12345678 with your actual found physical address of the RSDP
   uint8_t *bios_mem = (uint8_t*)0xE0000; // Map this to virtual memory if paging is on!
    
    for (size_t i = 0; i < 0x20000; i += 16) {
        if (uacpi_memcmp(&bios_mem[i], "RSD PTR ", 8) == 0) {
            // Found it! Calculate the physical address
            *out_rsdp_address = (uacpi_phys_addr)(0xE0000 + i);
            return UACPI_STATUS_OK;
        }
    }
    
    return UACPI_STATUS_NOT_FOUND;
}

uacpi_status uacpi_kernel_schedule_work(
    uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx
) {
    // Instead of queueing it for a background thread, just execute it right now!
    handler(ctx);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    // All work was executed immediately, so everything is already complete!
    return UACPI_STATUS_OK;
}

