/*
dwvw: a compression/decompression utility for the Typhoon DWVW audio compression
format.

DWVW was invented 1991 by Magnus Lidström and is copyright 1993 by NuEdge
Development.

This decoder is based on documentation in "fmt_typh.rtf" published on
ftp.t0.or.at.
*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define assert(x)                                                              \
  if (!(x))                                                                    \
  __builtin_trap()

#ifndef DEBUG
#define DEBUG 0
#endif

void fail(char *fmt, ...) {
  va_list ap;
  if (DEBUG)
    assert(0);
  fputs("error: ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

typedef int64_t word;
#define bit(shift) ((word)1 << (shift))

int64_t readuint(unsigned char *p, int width) {
  int i;
  int64_t x;
  assert(width > 0 && width <= 32 && !(width % 8));
  for (i = 0, x = 0; i < width / 8; i++)
    x = (x << 8) | (int64_t)*p++;
  return x;
}

int64_t readint(unsigned char *p, int width) {
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

#ifndef COMPRESSED_WORD_SIZE
#define COMPRESSED_WORD_SIZE 12
#endif

uint8_t *findchunk(int32_t ID, uint8_t *start, uint8_t *end) {
  uint8_t *p = start;
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

uint8_t *finduniquechunk(int32_t ID, uint8_t *start, uint8_t *end) {
  uint8_t *chunk = findchunk(ID, start, end), *chunk2;
  if (chunk < end)
    if (chunk2 = findchunk(ID, chunk + 8 + readint(chunk + 4, 32), end),
        chunk2 < end)
      fail("duplicate %4.4s chunk", chunk);
  return chunk;
}

uint8_t *loadform(FILE *f, int32_t *size) {
  uint8_t buf[8], *p;
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

void writeform(FILE *f, uint8_t *start, uint8_t *end) {
  if (end < start || end - start > INT32_MAX)
    fail("writeform: invalid memory range");
  putbe('FORM', 32, start);
  putbe(end - start - 8, 32, start + 4);
  putbe('AIFC', 32, start + 8);
  if (!fwrite(start, end - start, 1, f))
    fail("fwrite failed");
}

struct comm {
  int32_t size;
  int16_t nchannels;
  uint32_t nsamples;
  int16_t wordsize;
  int32_t compressiontype;
  char compressiontypestring[4];
};

struct comm loadcomm(uint8_t *in, uint8_t *inend, int32_t filetype) {
  uint8_t *comm;
  struct comm cm = {0};
  if (comm = finduniquechunk('COMM', in + 12, inend), comm == inend)
    fail("cannot find COMM chunk");
  cm.size = readint(comm + 4, 32);
  if (cm.size < (filetype == 'AIFC' ? 22 : 18))
    fail("COMM chunk too small: %d", cm.size);
  cm.nchannels = readint(comm + 8, 16);
  if (cm.nchannels < 1)
    fail("invalid number of channels: %d", cm.nchannels);
  cm.nsamples = readuint(comm + 10, 32);
  cm.wordsize = readint(comm + 14, 16);
  if (cm.wordsize < 1 || cm.wordsize > 32)
    fail("invalid wordsize: %d", cm.wordsize);
  if (cm.size >= 22) {
    cm.compressiontype = readint(comm + 8 + 18, 32);
    memmove(&cm.compressiontypestring, comm + 8 + 18, 4);
  }
  return cm;
}

struct bitwriter {
  uint8_t *p;
  int n, size;
};

void putbit(struct bitwriter *bw, int value) {
  int byte = bw->n / 8, shift = 7 - bw->n % 8;
  if (byte < bw->size) {
    if (shift == 7)
      bw->p[byte] = 0;
    bw->p[byte] |= value << shift;
    bw->n++;
  }
}

int encodedwvw(uint8_t *input, int nsamples, word inwordsize, int stride,
               uint8_t *output, uint8_t *outputend, word outwordsize) {
  int j;
  word lastsample = 0, lastdeltawidth = 0,
       deltarange = bit(outwordsize - 1) - 1;
  struct bitwriter bw = {0};
  bw.p = output;
  bw.size = outputend - output;
  for (j = 0; j < nsamples; j++) {
    int dwm, dwmsign, deltawidth, deltasign, i;
    word delta, sample = readint(input, inwordsize);
    input += stride * inwordsize / 8;
    if (inwordsize < outwordsize)
      sample <<= outwordsize - inwordsize;
    else
      sample >>= inwordsize - outwordsize; /* TODO dither? */
    delta = sample - lastsample;
    lastsample = sample;
    if (delta >= bit(outwordsize - 1))
      delta -= bit(outwordsize);
    else if (delta < -bit(outwordsize - 1))
      delta += bit(outwordsize);
    if (DEBUG > 1)
      fprintf(stderr, "delta=%lld\n", delta);
    deltasign = delta < 0;
    delta = deltasign ? -delta : delta;
    for (deltawidth = 0; (1 << deltawidth) <= delta; deltawidth++)
      ;
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
    for (i = 1; i < deltawidth; i++)
      putbit(&bw, (delta & bit(deltawidth - 1 - i)) > 0);
    if (deltawidth)
      putbit(&bw, deltasign);
    if (deltasign && delta >= deltarange)
      putbit(&bw, delta > deltarange);
  }
  return (bw.n + 7) / 8;
}

struct bitreader {
  unsigned char *data;
  int bit, size;
};

struct decoder {
  struct bitreader br;
  word deltawidth, sample, wordsize;
};

word nextbit(struct bitreader *br) {
  word b = 0;
  if (br->bit / 8 < br->size) {
    b = (br->data[br->bit / 8] & bit(7 - (br->bit % 8))) > 0;
    br->bit++;
  }
  return b;
}

static int overflow(struct bitreader *br) { return br->bit / 8 >= br->size; }

void decoderinit(struct decoder *d, word wordsize, unsigned char *data,
                 int size) {
  struct decoder empty = {0};
  *d = empty;
  d->br.data = data;
  d->br.size = size;
  d->wordsize = wordsize;
}

void Decodernext(struct decoder *d, word *sample) {
  word dwm = 0; /* "delta width modifier" */
  /* Dwm is encoded in unary as a string of zeroes followed by a stop bit and a
   * sign bit. */
  while (dwm < d->wordsize / 2 && !nextbit(&d->br))
    dwm++;
  if (dwm) { /* deltawidth is changing */
    dwm *= nextbit(&d->br) ? -1 : 1;
    d->deltawidth += dwm;
    /* Deltawidth wraps around. This allows the encoding to minimize the
     * absolute value of dwm, which matters because dwm is encoded in unary. */
    if (d->deltawidth >= d->wordsize)
      d->deltawidth -= d->wordsize;
    else if (d->deltawidth < 0)
      d->deltawidth += d->wordsize;
  }
  if (d->deltawidth) { /* non-zero delta: sample is changing */
    word i, delta;
    /* Start iteration from 1 because the leading 1 of delta is implied */
    for (i = 1, delta = 1; i < d->deltawidth; i++)
      delta = (delta << 1) | nextbit(&d->br);
    delta *= nextbit(&d->br) ? -1 : 1;
    /* The lowest possible value for delta at this point is -(1 << (wordsize
     * -1)). So if wordsize is 8, the lowest possible value is -127. In 2's
     * complement we must also be able to represent -128. To account for this
     * DWVW adds an extra bit. To save space this bit is only present when
     * needed. So -126 is 1111110 1 (no extra bit), -127 is 1111111 1 0 and -128
     * is 1111111 1 1. */
    if (delta == 1 - bit(d->wordsize - 1))
      delta -= nextbit(&d->br);
    if (DEBUG > 1)
      fprintf(stderr, "delta=%lld\n", delta);
    d->sample += delta;
    if (d->sample >= bit(d->wordsize - 1))
      d->sample -= bit(d->wordsize);
    else if (d->sample < -bit(d->wordsize - 1))
      d->sample += bit(d->wordsize);
  } else if (DEBUG > 1) {
    fputs("delta=0\n", stderr);
  }
  *sample = d->sample;
}

int decodernext(struct decoder *d, word *sample) {
  Decodernext(d, sample);
  return overflow(&d->br) ? -2 : 0;
}
int decoderpos(struct decoder *d) { return d->br.bit / 8 + 1; }

int decodedwvw(uint8_t *input, uint8_t *inend, int nsamples, word inwordsize,
               int stride, uint8_t *output, word outwordsize) {
  struct decoder d;
  uint8_t *p = output;
  int j;
  decoderinit(&d, inwordsize, input, inend - input);
  for (j = 0; j < nsamples; j++) {
    word sample;
    int err;
    if (err = decodernext(&d, &sample), err)
      fail("sample %d: decoder: %d", j, err);
    p += putbe(sample << (outwordsize - inwordsize), outwordsize, p);
    p += (stride - 1) * outwordsize / 8;
  }
  return decoderpos(&d);
}

void compress(uint8_t *in, uint8_t *inend, struct comm comm, FILE *f,
              word outwordsize) {
  uint8_t *p, *q, *out;
  int outmax;
  if (readint(in + 8, 32) == 'AIFC' && comm.compressiontype != 'NONE')
    fail("unsupported input AIFC compression format: 4.4s",
         comm.compressiontypestring);
  outmax = (inend - in) * 2 *
           (outwordsize > comm.wordsize
                ? (outwordsize + comm.wordsize - 1) / comm.wordsize
                : 1);
  if (out = malloc(outmax), !out)
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
      uint8_t *ssnd = q;
      int i;
      q += 16;
      for (i = 0; i < comm.nchannels; i++) {
        q += encodedwvw(p + 16 + i * comm.wordsize / 8, comm.nsamples,
                        comm.wordsize, comm.nchannels, q, out + outmax,
                        outwordsize);
        if (q - out >= outmax)
          fail("write overflow");
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
  writeform(f, out, q);
}

void decompress(uint8_t *in, uint8_t *inend, struct comm comm, FILE *f) {
  int16_t outwordsize = 8 * ((comm.wordsize + 7) / 8);
  uint8_t *p, *q, *out;
  int outmax;
  if (readint(in + 8, 32) != 'AIFC' || comm.compressiontype != 'DWVW')
    fail("unsupported input AIFC compression format: %4.4s",
         comm.compressiontypestring);
  outmax = inend - in + comm.nchannels * comm.nsamples * (outwordsize / 8);
  if (out = malloc(outmax), !out)
    fail("malloc out failed");
  p = in + 12;
  q = out + 12;
  while (p < inend - 8) {
    int32_t ID = readint(p, 32), size = readint(p + 4, 32);
    int i;
    if (ID == 'COMM') {
      int compressoff = 18 + 8;
      memmove(q, p, compressoff);
      putbe(outwordsize, 16, q + 14);
      memmove(q + compressoff, "NONE\x0enot compressed\x00", 20);
      putbe(18 + 20, 32, q + 4);
      q += 18 + 20 + 8;
    } else if (ID == 'SSND') {
      uint8_t *ssnd = q, *pp = p + 16;
      q += 16;
      for (i = 0; i < comm.nchannels; i++) {
        pp += decodedwvw(pp, p + 8 + readint(p + 4, 32), comm.nsamples,
                         comm.wordsize, comm.nchannels,
                         q + i * (outwordsize / 8), outwordsize);
        if ((pp - p) & 1)
          pp++;
      }
      q += comm.nchannels * comm.nsamples * (outwordsize / 8);
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
  writeform(f, out, q);
}

int main(int argc, char **argv) {
  uint8_t *in, *inend;
  int32_t filetype, insize;
  struct comm cm;
  if (argc != 2 ||
      (strcmp(argv[1], "compress") && strcmp(argv[1], "decompress"))) {
    fputs("Usage: dwvw compress|decompress\n", stderr);
    exit(1);
  }
  in = loadform(stdin, &insize);
  inend = in + insize;
  filetype = readint(in + 8, 32);
  if (filetype != 'AIFF' && filetype != 'AIFC')
    fail("invalid file type: %4.4s", in + 8);
  if (findchunk(0, in + 12, inend) != inend) /* validate chunk sizes */
    fail("zero chunk ID found");
  cm = loadcomm(in, inend, filetype);
  if (!strcmp(argv[1], "compress"))
    compress(in, inend, cm, stdout, COMPRESSED_WORD_SIZE);
  else
    decompress(in, inend, cm, stdout);
}
