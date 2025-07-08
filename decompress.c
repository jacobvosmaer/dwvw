
#include "decoder.h"
#include "fail.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define nelem(x) (sizeof(x) / sizeof(*(x)))

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

int putbe(word x, word wordsize, unsigned char *p) {
  word shift;
  if (x < 0)
    x += bit(wordsize);
  for (shift = wordsize - 8; shift >= 0; shift -= 8)
    *p++ = x >> shift;
  return wordsize / 8;
}

struct chunk {
  int32_t ID, size;
  unsigned char *data;
} chunk[32];

int nchunk;

void findchunks(unsigned char *data, int size) {
  unsigned char *end = data + size;
  while (data < end) {
    struct chunk *ch = chunk + nchunk++;
    if (nchunk == nelem(chunk))
      fail("too many chunks");
    ch->ID = readsint(data, 32);
    ch->size = readsint(data + 4, 32);
    if (ch->size < 0)
      fail("chunk %4.4s has negative size %d", data, ch->size);
    ch->data = data + 8;
    data += 8 + ch->size;
  }
  if (data != end)
    fail("sum of chunks %lld larger than FORM", data - end);
}

struct chunk *getchunk(int32_t ID) {
  unsigned char idbuf[4];
  struct chunk *ch, *out = 0;
  putbe(ID, 32, idbuf);
  for (ch = chunk; ch < chunk + nchunk; ch++) {
    if (ch->ID == ID) {
      if (out)
        fail("more than one %s chunk", idbuf);
      else
        out = ch;
    }
  }
  if (!out)
    fail("missing %s", idbuf);
  return out;
}

unsigned char commbuf[38];

int main(int argc, char **argv) {
  int nchannels, wordsize, infd, outfd, i;
  struct chunk *ch;
  unsigned char *in, *p, *out, *ssndstart;
  uint16_t outwordsize = 0;
  uint32_t nsamples;
  int32_t formsize;
  struct chunk *comm = 0, *ssnd = 0, ssndcompressed;
  struct stat st;

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
  p = in;
  if (readsint(p, 32) != 'FORM' || readsint(p + 8, 32) != 'AIFC')
    fail("invalid header: %12.12s", p);
  formsize = readsint(p + 4, 32);
  if (formsize < 0)
    fail("negative FORM size: %d\n", formsize);
  p += 12;
  findchunks(p, formsize - 4);

  comm = getchunk('COMM');
  if (comm->size < 18)
    fail("COMM too small: %d", comm->size);
  memmove(commbuf, comm->data, 18);
  memmove(commbuf + 18, "NONE\x0enot compressed\x00", 20);
  comm->data = commbuf;
  comm->size = sizeof(commbuf);
  nchannels = readsint(comm->data, 16);
  if (nchannels < 1)
    fail("invalid number of channels: %d", nchannels);
  nsamples = readsint(comm->data + 2, 32);
  if (nsamples < 0)
    fail("invalid number of samples: %d", nsamples);
  wordsize = readsint(comm->data + 6, 16);
  if (wordsize < 1 || wordsize > 32)
    fail("invalid wordsize: %d", wordsize);
  outwordsize = (wordsize + 7) & ~7;
  comm->data[6] = outwordsize >> 8;
  comm->data[7] = outwordsize;

  ssnd = getchunk('SSND');
  ssndcompressed = *ssnd;
  ssnd->size = 8 + nsamples * nchannels * outwordsize / 8;

  for (ch = chunk, formsize = 4; ch < chunk + nchunk; ch++)
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
  for (ch = chunk; ch < chunk + nchunk; ch++) {
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
