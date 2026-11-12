#include "vga.h"
#include "io.h"

#define KEYBOARD_IRQ   33   /* IRQ1 (32 + 1) */
#define KEYBOARD_PORT  0x60

static void print_hex(unsigned char value)
{
    const char *digits = "0123456789ABCDEF";
    vga_putchar(digits[value >> 4]);
    vga_putchar(digits[value & 0x0F]);
}

void irq_handler(unsigned int irq)
{
    if (irq == KEYBOARD_IRQ) {
        unsigned char scancode = inb(KEYBOARD_PORT);

        /* Bit 7 set = key released. Only print presses. */
        if (!(scancode & 0x80)) {
            vga_print("Key scancode: 0x");
            print_hex(scancode);
            vga_putchar('\n');
        }
    }

    /* Tell the PIC the interrupt is handled */
    if (irq >= 40)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}