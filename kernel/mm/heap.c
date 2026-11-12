#include "heap.h"
#include "vga.h"

#define HEAP_SIZE  (256 * 1024)
#define ALIGN_MASK 15u           /* every block is a multiple of 16 bytes */

/* Each block starts with this header. It is padded to 16 bytes so the
   memory after it stays aligned. */
struct block {
    unsigned int  size;          /* usable bytes after the header */
    unsigned int  free;          /* 1 = free, 0 = in use */
    struct block *next;
    unsigned int  pad;
};

static unsigned char heap_mem[HEAP_SIZE] __attribute__((aligned(16)));
static struct block *head;

void heap_init(void)
{
    head = (struct block *)heap_mem;
    head->size = HEAP_SIZE - sizeof(struct block);
    head->free = 1;
    head->next = 0;
}

void *kmalloc(unsigned int size)
{
    if (size == 0)
        return 0;

    /* Round the request up to a multiple of 16 */
    size = (size + ALIGN_MASK) & ~ALIGN_MASK;

    for (struct block *b = head; b; b = b->next) {
        if (!b->free || b->size < size)
            continue;

        /* Split the block if there's room for another block after this one */
        if (b->size >= size + sizeof(struct block) + 16) {
            struct block *nb = (struct block *)((unsigned char *)(b + 1) + size);

            nb->size = b->size - size - sizeof(struct block);
            nb->free = 1;
            nb->next = b->next;

            b->size = size;
            b->next = nb;
        }

        b->free = 0;
        return b + 1;   /* memory starts right after the header */
    }

    return 0;           /* out of heap memory */
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    struct block *b = (struct block *)ptr - 1;
    b->free = 1;

    /* Merge with following free blocks to reduce fragmentation */
    while (b->next && b->next->free) {
        b->size += sizeof(struct block) + b->next->size;
        b->next = b->next->next;
    }
}

static void print_hex(unsigned int value)
{
    const char *digits = "0123456789ABCDEF";
    vga_print("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        vga_putchar(digits[(value >> shift) & 0xF]);
}

void heap_selftest(void)
{
    void *a = kmalloc(100);
    void *b = kmalloc(2000);

    vga_print("HEAP: a = ");
    print_hex((unsigned int)(unsigned long)a);
    vga_print("  b = ");
    print_hex((unsigned int)(unsigned long)b);
    vga_putchar('\n');

    /* Free a, then allocate something smaller. It should reuse a's spot. */
    kfree(a);
    void *c = kmalloc(50);

    vga_print("HEAP: c = ");
    print_hex((unsigned int)(unsigned long)c);
    vga_print(c == a ? " (reused a)\n" : " (new spot)\n");

    kfree(b);
    kfree(c);
}