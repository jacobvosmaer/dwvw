
#include "fail.h"
#include "int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char *findchunk(int32_t ID, unsigned char *start, unsigned char *end) {
  unsigned char *p = start;
  while (p < end - 8) {
    int32_t size = readint(p + 4, 32);
    if (size < 0 || size > end - (p + 8))
      fail("chunk %4.4s: invalid size %d", p, size);
    if (readint(p, 32) == ID)
      return p;
    p += size + 8;
  }
  return end;
}

unsigned char *finduniquechunk(int32_t ID, unsigned char *start,
                               unsigned char *end) {
  unsigned char *chunk = findchunk(ID, start, end), *chunk2;
  if (chunk < end)
    if (chunk2 = findchunk(ID, chunk + 8 + readint(chunk + 4, 32), end),
        chunk2 < end)
      fail("duplicate %4.4s chunk", chunk);
  return chunk;
}

unsigned char *loadform(FILE *f, int32_t *size) {
  unsigned char buf[8], *p;
  int32_t formsize;
  if (!fread(buf, sizeof(buf), 1, f))
    fail("read AIFF header: short read");
  if (readint(buf, 32) != 'FORM')
    fail("missing FORM");
  formsize = readint(buf + 4, 32);
  if (formsize < 4 || formsize > INT32_MAX - 8)
    fail("invalid FORM size: %d", formsize);
  *size = formsize + 8;
  if (p = malloc(*size), !p)
    fail("malloc input failed");
  memmove(p, buf, sizeof(buf));
  if (!fread(p + sizeof(buf), formsize, 1, f))
    fail("short read");
  return p;
}

struct bitwriter {
  unsigned char *p;
  int n;
};

void putbit(struct bitwriter *bw, int bit) {
  int byte = bw->n / 8, shift = 7 - bw->n % 8;
  if (shift == 7)
    bw->p[byte] = 0;
  bw->p[byte] |= bit << shift;
  bw->n++;
}

int encodedwvw(unsigned char *input, int nsamples, word inwordsize, int stride,
               unsigned char *output, word outwordsize) {
  word lastsample = 0, lastdeltawidth = 0,
       deltarange = (1 << (outwordsize - 1)) - 1;
  struct bitwriter bw = {0};
  bw.p = output;
  while (nsamples--) {
    int dwm, dwmsign, deltawidth, deltasign, i;
    word delta, sample = readint(input, inwordsize);
    input += stride * inwordsize / 8;
    if (inwordsize < outwordsize)
      sample <<= outwordsize - inwordsize;
    else
      sample >>= inwordsize - outwordsize; /* TODO dither? */
    delta = sample - lastsample;
    lastsample = sample;
    deltawidth = width(delta);
    dwm = deltawidth - lastdeltawidth;
    lastdeltawidth = deltawidth;
    if (dwm > outwordsize / 2)
      dwm -= outwordsize;
    else if (dwm < -outwordsize / 2)
      dwm += outwordsize;
    dwmsign = dwm < 0;
    dwm = dwmsign ? -dwm : dwm;
    for (i = 0; i < dwm; i++)
      putbit(&bw, 0);
    if (dwm < outwordsize / 2)
      putbit(&bw, 1);
    if (dwm)
      putbit(&bw, dwmsign);
    deltasign = delta < 0;
    delta = deltasign ? -delta : delta;
    for (i = 1; i < deltawidth && i < outwordsize - 1; i++)
      putbit(&bw, (delta & (1 << (deltawidth - 1 - i))) > 0);
    if (deltawidth)
      putbit(&bw, deltasign);
    if (deltasign && delta >= deltarange)
      putbit(&bw, delta > deltarange);
  }
  return (bw.n + 7) / 8;
}

const word outwordsize = 12;

int main(void) {
  unsigned char *in, *inend, *comm, *out, *p, *q;
  int32_t filetype, insize, commsize;
  in = loadform(stdin, &insize);
  if (insize > INT32_MAX / 2)
    fail("input file too large");
  inend = in + insize;
  filetype = readint(in + 8, 32);
  if (filetype != 'AIFF' && filetype != 'AIFC')
    fail("invalid file type: %4.4s", in + 8);
  if (findchunk(0, in + 12, inend) != inend) /* validate chunk sizes */
    fail("zero chunk ID found");
  if (comm = finduniquechunk('COMM', in + 12, inend), comm == inend)
    fail("cannot find COMM chunk");
  if (commsize = readint(comm + 4, 32), commsize < 22)
    fail("COMM chunk too small: %d", commsize);
  if (filetype == 'AIFC' && readint(comm + 8 + 18, 32) != 'NONE')
    fail("unsupported input AIFC compression format: %4.4s", comm + 8 + 18);
  if (out = malloc(2 * insize), !out)
    fail("malloc output failed");
  p = in + 12;
  q = out + 12;
  while (p < inend - 8) {
    int32_t ID = readint(p, 32), size = readint(p + 4, 32);
    if (ID == 'COMM') {
      int compressoff = 18 + 8;
      memmove(q, p, compressoff);
      putbe(outwordsize, 16, q + 14);
      memmove(q + compressoff, "DWVWxDelta With Variable Word Width\x00", 36);
      q[compressoff + 4] =
          0x1f; /* not allowed to put \x1f in string literal?? */
      putbe(18 + 36, 32, q + 4);
      q += 18 + 36 + 8;
    } else if (ID == 'SSND') {
      unsigned char *ssnd = q;
      int16_t nchannels = readint(comm + 8, 16);
      uint32_t nsamples = readuint(comm + 10, 32);
      int16_t inwordsize = readint(comm + 14, 16);
      int i;
      if (nchannels < 1)
        fail("invalid number of channels: %d", nchannels);
      if (inwordsize < 1 || inwordsize > 32)
        fail("invalid input word size: %d", inwordsize);
      q += 16;
      for (i = 0; i < nchannels; i++) {
        q += encodedwvw(p + 16 + i * inwordsize / 8, nsamples, inwordsize,
                        nchannels, q, outwordsize);
        q += (q - ssnd) & 1;
      }
      putbe('SSND', 32, ssnd);
      putbe(q - ssnd - 8, 32, ssnd + 4);
      putbe(0, 32, ssnd + 8);
      putbe(0, 32, ssnd + 12);
    } else {
      memmove(q, p, size + 8);
      q += size + 8;
    }
    p += size + 8;
  }
  putbe('FORM', 32, out);
  putbe(q - out - 8, 32, out + 4);
  putbe('AIFC', 32, out + 8);
  fwrite(out, q - out, 1, stdout);
}
