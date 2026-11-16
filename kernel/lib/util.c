#include "util.h"
#include "vga.h"

int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

unsigned int str_len(const char *s)
{
    unsigned int n = 0;
    while (s[n])
        n++;
    return n;
}

void print_udec(unsigned int value)
{
    char buf[12];
    int i = 0;

    if (value == 0)
        buf[i++] = '0';
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    while (i > 0)
        vga_putchar(buf[--i]);
}

int parse_uint(const char *s, unsigned int *out)
{
    unsigned int n = 0;

    if (!*s)
        return 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        n = n * 10 + (*p - '0');
    }
    *out = n;
    return 1;
}