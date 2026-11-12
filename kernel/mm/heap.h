#ifndef HEAP_H
#define HEAP_H

void heap_init(void);
void *kmalloc(unsigned int size);
void kfree(void *ptr);
void heap_selftest(void);

#endif