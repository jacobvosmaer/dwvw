#include "int.h"
#include "fail.h"

int64_t readuint(unsigned char *p, int width) {
  int i;
  int64_t x;
  assert(width > 0 && width <= 32 && !(width % 8));
  for (i = 0, x = 0; i < width / 8; i++)
    x = (x << 8) | (int64_t)*p++;
  return x;
}

int64_t readsint(unsigned char *p, int width) {
  int64_t x = readuint(p, width), sup = (int64_t)1 << (width - 1);
  if (x >= sup)
    x -= (int64_t)1 << width;
  return x;
}

int putbe(word x, word wordsize, unsigned char *p) {
  word shift;
  if (x < 0)
    x += bit(wordsize);
  for (shift = wordsize - 8; shift >= 0; shift -= 8)
    *p++ = x >> shift;
  return wordsize / 8;
}
