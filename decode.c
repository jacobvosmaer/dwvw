
#include "decoder.h"
#include "fail.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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
    failerrno("stat stdin");
  data = mmap(0, st.st_size, PROT_READ, MAP_SHARED, STDIN_FILENO, 0);
  if (data == MAP_FAILED)
    failerrno("mmap stdin");
  while (nchannels--) {
    word i;
    int err;
    struct decoder d;
    decoderinit(&d, inwordsize, data, st.st_size);
    for (i = 0; i < nsamples; i++) {
      word sample;
      unsigned char buf[4];
      if (err = decodernext(&d, &sample), err)
        fail("decoder: %d", err);
      putbe(sample << (outwordsize - inwordsize), outwordsize, buf);
      fwrite(buf, 1, sizeof(buf), stdout);
    }
    decoderprintstats(&d, stderr);
  }
  return 0;
}
