#ifndef VGA_H
#define VGA_H

#define VGA_COLOR_ERROR  0x0C /* light red */
#define VGA_COLOR_WARN   0x0E /* yellow */
#define VGA_COLOR_USAGE  0x0B /* light cyan */
#define VGA_COLOR_OK     0x0A /* light green */

void vga_clear(void);
void vga_putchar(char c);
void vga_print(const char *s);
void vga_set_color(unsigned char color);

void vga_cprint(const char *s, unsigned char color);
void vga_eprint(const char *s);
void vga_wprint(const char *s);
void vga_lprint(const char *s);

void vga_reset_error(void);
void vga_get_cursor(unsigned int *row, unsigned int *col);
void vga_put_right(int row, const char *s, unsigned char color);

void vga_put_at(unsigned int index, char c, unsigned char color);
void vga_set_cursor_index(unsigned int index);

unsigned int vga_scroll_count(void);

int vga_error_seen(void);

#endif