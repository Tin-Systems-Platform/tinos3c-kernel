[BITS 32]
[extern _main]

section .text.prologue

global _start
_start:
    cli
    mov esp, stack_top

    push eax
    push ebx

    call _main
hltLoop:
    hlt
    jmp hltLoop

section .bss

align 16
stack_bottom:
resb 8192
stack_top: