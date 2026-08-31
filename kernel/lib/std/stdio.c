#include <lib/std/stdio.h>
#include <lib/std/stdint.h>
#include <lib/scancodes.h>

#include <drivers/keyboard/kb.h>

// I/O port functions inb and outb
unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

//wait for I/O operation to complete
void io_wait(void) {
    asm volatile ( "outb %%al, $0x80" : : "a"(0) );
}

// VGA stuff
#define VGA_WIDTH 80
#define VGA_HEIGHT 60

#define VGA_MEMORY ((unsigned short*)0xb8000)

#define DEFAULT_COLOR 0x07

volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;

int cursor_x = 0;
int cursor_y = 0;

// put 1 charapter on the screen
void putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(' ', DEFAULT_COLOR);
        }
    } else {
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(c, DEFAULT_COLOR);
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= VGA_HEIGHT) {
        scroll_screen();
        cursor_y = VGA_HEIGHT - 1;
    }

    update_cursor(cursor_x, cursor_y);
}

//scroll screen one line up
void scroll_screen() {
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[(y - 1) * VGA_WIDTH + x] =
                vga_buffer[y * VGA_WIDTH + x];
        }
    }

    // Clear last line
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            vga_entry(' ', 0x07);
    }

    // Move cursor up with the screen
    if (cursor_y > 0) {
        cursor_y--;
    }
}

//print string
void print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        putchar(str[i]);
    }
}

//update cursor pos
void update_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}


//clear screen
void clear_screen() {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', DEFAULT_COLOR);
        }
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor(cursor_x, cursor_y);
}

uint16_t vga_entry(char c, uint8_t color) {
    return ((uint16_t)color << 8) | (uint8_t)c;
}

//take user imput
void scanf(char* buffer, size_t max_len) {
    size_t i = 0;
    while (i < max_len - 1) {
        uint8_t scancode = keyboard_read_scancode();
        
        if (scancode == 0x1C) {
            break;
        } else if (scancode == 0x0E && i > 0) {
            i--;
            buffer[i] = '\0';
        } else {
            char c = scancode_to_ascii(scancode);
            if (c) {
                buffer[i++] = c;
            }
        }
    }
    buffer[i] = '\0';
}

//string compare
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

//get a charapter from user input
char getchar() {
    uint8_t scancode = 0;
    while ((scancode = kbd_pop()) == 0) {
        asm volatile("hlt"); 
    }
    return scancode_to_ascii(scancode);
}

void print_int(int num) {
    char buf[16];
    int i = 0;

    if (num == 0) {
        print("0");
        return;
    }

    if (num < 0) {
        print("-");
        num = -num;
    }

    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }

    // reverse
    for (int j = i - 1; j >= 0; j--) {
        char c[2] = {buf[j], 0};
        print(c);
    }
}

void print_hex(uint64_t value)
{
    const char* hex = "0123456789ABCDEF";

    print("0x");

    for (int i = 15; i >= 0; i--)
    {
        putchar(hex[(value >> (i * 4)) & 0xF]);
    }
}

void print_uint64(uint64_t value)
{
    char buffer[21];
    int i = 20;

    buffer[i] = '\0';

    if (value == 0)
    {
        print("0");
        return;
    }

    while (value > 0)
    {
        buffer[--i] = '0' + (value % 10);
        value /= 10;
    }

    print(&buffer[i]);
}