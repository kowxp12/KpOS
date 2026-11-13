#ifndef KPFS_H
#define KPFS_H

/* Erase the disk and write a fresh KPFS layout. Returns 0 on success. */
int kpfs_format(void);

/* Returns 1 if the disk has a valid KPFS superblock, 0 otherwise */
int kpfs_probe(void);

/* Create or replace a file. len is the number of bytes in data. */
int kpfs_write(const char *name, const char *data, unsigned int len);

/* Print a file's contents. */
int kpfs_read(const char *name);

/* Delete a file. */
int kpfs_remove(const char *name);

/* List all files with their sizes. */
int kpfs_list(void);

#endif