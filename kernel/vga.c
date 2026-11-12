#include "vga.h"
#include "io.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_COLOR   0x02

static volatile unsigned short *const vga_mem =
    (volatile unsigned short *)0xB8000;

static unsigned int cursor_row = 0;
static unsigned int cursor_col = 0;

static unsigned short make_entry(char c)
{
    return (unsigned short)((VGA_COLOR << 8) | (unsigned char)c);
}

/* Move the blinking hardware cursor to match cursor_row / cursor_col */
static void update_cursor(void)
{
    unsigned short pos = cursor_row * VGA_WIDTH + cursor_col;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

/* Move every row up by one, then blank the botton row. */
static void scroll(void)
{
    for (unsigned int row = 1; row < VGA_HEIGHT; row++)
        for (unsigned int col = 0; col < VGA_WIDTH; col++)
            vga_mem[(row - 1) * VGA_WIDTH + col] =
                vga_mem[row * VGA_WIDTH + col];
    
    for (unsigned int col = 0; col < VGA_WIDTH; col++)
        vga_mem[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = make_entry(' ');
    
    cursor_row = VGA_HEIGHT - 1;
}

void vga_clear(void)
{
    for (unsigned int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga_mem[i] = make_entry(' ');

    cursor_row = 0;
    cursor_col = 0;
    update_cursor();
}

static void newline(void)
{
    cursor_col = 0;
    cursor_row++;
    if (cursor_row == VGA_HEIGHT)
        scroll();
}

void vga_putchar(char c)
{
    if (c == '\n') {
        newline();
    } else if (c == '\b') {
        if (cursor_col > 0)
            cursor_col--;
        vga_mem[cursor_row * VGA_WIDTH + cursor_col] = make_entry(' ');
    } else {
        vga_mem[cursor_row * VGA_WIDTH + cursor_col] = make_entry(c);
        cursor_col++;
        if (cursor_col == VGA_WIDTH)
            newline();
    }

    update_cursor();
}

void vga_print(const char *s)
{
    while (*s)
    {
        vga_putchar(*s++);
    }
}