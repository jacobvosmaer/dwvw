
#include "decoder.h"
#include "fail.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void putbe(word x, word wordsize, FILE *f) {
  word shift;
  if (x < 0)
    x += bit(wordsize);
  for (shift = wordsize - 8; shift >= 0; shift -= 8)
    fputc(x >> shift, f);
}

int main(int argc, char **argv) {
  word inwordsize, outwordsize, nchannels, nsamples;
  void *data;
  struct stat st;
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
  if (fstat(STDIN_FILENO, &st))
    fail("stat stdin: %s", strerror(errno));
  data = mmap(0, st.st_size, PROT_READ, MAP_SHARED, STDIN_FILENO, 0);
  if (data == MAP_FAILED)
    fail("mmap stdin: %s", strerror(errno));
  while (nchannels--) {
    word i;
    int err;
    struct decoder d;
    decoderinit(&d, inwordsize, data, st.st_size);
    for (i = 0; i < nsamples; i++) {
      word sample;
      if (err = decodernext(&d, &sample), err)
        fail("decoder: %d", err);
      putbe(sample << (outwordsize - inwordsize), outwordsize, stdout);
    }
    decoderprintstats(&d, stderr);
  }
  return 0;
}
