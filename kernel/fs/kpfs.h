#ifndef KPFS_H
#define KPFS_H

int kpfs_format(void);
int kpfs_probe(void);

/* Folders */
int kpfs_list(void);
int kpfs_mkdir(const char *name);
int kpfs_cd(const char *name);
int kpfs_pwd(void);

/* Files and items */
int kpfs_write(const char *name, const char *data, unsigned int len);
int kpfs_read(const char *name);
int kpfs_remove(const char *name, int force);
int kpfs_move(const char *name, const char *dest);
int kpfs_rename(const char *old_name, const char *new_name);

#endif