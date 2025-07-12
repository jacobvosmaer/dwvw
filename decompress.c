
#include "aiff.h"
#include "decoder.h"
#include "fail.h"
#include "int.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define nelem(x) (sizeof(x) / sizeof(*(x)))

struct aiff aiff;
unsigned char commbuf[38];

int main(int argc, char **argv) {
  int nchannels, wordsize, infd, outfd, i;
  struct chunk *ch;
  char *err;
  unsigned char *in, *p, *out, *ssndstart;
  uint16_t outwordsize = 0;
  uint32_t nsamples;
  int32_t formsize;
  struct chunk *comm = 0, *ssnd = 0, ssndcompressed;
  struct stat st;
  struct comm cm;

  if (argc != 3) {
    fputs("Usage: decompress INFILE OUTFILE\n", stderr);
    exit(1);
  }
  if (infd = open(argv[1], O_RDONLY), infd < 0)
    failerrno("cannot open %s", argv[1]);
  if (outfd = open(argv[2], O_RDWR | O_CREAT | O_TRUNC, 0644), outfd < 0)
    failerrno("open: %s", argv[2]);

  if (fstat(infd, &st))
    failerrno("fstat %s", argv[1]);
  if (in = mmap(0, st.st_size, PROT_READ, MAP_SHARED, infd, 0),
      in == MAP_FAILED)
    failerrno("mmap %s", argv[1]);
  if (err = aiffload(&aiff, in, st.st_size), err)
    fail("load aiff: %s", err);

  if (err = aiffcomm(&aiff, &cm), err)
    fail("aiffcomm: %s", err);
  comm = aiffchunk(&aiff, 'COMM');
  if (comm->size < 18)
    fail("COMM too small: %d", comm->size);
  memmove(commbuf, comm->data, 18);
  memmove(commbuf + 18, "NONE\x0enot compressed\x00", 20);
  comm->data = commbuf;
  comm->size = sizeof(commbuf);
  nchannels = readint(comm->data, 16);
  if (nchannels < 1)
    fail("invalid number of channels: %d", nchannels);
  nsamples = readint(comm->data + 2, 32);
  if (nsamples < 0)
    fail("invalid number of samples: %d", nsamples);
  wordsize = readint(comm->data + 6, 16);
  if (wordsize < 1 || wordsize > 32)
    fail("invalid wordsize: %d", wordsize);
  outwordsize = (wordsize + 7) & ~7;
  comm->data[6] = outwordsize >> 8;
  comm->data[7] = outwordsize;

  ssnd = aiffchunk(&aiff, 'SSND');
  ssndcompressed = *ssnd;
  ssnd->size = 8 + nsamples * nchannels * outwordsize / 8;

  for (ch = aiff.chunk, formsize = 4; ch < aiff.chunk + aiff.nchunk; ch++)
    formsize += ch->size;
  if (ftruncate(outfd, formsize + 8))
    failerrno("ftruncate");
  if (out = mmap(0, formsize + 8, PROT_WRITE, MAP_SHARED, outfd, 0),
      out == MAP_FAILED)
    failerrno("mmap %s", argv[2]);
  p = out;
  p += putbe('FORM', 32, p);
  p += putbe(formsize, 32, p);
  p += putbe('AIFC', 32, p);
  for (ch = aiff.chunk; ch < aiff.chunk + aiff.nchunk; ch++) {
    p += putbe(ch->ID, 32, p);
    p += putbe(ch->size, 32, p);
    if (ch->ID == 'SSND')
      ssndstart = p;
    else
      memmove(p, ch->data, ch->size);
    p += ch->size;
  }

  putbe(0, 32, ssndstart);
  putbe(0, 32, ssndstart + 4);
  ssndcompressed.data += 8;
  for (i = 0; i < nchannels; i++) {
    struct decoder d;
    int pos, j;
    decoderinit(&d, wordsize, ssndcompressed.data, ssndcompressed.size);
    p = ssndstart + 8 + i * outwordsize / 8;
    for (j = 0; j < nsamples; j++) {
      word sample;
      int err;
      if (err = decodernext(&d, &sample), err)
        fail("decoder: %d", err);
      p += putbe(sample << (outwordsize - wordsize), outwordsize, p);
      p += (nchannels - 1) * outwordsize / 8;
    }
    pos = decoderpos(&d);
    if (pos & 1)
      pos++;
    ssndcompressed.data += pos;
    ssndcompressed.size -= pos;
  }
}
