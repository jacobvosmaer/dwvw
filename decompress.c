
#include "decoder.h"
#include "fail.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void copy(FILE *fout, FILE *fin, int64_t n) {
  assert(n >= 0);
  while (n--) {
    int c = fgetc(fin);
    if (c < 0)
      fail("copy: read error");
    fputc(c, fout);
  }
}

int main(int argc, char **argv) {
  unsigned char buf[18];
  int commseen = 0, ssndseen = 0, nchannels, wordsize;
  uint16_t outwordsize = 0;
  uint32_t nsamples;
  int32_t formsize;
  FILE *fin, *fout;
  if (argc != 3) {
    fputs("Usage: decompress INFILE OUTFILE\n", stderr);
    exit(1);
  }
  if (fin = fopen(argv[1], "rb"), !fin)
    fail("cannot open: %s", argv[1]);
  if (fout = fopen(argv[2], "wb"), !fout)
    fail("cannot open: %s", argv[2]);

  if (fread(buf, 1, 12, fin) != 12)
    fail("short read");
  if (memcmp(buf, "FORM", 4) || memcmp(buf + 8, "AIFC", 4))
    fail("invalid header: %12.12s", buf);
  formsize = readsint(buf + 4, 32);
  if (formsize < 0)
    fail("negative FORM size: %d\n", formsize);
  fprintf(stderr, "formsize=%d\n", formsize);
  fwrite("FORMxxxxAIFC", 1, 12, fout);
  formsize -= 4;
  while (formsize > 0) {
    int32_t chunksize;
    if (fread(buf, 1, 8, fin) != 8)
      fail("chunk header: short read");
    formsize -= 8;
    fprintf(stderr, "chunk: %4.4s\n", buf);
    chunksize = readsint(buf + 4, 32);
    if (chunksize < 0)
      fail("negative chunk size: %d\n", chunksize);
    formsize -= chunksize;
    if (!memcmp(buf, "COMM", 4)) {
      if (commseen)
        fail("duplicate COMM chunk");
      commseen = 1;
      fwrite("COMM\x00\x00\x00\x26", 1, 8, fout);
      if (fread(buf, 1, 18, fin) != 18)
        fail("read COMM");
      chunksize -= 18;
      nchannels = readsint(buf, 16);
      if (nchannels != 1)
        fail("unsupported number of channels: %d\n", nchannels);
      nsamples = readuint(buf + 2, 32);
      wordsize = readsint(buf + 6, 16);
      if (wordsize < 1 || wordsize > 32)
        fail("invalid wordsize: %d\n", wordsize);
      outwordsize = (wordsize + 7) & ~7;
      buf[6] = outwordsize >> 8;
      buf[7] = outwordsize;
      fwrite(buf, 1, 18, fout);
      fwrite("NONE\x0enot compressed\x00", 1, 20, fout);
      if (fseek(fin, chunksize, SEEK_CUR))
        fail("fseek failed");
    } else if (!memcmp(buf, "SSND", 4)) {
      struct decoder d;
      if (ssndseen)
        fail("duplicate SSND chunk");
      ssndseen = 1;
      if (fread(buf, 1, 8, fin) != 8)
        fail("read SSND first 8 bytes");
      if (memcmp(buf, "\x00\x00\x00\x00\x00\x00\x00\x00", 8))
        fail("unexpected SSND first 8 bytes");
      fputs("SSND", fout);
      putbe(nsamples * outwordsize / 8, 32, fout);
      putbe(0, 32, fout);
      putbe(0, 32, fout);
      decoderinit(&d, wordsize, fin);
      while (nsamples--) {
        word sample;
        char *err;
        if (err = decodernext(&d, &sample), err)
          fail("decoder: %s");
        putbe(sample << (outwordsize - wordsize), outwordsize, fout);
      }
      decoderclose(&d);
    } else {
      fwrite(buf, 1, 8, fout);
      copy(fout, fin, chunksize);
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
