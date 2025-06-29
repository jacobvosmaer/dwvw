
#include "decoder.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define assert(x)                                                              \
  if (!(x))                                                                    \
  __builtin_trap()

void fail(char *fmt, ...) {
  va_list ap;
  fputs("error: ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

void putbe(word x, word wordsize, FILE *f) {
  word shift;
  if (x < 0)
    x += bit(wordsize);
  for (shift = wordsize - 8; shift >= 0; shift -= 8)
    fputc(x >> shift, f);
}

int main(int argc, char **argv) {
  word inwordsize, outwordsize, nchannels, nsamples;
  if (argc != 5) {
    fputs("Usage: decode INWORDSIZE OUTWORDSIZE NCHANNELS NSAMPLES\n", stderr);
    return 1;
  }
  inwordsize = atoi(argv[1]);
  if (inwordsize < 1 || inwordsize > 32)
    fail("invalid inwordsize: %d", inwordsize);
  outwordsize = atoi(argv[2]);
  if (outwordsize < 1 || outwordsize > 32 || outwordsize < inwordsize ||
      outwordsize % 8)
    fail("outvalid outwordsize: %d", outwordsize);
  nchannels = atoi(argv[3]);
  if (nchannels < 1)
    fail("invalid number of channels: %d", nchannels);
  nsamples = atoi(argv[4]);
  if (nsamples < 1)
    fail("invalid number of samples: %d", nsamples);
  while (nchannels--) {
    word i;
    char *err;
    struct decoder d;
    decoderinit(&d, inwordsize, stdin);
    for (i = 0; i < nsamples; i++) {
      word sample;
      if (err = decodernext(&d, &sample), err)
        fail("decoder: %s", err);
      putbe(sample << (outwordsize - inwordsize), outwordsize, stdout);
    }
    if (err = decoderclose(&d), err)
      fail("decoderclose: %s", err);
    decoderprintstats(&d, stderr);
  }
  return 0;
}
