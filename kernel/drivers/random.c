#include "random.h"

static unsigned int state = 0;

/* Read the CPU's cycle counter (RDTSC). It changes every clock tick. */
static unsigned int cycle_counter(void)
{
    unsigned int lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return lo ^ hi;
}

void random_seed(void)
{
    state = cycle_counter() ^ 0x9E3779B9u;
    if (state == 0)
        state = 1;   /* xorshift must never be zero */
}

/* xorshift32: each call advances the state and returns the new value */
unsigned int random_next(void)
{
    if (state == 0)
        random_seed();

    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

unsigned int random_range(unsigned int n)
{
    if (n == 0)
        return 0;

    /* Use the high bits, which are more evenly mixed than the low bits */
    return (random_next() >> 8) % n;
}