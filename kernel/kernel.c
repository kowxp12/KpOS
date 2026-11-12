/*
KpOS Kernel v0.0.2
*/
#include "vga.h"
#include "idt.h"
#include "pic.h"
#include "shell.h"

void kernel_main(unsigned int magic, unsigned int mbi_addr)
{
    (void)mbi_addr;

    vga_clear();

    if (magic != 0x36D76289) {
        vga_print("KpOS: Invalid boot magic.\n");
        for (;;)
            __asm__ volatile ("hlt");
    }

    vga_print("KpOS Kernel\n");
    vga_print("System initialized.\n");

    idt_init();
    pic_init();
    __asm__ volatile ("sti");   /* turn interrupts on */

    shell_init();

    for (;;)
        __asm__ volatile ("hlt");
}
