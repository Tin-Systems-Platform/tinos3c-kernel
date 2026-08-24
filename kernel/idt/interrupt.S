[BITS 64]
extern isr_handler
global isr0
global isr1
global isr_common_stub

%macro ISR_NOERRCODE 1
global isr%1        
isr%1:              
    push 0
    push %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push %1
    jmp isr_common_stub
%endmacro


ISR_ERRCODE 14
ISR_NOERRCODE 32
ISR_NOERRCODE 33


isr0: jmp isr32
isr1: jmp isr33

isr_common_stub:
    ; Save every general-purpose register.  The layout is mirrored by
    ; struct regs in interrupt.h, with RAX at the beginning of the frame.
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rsi
    push rdi
    push rbp
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp
    cld
    call isr_handler

    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rbp
    pop rdi
    pop rsi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    add rsp, 16
    iretq

section .text
global idt_flush
idt_flush:
    lidt [rdi]
    ret
