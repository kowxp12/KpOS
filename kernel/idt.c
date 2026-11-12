#include "idt.h"

struct idt_entry {
    unsigned short base_low;
    unsigned short selector;
    unsigned char  zero;
    unsigned char  flags;
    unsigned short base_high;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

#define IDT_SIZE 256

static struct idt_entry idt[IDT_SIZE];
static struct idt_ptr   idtp;

extern unsigned int irq_stub_table[16];

static void idt_set_gate(unsigned char num, unsigned int handler)
{
    unsigned short cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));

    idt[num].base_low  = handler & 0xFFFF;
    idt[num].base_high = (handler >> 16) & 0xFFFF;
    idt[num].selector  = cs;     /* use the CS GRUB gave us (0x10) */
    idt[num].zero      = 0;
    idt[num].flags     = 0x8E;   /* present, ring 0, 32-bit interrupt gate */
}

void idt_init(void)
{
    for (unsigned int i = 0; i < 16; i++)
        idt_set_gate(32 + i, irq_stub_table[i]);

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (unsigned int)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idtp));
}