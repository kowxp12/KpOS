#ifndef RANDOM_H
#define RANDOM_H

/* Seed from the CPU cycle counter. Called once at startup. */
void random_seed(void);

/* Returns a new pseudo-random 32-bit number */
unsigned int random_next(void);

/* Returns a number from 0 to n-1 */
unsigned int random_range(unsigned int n);

#endif