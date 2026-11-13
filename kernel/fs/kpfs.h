#ifndef KPFS_H
#define KPFS_H

/* Stage 1 */
int kpfs_format(void);
int kpfs_probe(void);
int kpfs_list(void);
int kpfs_mkdir(const char *name);
int kpfs_cd(const char *name);
int kpfs_pwd(void);

/* Stage 2: not implemented yet */
int kpfs_write(const char *name, const char *data, unsigned int len);
int kpfs_read(const char *name);
int kpfs_remove(const char *name);

#endif