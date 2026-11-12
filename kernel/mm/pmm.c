#include "pmm.h"
#include "vga.h"

#define PAGE_SIZE   4096u
#define PAGE_COUNT  1048576u      /* 4 GB / 4 KB */
#define TAG_END     0
#define TAG_MMAP    6
#define MMAP_USABLE 1

extern char _kernel_end[];        /* defined in linker.ld */

static unsigned char bitmap[PAGE_COUNT / 8];   /* 1 bit per page, 1 = in use */
static unsigned int free_count = 0;
static unsigned int next_hint = 0;

struct mb_tag {
    unsigned int type;
    unsigned int size;
} __attribute__((packed));

struct mb_mmap_tag {
    unsigned int type;
    unsigned int size;
    unsigned int entry_size;
    unsigned int entry_version;
} __attribute__((packed));

struct mb_mmap_entry {
    unsigned long long base;
    unsigned long long length;
    unsigned int       type;
    unsigned int       reserved;
} __attribute__((packed));

static void set_bit(unsigned int page, int used)
{
    if (used)
        bitmap[page / 8] |= (unsigned char)(1u << (page % 8));
    else
        bitmap[page / 8] &= (unsigned char)~(1u << (page % 8));
}

static int is_used(unsigned int page)
{
    return (bitmap[page / 8] >> (page % 8)) & 1;
}

void pmm_init(unsigned int mbi_addr)
{
    const unsigned char *info = (const unsigned char *)mbi_addr;
    unsigned int total_size = *(const unsigned int *)info;
    const unsigned char *p = info + 8;
    const unsigned char *end = info + total_size;

    /* Start with every page marked used, then free only the safe ones */
    for (unsigned int i = 0; i < sizeof(bitmap); i++)
        bitmap[i] = 0xFF;

    while (p < end) {
        const struct mb_tag *tag = (const struct mb_tag *)p;

        if (tag->type == TAG_END)
            break;

        if (tag->type == TAG_MMAP) {
            const struct mb_mmap_tag *mt = (const struct mb_mmap_tag *)tag;
            const unsigned char *e = p + 16;
            const unsigned char *tag_end = p + mt->size;

            while (e + mt->entry_size <= tag_end) {
                const struct mb_mmap_entry *m = (const struct mb_mmap_entry *)e;

                if (m->type == MMAP_USABLE) {
                    unsigned long long start = m->base;
                    unsigned long long stop  = m->base + m->length;

                    /* This kernel only manages the first 4 GB */
                    if (stop > 0x100000000ULL)
                        stop = 0x100000000ULL;

                    /* Round the start up to a page boundary */
                    start = (start + PAGE_SIZE - 1) & ~(unsigned long long)(PAGE_SIZE - 1);

                    for (unsigned long long addr = start;
                         addr + PAGE_SIZE <= stop; addr += PAGE_SIZE) {

                        /* Skip the low 1 MB and everything the kernel occupies */
                        if (addr < 0x100000ULL)
                            continue;
                        if (addr < (unsigned long)_kernel_end)
                            continue;

                        set_bit((unsigned int)(addr / PAGE_SIZE), 0);
                        free_count++;
                    }
                }

                e += mt->entry_size;
            }
            break;
        }

        p += (tag->size + 7) & ~7u;
    }
}

void *pmm_alloc_page(void)
{
    for (unsigned int i = 0; i < PAGE_COUNT; i++) {
        unsigned int page = (next_hint + i) % PAGE_COUNT;

        if (!is_used(page)) {
            set_bit(page, 1);
            free_count--;
            next_hint = page + 1;
            return (void *)(unsigned long)(page * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_page(void *ptr)
{
    unsigned int page = (unsigned int)(unsigned long)ptr / PAGE_SIZE;

    if (is_used(page)) {
        set_bit(page, 0);
        free_count++;
    }
}

unsigned int pmm_free_pages(void)
{
    return free_count;
}

static void print_hex(unsigned int value)
{
    const char *digits = "0123456789ABCDEF";
    vga_print("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        vga_putchar(digits[(value >> shift) & 0xF]);
}

void pmm_selftest(void)
{
    void *a = pmm_alloc_page();
    void *b = pmm_alloc_page();

    vga_print("PMM: alloc A = ");
    print_hex((unsigned int)(unsigned long)a);
    vga_print("  alloc B = ");
    print_hex((unsigned int)(unsigned long)b);
    vga_putchar('\n');

    pmm_free_page(a);
    pmm_free_page(b);
    vga_print("PMM: pages freed, free count = ");
    print_hex(pmm_free_pages());
    vga_putchar('\n');
}