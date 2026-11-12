#include "vga.h"
#include "io.h"
#include "keyboard.h"

#define KEYBOARD_IRQ   33   /* IRQ1 (32 + 1) */
#define KEYBOARD_PORT  0x60

void irq_handler(unsigned int irq)
{
    if (irq == KEYBOARD_IRQ) {
        unsigned char scancode = inb(KEYBOARD_PORT);
        keyboard_handle(scancode);
    }

    /* Tell the PIC the interrupt is handled */
    if (irq >= 40)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}