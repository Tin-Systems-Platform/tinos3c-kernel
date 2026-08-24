[BITS 32]

global _start
extern _main

section .text.prologue

_start:
    cli

    ; Multiboot v1 enters in 32-bit mode with magic in EAX and the physical
    ; multiboot-info pointer in EBX.  Preserve them across page-table setup.
    mov [boot_magic], eax
    mov [boot_info], ebx

    ; Set up a temporary 32-bit stack.
    mov esp, stack_top

    ; ---------------------------------------------------------
    ; Set up identity-mapped paging for the first 2 MiB.
    ;
    ; PML4[0] -> PDPT
    ; PDPT[0] -> PD
    ; PD[0]   -> 2 MiB page
    ; ---------------------------------------------------------

    mov eax, pdpt
    or eax, 0x03
    mov [pml4], eax

    mov eax, pd
    or eax, 0x03
    mov [pdpt], eax

    ; Present + writable + 2 MiB page
    mov eax, 0x00000083
    mov [pd], eax

    ; Keep the complete kernel image and early PMM bitmap reachable.
    mov eax, 0x00200083
    mov [pd + 8], eax
    mov eax, 0x00400083
    mov [pd + 16], eax
    mov eax, 0x00600083
    mov [pd + 24], eax

    ; Load PML4 into CR3
    mov eax, pml4
    mov cr3, eax

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Enable long mode in EFER
    mov ecx, 0xC0000080       ; IA32_EFER
    rdmsr
    or eax, 1 << 8            ; LME
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, 1 << 31           ; PG
    mov cr0, eax

    ; Load 64-bit GDT
    lgdt [gdt64.pointer]

    ; Far jump into 64-bit code
    jmp gdt64.code:long_mode_entry


[BITS 64]

long_mode_entry:
    ; Now actually executing in long mode.

    ; Set up a 64-bit stack.
    mov rsp, stack_top

    ; Clear direction flag.
    cld

    ; First two SysV AMD64 arguments: multiboot info and magic.
    mov edi, [rel boot_info]
    mov esi, [rel boot_magic]
    call _main

.hang:
    hlt
    jmp .hang




align 8

gdt64:
    dq 0x0000000000000000     ; Null descriptor

    ; 64-bit code descriptor
    dq 0x00AF9A000000FFFF

    ; Data descriptor
    dq 0x00AF92000000FFFF

.pointer:
    dw .pointer - gdt64 - 1
    dq gdt64

.code equ 0x08




align 4096

pml4:
    times 512 dq 0

pdpt:
    times 512 dq 0

pd:
    times 512 dq 0




section .bss

align 16

stack_bottom:
    resb 8192

stack_top:

align 8
boot_magic:
    resd 1
boot_info:
    resd 1
