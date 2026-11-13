#include "block.h"
#include "ata.h"

/* The ATA driver currently uses 28-bit addressing, so it can reach this many sectors */
#define ATA_LBA28_LIMIT 0x0FFFFFFFull

static uint64_t count = 0;

int block_init(void)
{
    if (!ata_present()) {
        count = 0;
        return -1;
    }

    count = ata_capacity_sectors();
    if (count > ATA_LBA28_LIMIT)
        count = ATA_LBA28_LIMIT;
    return 0;
}

uint64_t block_sectors(void)
{
    return count;
}

int block_read(uint64_t lba, void *buf)
{
    if (lba >= count)
        return -1;
    return ata_read((unsigned int)lba, buf);
}

int block_write(uint64_t lba, const void *buf)
{
    if (lba >= count)
        return -1;
    return ata_write((unsigned int)lba, buf);
}