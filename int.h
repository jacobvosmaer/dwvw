#ifndef INT_H
#define INT_H

#include <stdint.h>

typedef int64_t word;
#define bit(shift) ((word)1 << (shift))

int64_t readuint(unsigned char *p, int width);
int64_t readint(unsigned char *p, int width);
int putbe(word x, word wordsize, unsigned char *p);

#endif
