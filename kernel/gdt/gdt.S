[BITS 64]

global gdt_flush
gdt_flush:
    ; The 10-byte GDTR operand is the first SysV AMD64 argument.
    lgdt [rdi]
    ret
