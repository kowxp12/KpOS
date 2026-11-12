#ifndef PMM_H
#define PMM_H

/* Read the memory map from GRUB and mark usable pages as free */
void pmm_init(unsigned int mbi_addr);

/* Returns the address of one free 4 KB page, or 0 if none are left */
void *pmm_alloc_page(void);

/* Gives a page back so it can be used again */
void pmm_free_page(void *ptr);

/* Number of free pages right now */
unsigned int pmm_free_pages(void);

/* Quick test: allocate, print, and free a couple of pages */
void pmm_selftest(void);

#endif