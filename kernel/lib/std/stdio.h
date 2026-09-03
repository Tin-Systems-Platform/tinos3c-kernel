#ifndef STDIO_H
#define STDIO_H

#include <lib/std/stdint.h>

unsigned char inb(unsigned short port);

void outb(unsigned short port, unsigned char val);
void io_wait(void);

void scroll_screen();
void putchar(char c);

void printf(const char *fmt, ...);
void update_cursor(int x, int y);

void clear_screen();
uint16_t vga_entry(char c, uint8_t color);

void scanf(char* buffer, size_t max_len);
int strcmp(const char* s1, const char* s2);

char getchar();


static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}
#endif