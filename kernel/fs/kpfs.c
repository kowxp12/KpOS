#include "kpfs.h"
#include "block.h"
#include "vga.h"
#include "random.h"

#define KPFS_VERSION     2
#define KPFS_SECTOR      512
#define ET_DIVISOR       256    /* one entry-table sector per 256 disk sectors */
#define ET_MIN           1
#define ET_MAX           64
#define DIR_DIVISOR      128    /* one directory sector per 128 disk sectors */
#define DIR_MIN          4
#define DIR_MAX          256
#define ENTRY_SIZE       64
#define ENTRIES_PER_SEC  (KPFS_SECTOR / ENTRY_SIZE)
#define CHILDREN_MAX     (KPFS_SECTOR / 4)   /* 128 child IDs per directory */
#define NAME_MAX_LEN     15
#define ROOT_ID          0u

#define TYPE_FREE 0
#define TYPE_FILE 1
#define TYPE_DIR  2

/* Superblock, section 5 of the v2 spec. Exactly one sector. */
struct kpfs_super {
    char     magic[4];          /* "KPFS" */
    uint32_t version;
    uint32_t sector_size;
    uint64_t total_sectors;
    uint64_t total_bytes;
    uint64_t sb_start;
    uint64_t sb_size;
    uint64_t et_start;
    uint64_t et_size;
    uint64_t dir_start;
    uint64_t dir_size;
    uint64_t file_start;
    uint64_t fs_id;
    uint32_t next_id;           /* next ID to hand out */
    uint32_t dir_next;          /* directory sectors already in use */
    uint8_t  reserved[KPFS_SECTOR - 100];
} __attribute__((packed));
_Static_assert(sizeof(struct kpfs_super) == KPFS_SECTOR, "superblock must be one sector");

/* Entry table record: one per file or directory, 8 per sector */
struct kpfs_entry {
    char     name[16];
    uint32_t id;
    uint32_t parent;
    uint8_t  type;              /* TYPE_FREE, TYPE_FILE, or TYPE_DIR */
    uint64_t start;             /* directory: sector of its child list */
    uint64_t size;              /* file: size in bytes (stage 2) */
    uint64_t alloc;             /* file: sectors used (stage 2) */
    uint8_t  pad[ENTRY_SIZE - 49];
} __attribute__((packed));
_Static_assert(sizeof(struct kpfs_entry) == ENTRY_SIZE, "entry must be 64 bytes");

static uint8_t  sec[KPFS_SECTOR];       /* scratch sector */
static uint32_t kids[CHILDREN_MAX];     /* scratch child list */
static struct kpfs_super sb;            /* superblock in memory */
static uint32_t cur_dir = ROOT_ID;      /* current folder */

/* ---- Small helpers ---- */

static void copy_bytes(void *dst, const void *src, unsigned int n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (unsigned int i = 0; i < n; i++)
        d[i] = s[i];
}

static void zero_bytes(void *p, unsigned int n)
{
    uint8_t *d = p;
    for (unsigned int i = 0; i < n; i++)
        d[i] = 0;
}

static unsigned int str_len(const char *s)
{
    unsigned int n = 0;
    while (s[n])
        n++;
    return n;
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
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

static uint64_t clamp64(uint64_t v, uint64_t lo, uint64_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/* Names: 1-15 characters, no spaces or '/', and not "." or ".." */
static int valid_name(const char *name)
{
    unsigned int n = str_len(name);

    if (n == 0 || n > NAME_MAX_LEN)
        return 0;
    if (streq(name, ".") || streq(name, ".."))
        return 0;
    for (unsigned int i = 0; i < n; i++)
        if (name[i] == '/' || name[i] == ' ')
            return 0;
    return 1;
}

/* ---- Superblock ---- */

/* 0 = ok, -1 = not KPFS or unreadable, -2 = KPFS v1 disk */
static int load_super(void)
{
    if (block_init() != 0)
        return -1;
    if (block_read(0, sec) != 0)
        return -1;

    copy_bytes(&sb, sec, sizeof(sb));

    if (sb.magic[0] != 'K' || sb.magic[1] != 'P' ||
        sb.magic[2] != 'F' || sb.magic[3] != 'S')
        return -1;
    if (sb.version == 1)
        return -2;
    if (sb.version != KPFS_VERSION)
        return -1;
    if (sb.total_sectors != block_sectors())
        return -1;
    return 0;
}

static int write_super(void)
{
    zero_bytes(sec, KPFS_SECTOR);
    copy_bytes(sec, &sb, sizeof(sb));
    return block_write(0, sec);
}

/* Load the superblock for a command, printing a hint if the disk isn't usable */
static int need_fs(void)
{
    int r = load_super();
    if (r == 0)
        return 0;
    if (r == -2)
        vga_wprint("This disk uses KPFS v1. Type: format yes\n");
    else
        vga_wprint("No formatted KPFS disk. Type: format yes\n");
    return -1;
}

/* ---- Entry table ---- */

static uint32_t et_count(void)
{
    return (uint32_t)(sb.et_size * ENTRIES_PER_SEC);
}

static int entry_get(uint32_t i, struct kpfs_entry *e)
{
    if (block_read(sb.et_start + i / ENTRIES_PER_SEC, sec) != 0)
        return -1;
    copy_bytes(e, sec + (i % ENTRIES_PER_SEC) * ENTRY_SIZE, ENTRY_SIZE);
    return 0;
}

static int entry_put(uint32_t i, const struct kpfs_entry *e)
{
    uint64_t s = sb.et_start + i / ENTRIES_PER_SEC;
    if (block_read(s, sec) != 0)
        return -1;
    copy_bytes(sec + (i % ENTRIES_PER_SEC) * ENTRY_SIZE, e, ENTRY_SIZE);
    return block_write(s, sec);
}

/* Find a used entry by its ID */
static int find_by_id(uint32_t id, struct kpfs_entry *out)
{
    uint32_t n = et_count();
    for (uint32_t i = 0; i < n; i++) {
        if (entry_get(i, out) != 0)
            return -1;
        if (out->type != TYPE_FREE && out->id == id)
            return 0;
    }
    return -1;
}

/* Find the first unused entry slot */
static int find_free_slot(uint32_t *out)
{
    uint32_t n = et_count();
    for (uint32_t i = 0; i < n; i++) {
        struct kpfs_entry e;
        if (entry_get(i, &e) != 0)
            return -1;
        if (e.type == TYPE_FREE) {
            *out = i;
            return 0;
        }
    }
    return -1;
}

/* ---- Directory child lists ---- */

static int read_children(const struct kpfs_entry *dir)
{
    return block_read(dir->start, kids);
}

static int write_children(const struct kpfs_entry *dir)
{
    return block_write(dir->start, kids);
}

/* Print a folder path from the root down, like /docs/notes */
static void print_path(uint32_t id)
{
    if (id == ROOT_ID)
        return;

    struct kpfs_entry e;
    if (find_by_id(id, &e) != 0)
        return;

    print_path(e.parent);
    vga_putchar('/');
    vga_print(e.name);
}

/* ---- Format ---- */

int kpfs_format(void)
{
    if (block_init() != 0) {
        vga_eprint("No disk found. Run 'disks' to check.\n");
        return -1;
    }

    uint64_t total = block_sectors();
    if (total < 64) {
        vga_eprint("Disk is too small for KPFS.\n");
        return -1;
    }

    uint64_t et = clamp64(total / ET_DIVISOR, ET_MIN, ET_MAX);
    uint64_t dr = clamp64(total / DIR_DIVISOR, DIR_MIN, DIR_MAX);

    uint64_t et_start   = 1;
    uint64_t dir_start  = et_start + et;
    uint64_t file_start = dir_start + dr;

    if (file_start + 16 > total) {
        vga_eprint("Disk is too small for the layout.\n");
        return -1;
    }

    /* Build the new superblock in memory first */
    struct kpfs_super nsb;
    zero_bytes(&nsb, sizeof(nsb));
    nsb.magic[0] = 'K';
    nsb.magic[1] = 'P';
    nsb.magic[2] = 'F';
    nsb.magic[3] = 'S';
    nsb.version = KPFS_VERSION;
    nsb.sector_size = KPFS_SECTOR;
    nsb.total_sectors = total;
    nsb.total_bytes = total * KPFS_SECTOR;
    nsb.sb_start = 0;
    nsb.sb_size = 1;
    nsb.et_start = et_start;
    nsb.et_size = et;
    nsb.dir_start = dir_start;
    nsb.dir_size = dr;
    nsb.file_start = file_start;

    uint32_t hi = random_next();
    uint32_t lo = random_next();
    nsb.fs_id = ((uint64_t)hi << 32) | lo;

    nsb.next_id = 1;    /* ID 0 belongs to the root */
    nsb.dir_next = 1;   /* directory sector 0 belongs to the root */

    /* Step 1: invalidate the old superblock so a half-done format can't look valid */
    zero_bytes(sec, KPFS_SECTOR);
    if (block_write(0, sec) != 0) {
        vga_eprint("Failed to invalidate the old superblock.\n");
        return -1;
    }

    /* Step 2: erase every other sector, so old file contents are gone */
    vga_print("Erasing disk");
    for (uint64_t s = 1; s < total; s++) {
        if (block_write(s, sec) != 0) {
            vga_eprint("Erase failed.\n");
            return -1;
        }
        if (s % 256 == 0)
            vga_putchar('.');
    }
    vga_putchar('\n');

    /* Step 3: write the root directory's entry */
    sb = nsb;
    struct kpfs_entry root;
    zero_bytes(&root, sizeof(root));
    root.id = ROOT_ID;
    root.parent = ROOT_ID;
    root.type = TYPE_DIR;
    root.start = dir_start;

    if (entry_put(0, &root) != 0) {
        vga_eprint("Failed to write the root directory.\n");
        return -1;
    }

    /* Step 4: superblock last. Until this succeeds, the disk is not formatted. */
    if (write_super() != 0) {
        vga_eprint("Failed to write the superblock.\n");
        return -1;
    }

    cur_dir = ROOT_ID;

    vga_print("Formatted as KPFS v2.\n");
    vga_print("  Total sectors:  ");
    vga_print_u64(total);
    vga_print(" (");
    vga_print_u64(total >> 11);
    vga_print(" MB)\n");
    vga_print("  Superblock:     sector 0\n");
    vga_print("  Entry table:    sectors 1-");
    vga_print_u64(et);
    vga_print("\n");
    vga_print("  Folder data:    ");
    vga_print_u64(dr);
    vga_print(" folders max, from sector ");
    vga_print_u64(dir_start);
    vga_print("\n");
    vga_print("  File data:      from sector ");
    vga_print_u64(file_start);
    vga_print("\n");
    return 0;
}

int kpfs_probe(void)
{
    return load_super() == 0;
}

/* ---- Folder commands ---- */

int kpfs_list(void)
{
    if (need_fs() != 0)
        return -1;

    struct kpfs_entry dir;
    if (find_by_id(cur_dir, &dir) != 0) {
        vga_eprint("Current folder is missing.\n");
        return -1;
    }
    if (read_children(&dir) != 0) {
        vga_eprint("Read failed.\n");
        return -1;
    }

    unsigned int count = 0;
    for (unsigned int n = 0; n < CHILDREN_MAX && kids[n]; n++) {
        struct kpfs_entry e;
        if (find_by_id(kids[n], &e) != 0)
            continue;

        vga_print("  ");
        vga_print(e.name);
        if (e.type == TYPE_DIR)
            vga_putchar('/');
        vga_putchar('\n');
        count++;
    }

    if (count == 0)
        vga_print("No files or folders.\n");
    return 0;
}

int kpfs_mkdir(const char *name)
{
    if (need_fs() != 0)
        return -1;

    if (!valid_name(name)) {
        vga_wprint("Invalid name: 1-15 characters, no spaces or '/'.\n");
        return -1;
    }

    struct kpfs_entry parent;
    if (find_by_id(cur_dir, &parent) != 0 || parent.type != TYPE_DIR) {
        vga_eprint("Current folder is missing.\n");
        return -1;
    }
    if (read_children(&parent) != 0) {
        vga_eprint("Read failed.\n");
        return -1;
    }

    /* Check for a free child slot and for a duplicate name */
    unsigned int n = 0;
    while (n < CHILDREN_MAX && kids[n]) {
        struct kpfs_entry e;
        if (find_by_id(kids[n], &e) == 0 && name_eq(e.name, name)) {
            vga_wprint("A file or folder with that name already exists here.\n");
            return -1;
        }
        n++;
    }
    if (n == CHILDREN_MAX) {
        vga_wprint("This folder is full.\n");
        return -1;
    }

    if (sb.dir_next >= sb.dir_size) {
        vga_wprint("No room for more folders on this disk.\n");
        return -1;
    }

    uint32_t slot;
    if (find_free_slot(&slot) != 0) {
        vga_wprint("The entry table is full.\n");
        return -1;
    }

    uint32_t new_id = sb.next_id;
    uint64_t dir_sector = sb.dir_start + sb.dir_next;

    /* Step 1: empty child list for the new folder (the scratch list is reloaded below) */
    zero_bytes(kids, sizeof(kids));
    if (block_write(dir_sector, kids) != 0) {
        vga_eprint("Write failed. Nothing was changed.\n");
        return -1;
    }

    /* Step 2: entry for the new folder */
    struct kpfs_entry ne;
    zero_bytes(&ne, sizeof(ne));
    for (unsigned int i = 0; i < NAME_MAX_LEN && name[i]; i++)
        ne.name[i] = name[i];
    ne.id = new_id;
    ne.parent = cur_dir;
    ne.type = TYPE_DIR;
    ne.start = dir_sector;

    if (entry_put(slot, &ne) != 0) {
        vga_eprint("Write failed. Nothing was changed.\n");
        return -1;
    }

    /* Step 3: advance the superblock counters */
    sb.next_id++;
    sb.dir_next++;
    if (write_super() != 0) {
        vga_eprint("Write failed. Nothing was changed.\n");
        return -1;
    }

    /* Step 4: commit by adding the new ID to the parent's child list */
    if (read_children(&parent) != 0) {
        vga_eprint("Read failed.\n");
        return -1;
    }
    kids[n] = new_id;
    if (write_children(&parent) != 0) {
        vga_eprint("Could not update the parent folder.\n");
        return -1;
    }

    vga_print("Created folder ");
    vga_print(name);
    vga_print("/\n");
    return 0;
}

int kpfs_cd(const char *name)
{
    if (need_fs() != 0)
        return -1;

    if (streq(name, "/")) {
        cur_dir = ROOT_ID;
        return 0;
    }
    if (streq(name, ".")) {
        return 0;
    }

    struct kpfs_entry here;
    if (find_by_id(cur_dir, &here) != 0) {
        vga_eprint("Current folder is missing.\n");
        return -1;
    }

    if (streq(name, "..")) {
        cur_dir = here.parent;   /* the root's parent is the root itself */
        return 0;
    }

    if (read_children(&here) != 0) {
        vga_eprint("Read failed.\n");
        return -1;
    }

    for (unsigned int n = 0; n < CHILDREN_MAX && kids[n]; n++) {
        struct kpfs_entry e;
        if (find_by_id(kids[n], &e) == 0 &&
            e.type == TYPE_DIR && name_eq(e.name, name)) {
            cur_dir = e.id;
            return 0;
        }
    }

    vga_wprint("No folder named ");
    vga_wprint(name);
    vga_putchar('\n');
    return -1;
}

int kpfs_pwd(void)
{
    if (need_fs() != 0)
        return -1;

    if (cur_dir == ROOT_ID) {
        vga_print("/\n");
    } else {
        print_path(cur_dir);
        vga_putchar('\n');
    }
    return 0;
}

/* ---- File commands: stage 2 ---- */

int kpfs_write(const char *name, const char *data, unsigned int len)
{
    (void)name;
    (void)data;
    (void)len;
    vga_wprint("Saving files comes in KPFS v2 stage 2.\n");
    return -1;
}

int kpfs_read(const char *name)
{
    (void)name;
    vga_wprint("Reading files comes in KPFS v2 stage 2.\n");
    return -1;
}

int kpfs_remove(const char *name)
{
    (void)name;
    vga_wprint("Deleting files comes in KPFS v2 stage 2.\n");
    return -1;
}