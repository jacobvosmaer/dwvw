
#include "decoder.h"
#include "fail.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

int64_t readuint(unsigned char *p, int width) {
  int i;
  int64_t x;
  assert(width > 0 && width <= 32 && !(width & 8));
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

void putbe(word x, word wordsize, FILE *f) {
  word shift;
  if (x < 0)
    x += bit(wordsize);
  for (shift = wordsize - 8; shift >= 0; shift -= 8)
    fputc(x >> shift, f);
}

int main(int argc, char **argv) {
  int commseen = 0, ssndseen = 0, nchannels, wordsize, infd;
  unsigned char *in, *p;
  uint16_t outwordsize = 0;
  uint32_t nsamples;
  int32_t formsize;
  FILE *fout;
  struct stat st;
  if (argc != 3) {
    fputs("Usage: decompress INFILE OUTFILE\n", stderr);
    exit(1);
  }
  if (infd = open(argv[1], O_RDONLY), infd < 0)
    fail("cannot open %s: %s", argv[1], strerror(errno));
  if (fstat(infd, &st))
    fail("fstat %s: %s", argv[1], strerror(errno));
  if (in = mmap(0, st.st_size, PROT_READ, MAP_SHARED, infd, 0),
      in == MAP_FAILED)
    fail("mmap %s: %s", argv[1], strerror(errno));
  if (fout = fopen(argv[2], "wb"), !fout)
    fail("cannot open: %s", argv[2]);
  p = in;
  if (readsint(p, 32) != 'FORM' || readsint(p + 8, 32) != 'AIFC')
    fail("invalid header: %12.12s", p);
  formsize = readsint(p + 4, 32);
  if (formsize < 0)
    fail("negative FORM size: %d\n", formsize);
  p += 12;
  fprintf(stderr, "formsize=%d\n", formsize);
  fwrite("FORMxxxxAIFC", 1, 12, fout);
  formsize -= 4;
  while (formsize > 0) {
    int32_t chunksize, ID;
    fprintf(stderr, "chunk: %4.4s\n", p);
    ID = readsint(p, 32);
    chunksize = readsint(p + 4, 32);
    p += 8;
    formsize -= 8;
    if (chunksize < 0)
      fail("negative chunk size: %d\n", chunksize);
    formsize -= chunksize;
    if (ID == 'COMM') {
      if (commseen)
        fail("duplicate COMM chunk");
      commseen = 1;
      fwrite("COMM\x00\x00\x00\x26", 1, 8, fout);
      nchannels = readsint(p, 16);
      if (nchannels != 1)
        fail("unsupported number of channels: %d\n", nchannels);
      nsamples = readuint(p + 2, 32);
      wordsize = readsint(p + 6, 16);
      if (wordsize < 1 || wordsize > 32)
        fail("invalid wordsize: %d\n", wordsize);
      outwordsize = (wordsize + 7) & ~7;
      fwrite(p, 1, 6, fout);
      fputc(outwordsize >> 8, fout);
      fputc(outwordsize, fout);
      fwrite(p + 8, 1, 10, fout);
      fwrite("NONE\x0enot compressed\x00", 1, 20, fout);
      p += chunksize;
    } else if (ID == 'SSND') {
      struct decoder d;
      if (ssndseen)
        fail("duplicate SSND chunk");
      ssndseen = 1;
      if (!commseen)
        fail("SSND before COMM not supported");
      if (readuint(p, 32) || readuint(p + 4, 32))
        fail("unexpected SSND first 8 bytes");
      p += 8;
      putbe('SSND', 32, fout);
      putbe(nsamples * outwordsize / 8, 32, fout);
      putbe(0, 32, fout);
      putbe(0, 32, fout);
      decoderinit(&d, wordsize, p, chunksize - 8);
      while (nsamples--) {
        word sample;
        int err;
        if (err = decodernext(&d, &sample), err)
          fail("decoder: %d", err);
        putbe(sample << (outwordsize - wordsize), outwordsize, fout);
      }
      p += decoderpos(&d);
      if ((p - in) & 1)
        p++;
    } else {
      fwrite(p - 8, 1, 8 + chunksize, fout);
      p += chunksize;
    }
  }
  if (formsize)
    fail("chunk sizes do not add up: %d", formsize);
  formsize = ftell(fout);
  if (formsize < 0)
    fail("ftell: %s", strerror(errno));
  if (fseek(fout, 4, SEEK_SET))
    fail("fseek: %s", strerror(errno));
  putbe(formsize - 8, 32, fout);
}
