#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <lib/std/stdint.h>
#include <lib/std/stdio.h>

struct regs {
    uint64_t rax, rbx, rcx, rdx, rbp, rdi, rsi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

void isr_handler(struct regs *r);

#endif
