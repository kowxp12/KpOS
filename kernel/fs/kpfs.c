#include "kpfs.h"
#include "block.h"
#include "vga.h"
#include "random.h"

#define KPFS_VERSION     2
#define KPFS_SECTOR      512
#define ET_DIVISOR       256    /* one entry-table sector per 256 disk sectors */
#define ET_MIN           1
#define ET_MAX           64
#define DIR_DIVISOR      128    /* one folder sector per 128 disk sectors */
#define DIR_MIN          4
#define DIR_MAX          256
#define ENTRY_SIZE       64
#define ENTRIES_PER_SEC  (KPFS_SECTOR / ENTRY_SIZE)
#define CHILDREN_MAX     (KPFS_SECTOR / 4)   /* 128 child IDs per folder */
#define NAME_MAX_LEN     15
#define ROOT_ID          0u

#define TYPE_FREE 0
#define TYPE_FILE 1
#define TYPE_DIR  2

/* Superblock, section 5 of the v2 spec. Exactly one sector. */
struct kpfs_super {
    char     magic[4];
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
    uint32_t next_id;
    uint32_t dir_next;
    uint8_t  reserved[KPFS_SECTOR - 100];
} __attribute__((packed));
_Static_assert(sizeof(struct kpfs_super) == KPFS_SECTOR, "superblock must be one sector");

/* Entry table record: one per file or folder, 8 per sector */
struct kpfs_entry {
    char     name[16];
    uint32_t id;
    uint32_t parent;
    uint8_t  type;
    uint64_t start;     /* folder: sector of its child list; file: first data sector */
    uint64_t size;      /* file: size in bytes */
    uint64_t alloc;     /* file: sectors used */
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

static void set_name(struct kpfs_entry *e, const char *name)
{
    zero_bytes(e->name, sizeof(e->name));
    for (unsigned int i = 0; i < NAME_MAX_LEN && name[i]; i++)
        e->name[i] = name[i];
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

/* Find a used entry by ID. idx may be NULL. */
static int find_at(uint32_t id, struct kpfs_entry *out, uint32_t *idx)
{
    uint32_t n = et_count();
    for (uint32_t i = 0; i < n; i++) {
        if (entry_get(i, out) != 0)
            return -1;
        if (out->type != TYPE_FREE && out->id == id) {
            if (idx)
                *idx = i;
            return 0;
        }
    }
    return -1;
}

static int find_by_id(uint32_t id, struct kpfs_entry *out)
{
    return find_at(id, out, 0);
}

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

/* ---- Folder child lists ---- */

/* Load a folder's entry and its child list into kids[] */
static int load_dir(uint32_t id, struct kpfs_entry *dir)
{
    if (find_by_id(id, dir) != 0 || dir->type != TYPE_DIR)
        return -1;
    return block_read(dir->start, kids) == 0 ? 0 : -1;
}

static unsigned int count_kids(void)
{
    unsigned int n = 0;
    while (n < CHILDREN_MAX && kids[n])
        n++;
    return n;
}

/* Find a direct child by name in a folder. kid_pos may be NULL. */
static int child_lookup(uint32_t dir_id, const char *name,
                        struct kpfs_entry *out, unsigned int *kid_pos)
{
    struct kpfs_entry dir;
    if (load_dir(dir_id, &dir) != 0)
        return -1;

    for (unsigned int n = 0; n < CHILDREN_MAX && kids[n]; n++) {
        struct kpfs_entry e;
        if (find_by_id(kids[n], &e) == 0 && name_eq(e.name, name)) {
            copy_bytes(out, &e, sizeof(e));
            if (kid_pos)
                *kid_pos = n;
            return 0;
        }
    }
    return -1;
}

/* Add an ID to a folder's child list (the commit step for new items) */
static int link_child(uint32_t dir_id, uint32_t id)
{
    struct kpfs_entry dir;
    if (load_dir(dir_id, &dir) != 0)
        return -1;

    unsigned int c = count_kids();
    if (c >= CHILDREN_MAX)
        return -1;

    kids[c] = id;
    return block_write(dir.start, kids);
}

/* Remove an ID from a folder's child list, closing the gap */
static int unlink_child(uint32_t dir_id, uint32_t id)
{
    struct kpfs_entry dir;
    if (load_dir(dir_id, &dir) != 0)
        return -1;

    unsigned int c = count_kids();
    for (unsigned int i = 0; i < c; i++) {
        if (kids[i] == id) {
            for (unsigned int j = i; j + 1 < c; j++)
                kids[j] = kids[j + 1];
            kids[c - 1] = 0;
            return block_write(dir.start, kids);
        }
    }
    return -1;
}

/* True if target is id itself or lies below it in the folder tree */
static int is_inside(uint32_t target, uint32_t id)
{
    uint32_t cur = target;

    for (uint32_t guard = 0; guard < et_count(); guard++) {
        if (cur == id)
            return 1;
        if (cur == ROOT_ID)
            return 0;

        struct kpfs_entry e;
        if (find_by_id(cur, &e) != 0)
            return 0;
        cur = e.parent;
    }
    return 0;
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

/* ---- File data space ---- */

/* First free run of 'need' sectors in the File Data region. Every file's
   sectors count as occupied, including a file that is being replaced. */
static int find_run(uint64_t need, uint64_t *start_out)
{
    uint64_t s = sb.file_start;
    uint32_t n = et_count();

    for (;;) {
        if (s + need > sb.total_sectors)
            return -1;

        int bumped = 0;
        for (uint32_t i = 0; i < n && !bumped; i++) {
            struct kpfs_entry e;
            if (entry_get(i, &e) != 0)
                return -1;
            if (e.type != TYPE_FILE || e.alloc == 0)
                continue;

            uint64_t end = e.start + e.alloc;
            if (s < end && e.start < s + need) {
                s = end;
                bumped = 1;
            }
        }
        if (!bumped) {
            *start_out = s;
            return 0;
        }
    }
}

/* Returns 1 if a live folder already uses this sector */
static int dir_sector_in_use(uint64_t sector)
{
    uint32_t n = et_count();
    for (uint32_t i = 0; i < n; i++) {
        struct kpfs_entry e;
        if (entry_get(i, &e) != 0)
            return 1;               /* be safe: treat unreadable as used */
        if (e.type == TYPE_DIR && e.start == sector)
            return 1;
    }
    return 0;
}

/* First folder-data sector that no live folder uses */
static int find_free_dir_sector(uint64_t *out)
{
    for (uint64_t k = 0; k < sb.dir_size; k++) {
        uint64_t s = sb.dir_start + k;
        if (!dir_sector_in_use(s)) {
            *out = s;
            return 0;
        }
    }
    return -1;
}

/* Free an entry and, for folders, everything inside it.
   The caller has already removed the item from its parent. */
static int delete_tree(uint32_t id)
{
    struct kpfs_entry e;
    uint32_t slot;
    if (find_at(id, &e, &slot) != 0)
        return -1;

    if (e.type == TYPE_DIR) {
        /* Copy the child list first, because recursion reuses kids[] */
        uint32_t list[CHILDREN_MAX];
        if (block_read(e.start, kids) != 0)
            return -1;
        copy_bytes(list, kids, sizeof(list));

        for (unsigned int n = 0; n < CHILDREN_MAX && list[n]; n++)
            if (delete_tree(list[n]) != 0)
                return -1;

        /* Clear the folder's child-list sector so the space is clean */
        zero_bytes(sec, KPFS_SECTOR);
        if (block_write(e.start, sec) != 0)
            return -1;
    }

    struct kpfs_entry blank;
    zero_bytes(&blank, sizeof(blank));
    return entry_put(slot, &blank);
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
    nsb.next_id = 1;
    nsb.dir_next = 1;

    /* Step 1: invalidate the old superblock */
    zero_bytes(sec, KPFS_SECTOR);
    if (block_write(0, sec) != 0) {
        vga_eprint("Failed to invalidate the old superblock.\n");
        return -1;
    }

    /* Step 2: erase every other sector */
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

    /* Step 3: root folder entry */
    copy_bytes(&sb, &nsb, sizeof(sb));
    struct kpfs_entry root;
    zero_bytes(&root, sizeof(root));
    root.id = ROOT_ID;
    root.parent = ROOT_ID;
    root.type = TYPE_DIR;
    root.start = dir_start;

    if (entry_put(0, &root) != 0) {
        vga_eprint("Failed to write the root folder.\n");
        return -1;
    }

    /* Step 4: superblock last */
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
    if (load_dir(cur_dir, &dir) != 0) {
        vga_eprint("Current folder is missing.\n");
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
        else {
            vga_print("  ");
            vga_print_u64(e.size);
            vga_print(" bytes");
        }
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

    struct kpfs_entry dup;
    if (child_lookup(cur_dir, name, &dup, 0) == 0) {
        vga_wprint("A file or folder with that name already exists here.\n");
        return -1;
    }

    struct kpfs_entry parent;
    if (load_dir(cur_dir, &parent) != 0) {
        vga_eprint("Current folder is missing.\n");
        return -1;
    }
    if (count_kids() >= CHILDREN_MAX) {
        vga_wprint("This folder is full.\n");
        return -1;
    }

    uint32_t slot;
    if (find_free_slot(&slot) != 0) {
        vga_wprint("The entry table is full.\n");
        return -1;
    }

    uint64_t dir_sector;
    if (find_free_dir_sector(&dir_sector) != 0) {
        vga_wprint("No room for more folders on this disk.\n");
        return -1;
    }

    uint32_t new_id = sb.next_id;

    /* Step 1: empty child list for the new folder */
    zero_bytes(kids, sizeof(kids));
    if (block_write(dir_sector, kids) != 0) {
        vga_eprint("Write failed. Nothing was changed.\n");
        return -1;
    }

    /* Step 2: the new folder's entry */
    struct kpfs_entry ne;
    zero_bytes(&ne, sizeof(ne));
    set_name(&ne, name);
    ne.id = new_id;
    ne.parent = cur_dir;
    ne.type = TYPE_DIR;
    ne.start = dir_sector;

    if (entry_put(slot, &ne) != 0) {
        vga_eprint("Write failed. Nothing was changed.\n");
        return -1;
    }

    /* Step 3: advance the ID counter */
    sb.next_id++;
    if (write_super() != 0) {
        vga_eprint("Write failed. Nothing was changed.\n");
        return -1;
    }

    /* Step 4: commit by adding the ID to the parent's list */
    if (link_child(cur_dir, new_id) != 0) {
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
    if (streq(name, "."))
        return 0;

    struct kpfs_entry here;
    if (find_by_id(cur_dir, &here) != 0) {
        vga_eprint("Current folder is missing.\n");
        return -1;
    }
    if (streq(name, "..")) {
        cur_dir = here.parent;
        return 0;
    }

    struct kpfs_entry d;
    if (child_lookup(cur_dir, name, &d, 0) == 0 && d.type == TYPE_DIR) {
        cur_dir = d.id;
        return 0;
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

/* ---- File commands ---- */

/* Create or replace a file in the current folder */
int kpfs_write(const char *name, const char *data, unsigned int len)
{
    if (need_fs() != 0)
        return -1;

    if (!valid_name(name)) {
        vga_wprint("Invalid name: 1-15 characters, no spaces or '/'.\n");
        return -1;
    }

    struct kpfs_entry ne;
    int exists = (child_lookup(cur_dir, name, &ne, 0) == 0);
    if (exists && ne.type == TYPE_DIR) {
        vga_wprint("A folder with that name already exists here.\n");
        return -1;
    }

    uint32_t slot = 0;
    if (exists) {
        /* Keep the same ID, name, and parent. Only the storage fields change. */
        if (find_at(ne.id, &ne, &slot) != 0) {
            vga_eprint("Read error.\n");
            return -1;
        }
    } else {
        struct kpfs_entry dir;
        if (load_dir(cur_dir, &dir) != 0) {
            vga_eprint("Current folder is missing.\n");
            return -1;
        }
        if (count_kids() >= CHILDREN_MAX) {
            vga_wprint("This folder is full.\n");
            return -1;
        }
        if (find_free_slot(&slot) != 0) {
            vga_wprint("The entry table is full.\n");
            return -1;
        }
        zero_bytes(&ne, sizeof(ne));
        set_name(&ne, name);
        ne.parent = cur_dir;
        ne.type = TYPE_FILE;
    }

    uint64_t need = ((uint64_t)len + KPFS_SECTOR - 1) / KPFS_SECTOR;
    uint64_t start = sb.file_start;

    if (need > 0 && find_run(need, &start) != 0) {
        vga_wprint("Not enough free space.\n");
        return -1;
    }

    /* Step 1: write the data to free sectors. The old file is untouched. */
    for (uint64_t k = 0; k < need; k++) {
        uint64_t off = k * KPFS_SECTOR;
        uint64_t left = (uint64_t)len - off;
        unsigned int n = left > KPFS_SECTOR ? KPFS_SECTOR : (unsigned int)left;

        zero_bytes(sec, KPFS_SECTOR);
        copy_bytes(sec, data + off, n);
        if (block_write(start + k, sec) != 0) {
            vga_eprint("Write failed. The old file is unchanged.\n");
            return -1;
        }
    }

    /* Step 2: a new file gets the next ID */
    if (!exists) {
        ne.id = sb.next_id;
        sb.next_id++;
        if (write_super() != 0) {
            vga_eprint("Write failed. Nothing was changed.\n");
            return -1;
        }
    }

    /* Step 3: commit the entry in one sector write */
    ne.start = start;
    ne.size = len;
    ne.alloc = need;

    if (entry_put(slot, &ne) != 0) {
        vga_eprint("Could not update the entry table. The old file is unchanged.\n");
        return -1;
    }

    /* Step 4: a new file joins its folder last, so a failure leaves nothing visible */
    if (!exists && link_child(cur_dir, ne.id) != 0) {
        vga_eprint("Could not add the file to its folder.\n");
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

    struct kpfs_entry e;
    if (child_lookup(cur_dir, name, &e, 0) != 0) {
        vga_wprint("No file named ");
        vga_wprint(name);
        vga_putchar('\n');
        return -1;
    }
    if (e.type != TYPE_FILE) {
        vga_wprint(name);
        vga_wprint(" is a folder. Use cd to open it.\n");
        return -1;
    }

    /* Read only the file's real size, not its whole sectors */
    uint64_t remaining = e.size;
    for (uint64_t k = 0; k < e.alloc && remaining > 0; k++) {
        if (block_read(e.start + k, sec) != 0) {
            vga_eprint("Read failed.\n");
            return -1;
        }

        unsigned int n = remaining > KPFS_SECTOR ? KPFS_SECTOR : (unsigned int)remaining;
        for (unsigned int i = 0; i < n; i++) {
            char c = (char)sec[i];
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

/* Delete a file. Folder deletion comes in stage 3. */
int kpfs_remove(const char *name, int force)
{
    if (need_fs() != 0)
        return -1;

    struct kpfs_entry e;
    if (child_lookup(cur_dir, name, &e, 0) != 0) {
        vga_wprint("No file or folder named ");
        vga_wprint(name);
        vga_putchar('\n');
        return -1;
    }

    if (e.type == TYPE_DIR) {
        /* Never delete a folder we're inside, or any folder above us */
        if (is_inside(cur_dir, e.id)) {
            vga_eprint("You are inside that folder. Use cd to leave it first.\n");
            return -1;
        }

        struct kpfs_entry d;
        if (load_dir(e.id, &d) != 0) {
            vga_eprint("Read error.\n");
            return -1;
        }

        unsigned int count = count_kids();
        if (count > 0 && !force) {
            vga_wprint("Folder ");
            vga_wprint(name);
            vga_wprint(" has ");
            vga_print_u64(count);
            vga_wprint(" items. Use: rm -y ");
            vga_wprint(name);
            vga_putchar('\n');
            return -1;
        }
    }

    /* Step 1: unlink from the current folder. This is the commit point. */
    if (unlink_child(cur_dir, e.id) != 0) {
        vga_eprint("Could not update the folder. Nothing was changed.\n");
        return -1;
    }

    /* Step 2: free the item, and everything inside it for folders */
    if (delete_tree(e.id) != 0) {
        vga_eprint("Delete partly finished. Run ls to check.\n");
        return -1;
    }

    vga_print(e.type == TYPE_DIR ? "Deleted folder " : "Deleted ");
    vga_print(name);
    vga_putchar('\n');
    return 0;
}

/* Resolve a destination folder: "/", ".", "..", or a child folder name */
static int resolve_dest(const char *name, uint32_t *out)
{
    if (streq(name, "/")) {
        *out = ROOT_ID;
        return 0;
    }
    if (streq(name, ".")) {
        *out = cur_dir;
        return 0;
    }

    struct kpfs_entry here;
    if (find_by_id(cur_dir, &here) != 0)
        return -1;
    if (streq(name, "..")) {
        *out = here.parent;
        return 0;
    }

    struct kpfs_entry d;
    if (child_lookup(cur_dir, name, &d, 0) == 0 && d.type == TYPE_DIR) {
        *out = d.id;
        return 0;
    }

    vga_wprint("No folder named ");
    vga_wprint(name);
    vga_putchar('\n');
    return -1;
}

/* Move a file or folder into another folder. The ID never changes. */
int kpfs_move(const char *name, const char *dest)
{
    if (need_fs() != 0)
        return -1;

    struct kpfs_entry e;
    if (child_lookup(cur_dir, name, &e, 0) != 0) {
        vga_wprint("No file or folder named ");
        vga_wprint(name);
        vga_putchar('\n');
        return -1;
    }

    uint32_t dest_id;
    if (resolve_dest(dest, &dest_id) != 0)
        return -1;

    if (dest_id == cur_dir) {
        vga_wprint("It is already in that folder.\n");
        return -1;
    }
    if (e.type == TYPE_DIR && is_inside(dest_id, e.id)) {
        vga_eprint("A folder can't be moved into itself or one of its subfolders.\n");
        return -1;
    }

    struct kpfs_entry clash;
    if (child_lookup(dest_id, name, &clash, 0) == 0) {
        vga_wprint("That folder already has an item with that name.\n");
        return -1;
    }

    struct kpfs_entry dd;
    if (load_dir(dest_id, &dd) != 0) {
        vga_eprint("Destination folder is missing.\n");
        return -1;
    }
    if (count_kids() >= CHILDREN_MAX) {
        vga_wprint("The destination folder is full.\n");
        return -1;
    }

    uint32_t slot;
    struct kpfs_entry live;
    if (find_at(e.id, &live, &slot) != 0) {
        vga_eprint("Read error.\n");
        return -1;
    }

    /* Step 1: add to the destination. If this fails, nothing has changed. */
    if (link_child(dest_id, e.id) != 0) {
        vga_eprint("Move failed. Nothing was changed.\n");
        return -1;
    }

    /* Step 2: remove from the source. If this fails, undo step 1. */
    if (unlink_child(cur_dir, e.id) != 0) {
        unlink_child(dest_id, e.id);
        vga_eprint("Move failed. Nothing was changed.\n");
        return -1;
    }

    /* Step 3: record the new parent */
    live.parent = dest_id;
    if (entry_put(slot, &live) != 0) {
        vga_eprint("Move partly finished. Check the folders with ls.\n");
        return -1;
    }

    vga_print("Moved ");
    vga_print(name);
    vga_putchar('\n');
    return 0;
}

/* Rename a file or folder in the current folder. The ID never changes. */
int kpfs_rename(const char *old_name, const char *new_name)
{
    if (need_fs() != 0)
        return -1;

    if (!valid_name(new_name)) {
        vga_wprint("Invalid name: 1-15 characters, no spaces or '/'.\n");
        return -1;
    }

    struct kpfs_entry e;
    if (child_lookup(cur_dir, old_name, &e, 0) != 0) {
        vga_wprint("No file or folder named ");
        vga_wprint(old_name);
        vga_putchar('\n');
        return -1;
    }

    struct kpfs_entry clash;
    if (child_lookup(cur_dir, new_name, &clash, 0) == 0) {
        vga_wprint("A file or folder with that name already exists here.\n");
        return -1;
    }

    uint32_t slot;
    struct kpfs_entry live;
    if (find_at(e.id, &live, &slot) != 0) {
        vga_eprint("Read error.\n");
        return -1;
    }

    set_name(&live, new_name);
    if (entry_put(slot, &live) != 0) {
        vga_eprint("Could not rename. Nothing was changed.\n");
        return -1;
    }

    vga_print("Renamed ");
    vga_print(old_name);
    vga_print(" to ");
    vga_print(new_name);
    vga_putchar('\n');
    return 0;
}