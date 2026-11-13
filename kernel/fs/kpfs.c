#include "kpfs.h"
#include "block.h"
#include "vga.h"
#include "random.h"

#define KPFS_VERSION        1
#define KPFS_SECTOR         512
#define FT_DIVISOR          256
#define FT_MIN              1
#define FT_MAX              64
#define ENTRY_SIZE          64
#define ENTRIES_PER_SECTOR  (KPFS_SECTOR / ENTRY_SIZE)
#define NAME_MAX_LEN        15

/* Superblock, as described in section 4 of the spec */
struct kpfs_super {
    char     magic[4];          /* "KPFS" */
    uint32_t version;
    uint32_t sector_size;
    uint64_t total_sectors;
    uint64_t total_bytes;
    uint64_t sb_start;
    uint64_t sb_size;
    uint64_t ft_start;
    uint64_t ft_size;
    uint64_t data_start;
    uint64_t fs_id;
    uint8_t  reserved[KPFS_SECTOR - 76];
} __attribute__((packed));

/* One file table entry: 64 bytes, 8 per sector */
struct kpfs_entry {
    char     name[16];
    uint64_t start;            /* first data sector */
    uint64_t size;             /* file size in bytes */
    uint64_t alloc;            /* sectors used by the file */
    uint8_t  used;             /* 1 = in use */
    uint8_t  pad[ENTRY_SIZE - 41];
} __attribute__((packed));

static uint8_t io[KPFS_SECTOR];        /* shared sector buffer */
static struct kpfs_super cur;          /* superblock loaded from disk */
static uint32_t entry_count = 0;

static void copy_bytes(void *dst, const void *src, unsigned int n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (unsigned int i = 0; i < n; i++)
        d[i] = s[i];
}

static unsigned int str_len(const char *s)
{
    unsigned int n = 0;
    while (s[n])
        n++;
    return n;
}

/* Names must be 1-15 characters, with no spaces or '/' */
static int valid_name(const char *name)
{
    unsigned int n = str_len(name);

    if (n == 0 || n > NAME_MAX_LEN)
        return 0;
    for (unsigned int i = 0; i < n; i++)
        if (name[i] == '/' || name[i] == ' ')
            return 0;
    return 1;
}

/* Compare a stored 16-byte name with a NUL-terminated name */
static int name_eq(const char *stored, const char *name)
{
    for (unsigned int i = 0; i < 16; i++) {
        if (stored[i] != name[i])
            return 0;
        if (!name[i])
            return 1;
    }
    return 0;
}

/* Read and validate the superblock. Returns 0 if the disk is usable. */
static int load_super(void)
{
    if (block_init() != 0)
        return -1;
    if (block_read(0, io) != 0)
        return -1;

    copy_bytes(&cur, io, sizeof(cur));

    if (cur.magic[0] != 'K' || cur.magic[1] != 'P' ||
        cur.magic[2] != 'F' || cur.magic[3] != 'S')
        return -1;
    if (cur.version != KPFS_VERSION)
        return -1;
    if (cur.total_sectors != block_sectors())
        return -1;

    entry_count = (uint32_t)(cur.ft_size * ENTRIES_PER_SECTOR);
    return 0;
}

/* Like load_super, but prints a message when the disk isn't ready */
static int need_fs(void)
{
    if (load_super() != 0) {
        vga_wprint("No formatted KPFS disk. Type: format yes\n");
        return -1;
    }
    return 0;
}

static int entry_read(uint32_t i, struct kpfs_entry *e)
{
    uint64_t sector = cur.ft_start + i / ENTRIES_PER_SECTOR;
    if (block_read(sector, io) != 0)
        return -1;
    copy_bytes(e, io + (i % ENTRIES_PER_SECTOR) * ENTRY_SIZE, ENTRY_SIZE);
    return 0;
}

static int entry_write(uint32_t i, const struct kpfs_entry *e)
{
    uint64_t sector = cur.ft_start + i / ENTRIES_PER_SECTOR;
    if (block_read(sector, io) != 0)
        return -1;
    copy_bytes(io + (i % ENTRIES_PER_SECTOR) * ENTRY_SIZE, e, ENTRY_SIZE);
    return block_write(sector, io);
}

/* Find a used entry by name. Returns 0 and fills idx and e if found. */
static int find_entry(const char *name, uint32_t *idx, struct kpfs_entry *e)
{
    for (uint32_t i = 0; i < entry_count; i++) {
        if (entry_read(i, e) != 0)
            return -1;
        if (e->used && name_eq(e->name, name)) {
            *idx = i;
            return 0;
        }
    }
    return -1;
}

/* Find the first free run of 'need' sectors inside the data region.
   Any file's sectors count as occupied, including a file being replaced. */
static int find_run(uint64_t need, uint64_t *start_out)
{
    uint64_t s = cur.data_start;

    for (;;) {
        if (s + need > cur.total_sectors)
            return -1;

        int bumped = 0;
        for (uint32_t i = 0; i < entry_count && !bumped; i++) {
            struct kpfs_entry e;
            if (entry_read(i, &e) != 0)
                return -1;
            if (!e.used || e.alloc == 0)
                continue;

            uint64_t end = e.start + e.alloc;
            if (s < end && e.start < s + need) {
                s = end;        /* jump past the conflicting file */
                bumped = 1;
            }
        }
        if (!bumped) {
            *start_out = s;
            return 0;
        }
    }
}

int kpfs_format(void)
{
    if (block_init() != 0) {
        vga_eprint("No disk found. Run 'disks' to check.\n");
        return -1;
    }

    uint64_t total = block_sectors();
    if (total < 16) {
        vga_eprint("Disk is too small for KPFS.\n");
        return -1;
    }

    uint64_t ft = total / FT_DIVISOR;
    if (ft < FT_MIN)
        ft = FT_MIN;
    if (ft > FT_MAX)
        ft = FT_MAX;

    uint64_t data_start = 1 + ft;
    if (data_start >= total) {
        vga_eprint("Disk is too small for the file table.\n");
        return -1;
    }

    /* Build the superblock in its own variable, not in the shared buffer */
    struct kpfs_super newsb;
    for (unsigned int i = 0; i < sizeof(newsb); i++)
        ((uint8_t *)&newsb)[i] = 0;

    newsb.magic[0] = 'K';
    newsb.magic[1] = 'P';
    newsb.magic[2] = 'F';
    newsb.magic[3] = 'S';
    newsb.version = KPFS_VERSION;
    newsb.sector_size = KPFS_SECTOR;
    newsb.total_sectors = total;
    newsb.total_bytes = total * KPFS_SECTOR;
    newsb.sb_start = 0;
    newsb.sb_size = 1;
    newsb.ft_start = 1;
    newsb.ft_size = ft;
    newsb.data_start = data_start;

    uint32_t hi = random_next();
    uint32_t lo = random_next();
    newsb.fs_id = ((uint64_t)hi << 32) | lo;

    /* Step 1: erase the file table */
    for (unsigned int i = 0; i < KPFS_SECTOR; i++)
        io[i] = 0;
    for (uint64_t s = 1; s < data_start; s++) {
        if (block_write(s, io) != 0) {
            vga_eprint("Failed to erase the file table.\n");
            return -1;
        }
    }

    /* Step 2: erase the file data, so old file contents are gone for good */
    vga_print("Erasing data");
    for (uint64_t s = data_start; s < total; s++) {
        if (block_write(s, io) != 0) {
            vga_eprint("Failed to erase the data region.\n");
            return -1;
        }
        if ((s - data_start) % 256 == 255)
            vga_putchar('.');
    }
    vga_putchar('\n');

    /* Step 3: write the superblock last. Until this succeeds, the disk is not formatted. */
    copy_bytes(io, &newsb, sizeof(newsb));
    if (block_write(0, io) != 0) {
        vga_eprint("Failed to write the superblock.\n");
        return -1;
    }

    vga_print("Formatted as KPFS v1.\n");
    vga_print("  Total sectors: ");
    vga_print_u64(total);
    vga_print(" (");
    vga_print_u64(total >> 11);
    vga_print(" MB)\n");
    vga_print("  Superblock:    sector 0\n");
    vga_print("  File table:    sectors 1-");
    vga_print_u64(ft);
    vga_print("\n");
    vga_print("  File data:     from sector ");
    vga_print_u64(data_start);
    vga_print("\n");
    return 0;
}

int kpfs_probe(void)
{
    return load_super() == 0;
}

int kpfs_write(const char *name, const char *data, unsigned int len)
{
    if (need_fs() != 0)
        return -1;

    if (!valid_name(name)) {
        vga_wprint("Invalid name: 1-15 characters, no spaces or '/'.\n");
        return -1;
    }

    /* Find an existing file to replace, or a free slot for a new one */
    uint32_t idx = 0;
    struct kpfs_entry e;
    int exists = (find_entry(name, &idx, &e) == 0);

    if (!exists) {
        int found = 0;
        for (uint32_t i = 0; i < entry_count; i++) {
            if (entry_read(i, &e) != 0) {
                vga_eprint("Read error.\n");
                return -1;
            }
            if (!e.used) {
                idx = i;
                found = 1;
                break;
            }
        }
        if (!found) {
            vga_wprint("File table is full.\n");
            return -1;
        }
    }

    uint64_t need = ((uint64_t)len + KPFS_SECTOR - 1) / KPFS_SECTOR;
    uint64_t start = 0;

    if (need > 0 && find_run(need, &start) != 0) {
        vga_wprint("Not enough free space.\n");
        return -1;
    }

    /* Step 1: write the new data to free sectors. The old file is still untouched. */
    for (uint64_t k = 0; k < need; k++) {
        uint64_t off = k * KPFS_SECTOR;
        uint64_t left = (uint64_t)len - off;
        unsigned int n = left > KPFS_SECTOR ? KPFS_SECTOR : (unsigned int)left;

        for (unsigned int i = 0; i < KPFS_SECTOR; i++)
            io[i] = 0;
        copy_bytes(io, data + off, n);

        if (block_write(start + k, io) != 0) {
            vga_eprint("Write failed. The old file is unchanged.\n");
            return -1;
        }
    }

    /* Step 2: commit by updating the file table entry in one sector write */
    struct kpfs_entry ne;
    copy_bytes(&ne, "\0", 0);
    for (unsigned int i = 0; i < ENTRY_SIZE; i++)
        ((uint8_t *)&ne)[i] = 0;

    for (unsigned int i = 0; i < NAME_MAX_LEN && name[i]; i++)
        ne.name[i] = name[i];
    ne.start = start;
    ne.size = len;
    ne.alloc = need;
    ne.used = 1;

    if (entry_write(idx, &ne) != 0) {
        vga_eprint("Could not update the file table. The old file is unchanged.\n");
        return -1;
    }

    vga_print(exists ? "Replaced " : "Saved ");
    vga_print(name);
    vga_print(" (");
    vga_print_u64(len);
    vga_print(" bytes)\n");
    return 0;
}

int kpfs_read(const char *name)
{
    if (need_fs() != 0)
        return -1;

    uint32_t idx = 0;
    struct kpfs_entry e;
    if (find_entry(name, &idx, &e) != 0) {
        vga_wprint("No such file: ");
        vga_wprint(name);
        vga_putchar('\n');
        return -1;
    }

    /* Read only as many bytes as the file size, not the whole sectors */
    uint64_t remaining = e.size;
    for (uint64_t k = 0; k < e.alloc && remaining > 0; k++) {
        if (block_read(e.start + k, io) != 0) {
            vga_eprint("Read failed.\n");
            return -1;
        }

        unsigned int n = remaining > KPFS_SECTOR ? KPFS_SECTOR : (unsigned int)remaining;
        for (unsigned int i = 0; i < n; i++) {
            char c = (char)io[i];
            if (c == '\n' || (c >= 0x20 && c <= 0x7E))
                vga_putchar(c);
            else
                vga_putchar('.');
        }
        remaining -= n;
    }
    vga_putchar('\n');
    return 0;
}

int kpfs_remove(const char *name)
{
    if (need_fs() != 0)
        return -1;

    uint32_t idx = 0;
    struct kpfs_entry e;
    if (find_entry(name, &idx, &e) != 0) {
        vga_wprint("No such file: ");
        vga_wprint(name);
        vga_putchar('\n');
        return -1;
    }

    e.used = 0;
    if (entry_write(idx, &e) != 0) {
        vga_eprint("Could not update the file table.\n");
        return -1;
    }

    vga_print("Deleted ");
    vga_print(name);
    vga_putchar('\n');
    return 0;
}

int kpfs_list(void)
{
    if (need_fs() != 0)
        return -1;

    unsigned int count = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        struct kpfs_entry e;
        if (entry_read(i, &e) != 0) {
            vga_eprint("Read error.\n");
            return -1;
        }
        if (!e.used)
            continue;

        vga_print("  ");
        vga_print(e.name);
        vga_print("  ");
        vga_print_u64(e.size);
        vga_print(" bytes\n");
        count++;
    }

    if (count == 0)
        vga_print("No files.\n");
    return 0;
}