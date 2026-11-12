#include "vga.h"
#include "io.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25

static volatile unsigned short *const vga_mem =
    (volatile unsigned short *)0xB8000;

static unsigned int cursor_row = 0;
static unsigned int cursor_col = 0;

static unsigned int scroll_count = 0;
static int error_seen = 0;

static unsigned char current_color = 0x0F;

static unsigned short make_entry(char c)
{
    return (unsigned short)((current_color << 8) | (unsigned char)c);
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
    scroll_count++;
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
    scroll_count += 1000;
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
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = VGA_WIDTH - 1;
        }
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

void vga_set_color(unsigned char color)
{
    current_color = color & 0x0F;
}

void vga_cprint(const char *s, unsigned char color)
{
    unsigned char old = current_color;

    current_color = color & 0x0F;
    vga_print(s);
    current_color = old;
}

void vga_eprint(const char *s)
{
    error_seen = 1;
    vga_cprint(s, VGA_COLOR_ERROR);
}

void vga_wprint(const char *s)
{
    error_seen = 1;
    vga_cprint(s, VGA_COLOR_WARN);
}

void vga_lprint(const char *s)
{
    error_seen = 1;
    vga_cprint(s, VGA_COLOR_USAGE);
}

void vga_get_cursor(unsigned int *row, unsigned int *col)
{
    *row = cursor_row;
    *col = cursor_col;
}

int vga_error_seen(void)
{
    return error_seen;
}

void vga_reset_error(void)
{
    error_seen = 0;
}

unsigned int vga_scroll_count(void)
{
    return scroll_count;
}

/* Write text ending at the right edge of a row, without moving the cursor */
void vga_put_right(int row, const char *s, unsigned char color)
{
    if (row < 0 || row >= VGA_HEIGHT)
        return;

    unsigned int len = 0;
    while (s[len])
        len++;
    if (len > VGA_WIDTH)
        return;

    unsigned int col = VGA_WIDTH - len;
    for (unsigned int i = 0; i < len; i++)
        vga_mem[row * VGA_WIDTH + col + i] =
            (unsigned short)((color << 8) | (unsigned char)s[i]);
}

/* Write one character at an absolute screen index, without scrolling */
void vga_put_at(unsigned int index, char c, unsigned char color)
{
    if (index >= VGA_WIDTH * VGA_HEIGHT)
        return;
    vga_mem[index] = (unsigned short)((color << 8) | (unsigned char)c);
}

/* Move the blinking cursor to an absolute screen index */
void vga_set_cursor_index(unsigned int index)
{
    if (index >= VGA_WIDTH * VGA_HEIGHT)
        index = VGA_WIDTH * VGA_HEIGHT - 1;
    cursor_row = index / VGA_WIDTH;
    cursor_col = index % VGA_WIDTH;
    update_cursor();
}