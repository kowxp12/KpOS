#ifndef UTIL_H
#define UTIL_H

int          str_eq(const char *a, const char *b);
unsigned int str_len(const char *s);
void         print_udec(unsigned int value);
int          parse_uint(const char *s, unsigned int *out);

#endif