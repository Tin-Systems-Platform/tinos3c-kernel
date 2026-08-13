#ifndef STDIO_H
#define STDIO_H

#include <lib/std/stdint.h>

typedef unsigned int size_t;

unsigned char inb(unsigned short port);

void outb(unsigned short port, unsigned char val);
void io_wait(void);

void scroll_screen();
void putchar(char c);

void print(const char *str);
void update_cursor(int x, int y);

void clear_screen();
uint16_t vga_entry(char c, uint8_t color);

void scanf(char* buffer, size_t max_len);
int strcmp(const char* s1, const char* s2);

char getchar();
void print_int(int num);

void print_hex(uint64_t value);
void print_uint64(uint64_t value);
#endif