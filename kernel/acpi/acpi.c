#include "acpi.h"
#include "vga.h"
#include "io.h"

/* The RSDP: found by searching memory for "RSD PTR " */
struct rsdp {
    char          signature[8];
    unsigned char checksum;
    char          oem_id[6];
    unsigned char revision;
    unsigned int  rsdt_addr;
    unsigned int  length;
    unsigned long long xsdt_addr;   /* only valid when revision >= 2 */
    unsigned char ext_checksum;
    unsigned char reserved[3];
} __attribute__((packed));

/* The header every ACPI table starts with */
struct sdt_header {
    char          signature[4];
    unsigned int  length;
    unsigned char revision;
    unsigned char checksum;
    char          oem_id[6];
    char          oem_table_id[8];
    unsigned int  oem_revision;
    unsigned int  creator_id;
    unsigned int  creator_revision;
} __attribute__((packed));

static const struct sdt_header *fadt;

/* Every ACPI table's bytes must add up to 0 (mod 256) */
static int checksum_ok(const void *p, unsigned int len)
{
    const unsigned char *b = p;
    unsigned char sum = 0;

    for (unsigned int i = 0; i < len; i++)
        sum += b[i];
    return sum == 0;
}

static int sig_eq(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void print_hex(unsigned int value)
{
    const char *digits = "0123456789ABCDEF";
    vga_print("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        vga_putchar(digits[(value >> shift) & 0xF]);
}

/* Search the BIOS area in 16-byte steps for the RSDP signature */
static const struct rsdp *find_rsdp(void)
{
    for (unsigned int addr = 0xE0000; addr < 0x100000; addr += 16) {
        const struct rsdp *r = (const struct rsdp *)addr;

        if (sig_eq(r->signature, "RSD PTR ", 8) && checksum_ok(r, 20))
            return r;
    }
    return 0;
}

/* Walk the RSDT (or XSDT) and return the table with the given signature */
static const struct sdt_header *find_table(const struct rsdp *r, const char *sig)
{
    int use_xsdt = (r->revision >= 2 && r->xsdt_addr != 0 &&
                    (r->xsdt_addr >> 32) == 0);

    unsigned int root_addr = use_xsdt ? (unsigned int)r->xsdt_addr : r->rsdt_addr;
    const struct sdt_header *root = (const struct sdt_header *)root_addr;

    if (!checksum_ok(root, root->length))
        return 0;

    /* RSDT entries are 4 bytes each, XSDT entries are 8 bytes each */
    unsigned int stride = use_xsdt ? 8 : 4;
    unsigned int count = (root->length - sizeof(*root)) / stride;
    const unsigned char *entries = (const unsigned char *)root + sizeof(*root);

    for (unsigned int i = 0; i < count; i++) {
        /* Read the low 32 bits of each entry (the address fits in them) */
        unsigned int addr = *(const unsigned int *)(entries + i * stride);
        const struct sdt_header *h = (const struct sdt_header *)addr;

        if (sig_eq(h->signature, sig, 4) && checksum_ok(h, h->length))
            return h;
    }
    return 0;
}

static unsigned short pm1a_cnt;
static unsigned short slp_typa;

/* Search the DSDT bytes for "_S5_" and read its sleep-type number */
static int find_s5(const struct sdt_header *dsdt)
{
    const unsigned char *b = (const unsigned char *)dsdt;

    for (unsigned int i = sizeof(*dsdt) + 1; i + 8 < dsdt->length; i++) {
        /* "_S5_" must follow a NameOp (0x08) and be followed by a PackageOp (0x12) */
        if (b[i-1] == 0x08 && sig_eq((const char *)&b[i], "_S5_", 4) &&
            b[i+4] == 0x12) {

            /* b[i+5] = package length, b[i+6] = element count, b[i+7] = first element */
            unsigned char first = b[i+7];

            if (first == 0x0A)          /* BytePrefix: the value is the next byte */
                slp_typa = b[i+8];
            else                        /* ZeroOp (0x00) or OneOp (0x01) */
                slp_typa = first;

            return 1;
        }
    }
    return 0;
}

int acpi_init(void)
{
    const struct rsdp *r = find_rsdp();

    if (!r) {
        vga_print("ACPI: RSDP not found\n");
        return -1;
    }

    fadt = find_table(r, "FACP");
    if (!fadt) {
        vga_print("ACPI: FADT not found\n");
        return -1;
    }

    /* Offsets inside the FADT: DSDT address at 40, PM1a control port at 64 */
    const unsigned char *f = (const unsigned char *)fadt;
    unsigned int dsdt_addr = *(const unsigned int *)(f + 40);
    pm1a_cnt = (unsigned short)*(const unsigned int *)(f + 64);

    const struct sdt_header *dsdt = (const struct sdt_header *)dsdt_addr;
    if (!checksum_ok(dsdt, dsdt->length)) {
        vga_print("ACPI: DSDT checksum failed\n");
        return -1;
    }

    if (!find_s5(dsdt)) {
        vga_print("ACPI: _S5 not found\n");
        return -1;
    }

    vga_print("ACPI: PM1a port = ");
    print_hex(pm1a_cnt);
    vga_putchar('\n');

    vga_print("ACPI: S5 sleep type = ");
    print_hex(slp_typa);
    vga_putchar('\n');

    return 0;
}

void acpi_poweroff(void)
{
    if (pm1a_cnt == 0) {
        vga_print("ACPI: no power-off port available\n");
        return;
    }

    unsigned short value = (unsigned short)((slp_typa << 10) | (1 << 13));
    outw(pm1a_cnt, value);

    /* If we're still running, the write didn't power the machine off */
    vga_print("ACPI: power-off request sent, but machine is still running\n");
}

