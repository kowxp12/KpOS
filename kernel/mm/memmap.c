#include "memmap.h"
#include "vga.h"

#define TAG_END        0
#define TAG_MMAP       6
#define MMAP_AVAILABLE 1

/* Every tag in the info block starts with a type and a size */
struct mb_tag {
    unsigned int type;
    unsigned int size;
} __attribute__((packed));

/* The memory map tag: a header, then a list of entries */
struct mb_mmap_tag {
    unsigned int type;
    unsigned int size;
    unsigned int entry_size;
    unsigned int entry_version;
} __attribute__((packed));

struct mb_mmap_entry {
    unsigned long long base;
    unsigned long long length;
    unsigned int       type;      /* 1 = usable RAM */
    unsigned int       reserved;
} __attribute__((packed));

static void print_hex(unsigned int value)
{
    const char *digits = "0123456789ABCDEF";
    vga_print("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        vga_putchar(digits[(value >> shift) & 0xF]);
}

static void print_dec(unsigned int value)
{
    char buf[12];
    int i = 0;

    if (value == 0)
        buf[i++] = '0';
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    while (i > 0)
        vga_putchar(buf[--i]);
}

static void print_mmap(const struct mb_mmap_tag *tag)
{
    const unsigned char *p = (const unsigned char *)tag;
    const unsigned char *end = p + tag->size;
    unsigned int total_mb = 0;

    vga_print("Memory map:\n");

    /* Entries start right after the 16-byte tag header */
    for (const unsigned char *e = p + 16; e + tag->entry_size <= end;
         e += tag->entry_size) {
        const struct mb_mmap_entry *m = (const struct mb_mmap_entry *)e;

        /* Addresses are 64-bit, but this kernel runs at 32-bit, so the
           low 32 bits are enough to print and to size these regions */
        unsigned int base_lo = (unsigned int)m->base;
        unsigned int len_mb  = (unsigned int)(m->length / (1024 * 1024));

        vga_print("  ");
        print_hex(base_lo);
        vga_print("  ");
        print_dec(len_mb);
        vga_print(" MB  ");

        if (m->type == MMAP_AVAILABLE) {
            vga_print("usable\n");
            total_mb += len_mb;
        } else {
            vga_print("reserved\n");
        }
    }

    vga_print("Usable RAM: ");
    print_dec(total_mb);
    vga_print(" MB\n");
}

void memmap_init(unsigned int mbi_addr)
{
    const unsigned char *info = (const unsigned char *)(unsigned long)mbi_addr;

    /* Info block header: total size (4 bytes) and reserved (4 bytes) */
    unsigned int total_size = *(const unsigned int *)info;
    const unsigned char *p = info + 8;
    const unsigned char *end = info + total_size;

    /* Walk the tags until we reach the end tag or run out of space */
    while (p < end) {
        const struct mb_tag *tag = (const struct mb_tag *)p;

        if (tag->type == TAG_END)
            break;

        if (tag->type == TAG_MMAP) {
            print_mmap((const struct mb_mmap_tag *)tag);
            return;
        }

        /* Each tag is padded to a multiple of 8 bytes */
        p += (tag->size + 7) & ~7u;
    }

    vga_print("Memory map not found\n");
}