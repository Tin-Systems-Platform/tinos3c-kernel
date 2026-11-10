[BITS 64]

global gdt_flush
gdt_flush:
    ; The 10-byte GDTR operand is the first SysV AMD64 argument.
    lgdt [rdi]
    ; GRUB left the data segments at selector 0x18.  The kernel GDT uses
    ; selector 0x10 for its data descriptor; leaving SS stale makes the
    ; first interrupt return fault with #GP(0x18).
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    ret
