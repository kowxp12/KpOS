#include "pic.h"
#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static void io_wait(void)
{
    outb(0x80, 0);   /* writing to an unused port adds a tiny delay */
}

void pic_init(void)
{
    /* Start initialization sequence */
    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();

    /* New interrupt numbers: master starts at 32, slave at 40 */
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    /* Tell them how they're wired together */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* 8086 mode */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* Unmask all IRQs (0 = enabled) */
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}