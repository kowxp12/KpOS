#ifndef ATA_H
#define ATA_H

/* Returns 1 if a drive is attached to the primary bus, 0 otherwise */
int ata_present(void);

/* Read one 512-byte sector into buf. Returns 0 on success, -1 on error. */
int ata_read(unsigned int lba, void *buf);

/* Write one 512-byte sector from buf. Returns 0 on success, -1 on error. */
int ata_write(unsigned int lba, const void *buf);
/* Sector count reported by the drive during ata_present() */
unsigned long long ata_capacity_sectors(void);

#endif