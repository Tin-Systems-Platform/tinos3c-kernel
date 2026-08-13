#include "pic.h"
#include <lib/std/stdio.h>
// PIC remap function
void pic_remap(int offset1, int offset2) {
    uint8_t a1, a2;
    
    a1 = 0xFF; 
    a2 = 0xFF;

    //remap PIC. For more info, see: os dev wiki.
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);

    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    io_wait();
    outb(PIC1_DATA, offset1);

    io_wait();
    outb(PIC2_DATA, offset2);

    io_wait();
    outb(PIC1_DATA, 4); 

    io_wait();
    outb(PIC2_DATA, 2); 

    io_wait();
    outb(PIC1_DATA, 0x01);

    io_wait();
    outb(PIC2_DATA, 0x01);


    io_wait();
    outb(PIC1_DATA, 0x0); 
    outb(PIC2_DATA, 0x0);
}