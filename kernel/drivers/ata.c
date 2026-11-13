#include "ata.h"
#include "io.h"

/* Primary ATA bus I/O ports */
#define ATA_DATA      0x1F0
#define ATA_SECCOUNT  0x1F2
#define ATA_LBA_LO    0x1F3
#define ATA_LBA_MID   0x1F4
#define ATA_LBA_HI    0x1F5
#define ATA_DRIVE     0x1F6
#define ATA_STATUS    0x1F7   /* read: status bits */
#define ATA_CMD       0x1F7   /* write: command */

/* Status register bits */
#define ST_ERR  0x01
#define ST_DF   0x20
#define ST_DRQ  0x08
#define ST_BSY  0x80

/* Commands */
#define CMD_READ      0x20
#define CMD_WRITE     0x30
#define CMD_FLUSH     0xE7
#define CMD_IDENTIFY  0xEC

#define TIMEOUT 1000000

/* Reading the status port four times gives the drive about 400 ns to react */
static void ata_delay(void)
{
    for (int i = 0; i < 4; i++)
        inb(ATA_STATUS);
}

/* Wait until the drive is not busy */
static int wait_not_busy(void)
{
    for (unsigned int t = 0; t < TIMEOUT; t++)
        if (!(inb(ATA_STATUS) & ST_BSY))
            return 0;
    return -1;
}

/* Wait until the drive has data ready to transfer */
static int wait_drq(void)
{
    for (unsigned int t = 0; t < TIMEOUT; t++) {
        unsigned char s = inb(ATA_STATUS);
        if (s & (ST_ERR | ST_DF))
            return -1;
        if (!(s & ST_BSY) && (s & ST_DRQ))
            return 0;
    }
    return -1;
}

/* Tell the drive which sector we want (LBA28 addressing, one sector) */
static void select_lba(unsigned int lba)
{
    outb(ATA_DRIVE,    0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO,   lba & 0xFF);
    outb(ATA_LBA_MID,  (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI,   (lba >> 16) & 0xFF);
}

static unsigned short identify_data[256];
static unsigned long long capacity = 0;

unsigned long long ata_capacity_sectors(void)
{
    return capacity;
}

int ata_present(void)
{
    outb(ATA_DRIVE,    0xA0);
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO,   0);
    outb(ATA_LBA_MID,  0);
    outb(ATA_LBA_HI,   0);
    outb(ATA_CMD, CMD_IDENTIFY);

    if (inb(ATA_STATUS) == 0)
        return 0;
    if (wait_not_busy())
        return 0;
    if (inb(ATA_LBA_MID) || inb(ATA_LBA_HI))
        return 0;
    if (wait_drq())
        return 0;

    for (int i = 0; i < 256; i++)
        identify_data[i] = inw(ATA_DATA);

    /* Words 60-61: sector count with 28-bit addressing */
    capacity = identify_data[60] | ((unsigned long long)identify_data[61] << 16);

    /* Bit 10 of word 83: 48-bit addressing supported. Words 100-103 hold the count. */
    if (identify_data[83] & (1 << 10)) {
        capacity = (unsigned long long)identify_data[100]
                 | ((unsigned long long)identify_data[101] << 16)
                 | ((unsigned long long)identify_data[102] << 32)
                 | ((unsigned long long)identify_data[103] << 48);
    }
    return 1;
}

int ata_read(unsigned int lba, void *buf)
{
    unsigned short *out = buf;

    if (wait_not_busy())
        return -1;

    select_lba(lba);
    outb(ATA_CMD, CMD_READ);
    ata_delay();

    if (wait_drq())
        return -1;

    for (int i = 0; i < 256; i++)
        out[i] = inw(ATA_DATA);

    return 0;
}

int ata_write(unsigned int lba, const void *buf)
{
    const unsigned short *in = buf;

    if (wait_not_busy())
        return -1;

    select_lba(lba);
    outb(ATA_CMD, CMD_WRITE);
    ata_delay();

    if (wait_drq())
        return -1;

    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, in[i]);

    /* Ask the drive to save its cache to the disk */
    outb(ATA_CMD, CMD_FLUSH);
    return wait_not_busy();
}