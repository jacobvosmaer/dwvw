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

#ifndef COMPRESSED_WORD_SIZE
#define COMPRESSED_WORD_SIZE 12
#endif

#ifndef MAX_CHANNELS
#define MAX_CHANNELS 2
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

int64_t getuint(uint8_t *p, int width) {
  int i;
  int64_t x;
  assert(width > 0 && width <= 32 && !(width % 8));
  for (i = 0, x = 0; i < width / 8; i++)
    x = (x << 8) | (int64_t)*p++;
  return x;
}

int64_t getint(uint8_t *p, int width) {
  int64_t x = getuint(p, width), sup = (int64_t)1 << (width - 1);
  if (x >= sup)
    x -= (int64_t)1 << width;
  return x;
}

int putint(int64_t x, int64_t wordsize, uint8_t *p) {
  int64_t shift;
  if (x < 0)
    x += bit(wordsize);
  for (shift = wordsize - 8; shift >= 0; shift -= 8)
    *p++ = x >> shift;
  return wordsize / 8;
}

uint8_t *findchunk(int32_t ID, uint8_t *start, uint8_t *end) {
  uint8_t *p = start;
  while (p < end - 8) {
    int32_t size = getint(p + 4, 32);
    if (size < 0 || size > end - (p + 8))
      fail("chunk %4.4s: invalid size %d", p, size);
    if (getint(p, 32) == ID)
      return p;
    p += size + 8;
  }
  return end;
}

uint8_t *finduniquechunk(int32_t ID, uint8_t *start, uint8_t *end) {
  uint8_t *chunk = findchunk(ID, start, end), *chunk2;
  if (chunk < end)
    if (chunk2 = findchunk(ID, chunk + 8 + getint(chunk + 4, 32), end),
        chunk2 < end)
      fail("duplicate %4.4s chunk", chunk);
  return chunk;
}

uint8_t *loadform(FILE *f, int32_t *size) {
  uint8_t buf[8], *p;
  int32_t formsize;
  if (!fread(buf, sizeof(buf), 1, f))
    fail("read AIFF header: short read");
  if (getint(buf, 32) != 'FORM')
    fail("missing FORM");
  formsize = getint(buf + 4, 32);
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
  putint('FORM', 32, start);
  putint(end - start - 8, 32, start + 4);
  putint('AIFC', 32, start + 8);
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
  cm.size = getint(comm + 4, 32);
  if (cm.size < (filetype == 'AIFC' ? 22 : 18))
    fail("COMM chunk too small: %d", cm.size);
  cm.nchannels = getint(comm + 8, 16);
  if (cm.nchannels < 1 || cm.nchannels > MAX_CHANNELS)
    fail("invalid number of channels: %d", cm.nchannels);
  cm.nsamples = getuint(comm + 10, 32);
  cm.wordsize = getint(comm + 14, 16);
  if (cm.wordsize < 1 || cm.wordsize > 32)
    fail("invalid wordsize: %d", cm.wordsize);
  if (filetype == 'AIFC') {
    cm.compressiontype = getint(comm + 8 + 18, 32);
    memmove(&cm.compressiontypestring, comm + 8 + 18, 4);
  }
  return cm;
}

struct bitwriter {
  uint8_t *data;
  int32_t n, size;
};

void putbit(struct bitwriter *bw, int value) {
  int byte = bw->n / 8, shift = 7 - bw->n % 8;
  if (byte < bw->size) {
    if (shift == 7)
      bw->data[byte] = 0;
    bw->data[byte] |= value << shift;
    bw->n++;
  }
}

word convertbitdepth(word sample, word inwordsize, word outwordsize) {
  if (outwordsize > inwordsize)
    return sample << (outwordsize - inwordsize);
  else /* TODO use dithering? */
    return sample >> (inwordsize - outwordsize);
}

int encodedwvw(uint8_t *input, uint32_t nsamples, word inwordsize, int stride,
               uint8_t *output, uint8_t *outputend, word outwordsize) {
  uint32_t j;
  word lastsample = 0, lastdeltawidth = 0;
  struct bitwriter bw = {0};
  bw.data = output;
  bw.size = outputend - output;
  for (j = 0; j < nsamples; j++) {
    int dwm, dwmsign, deltawidth, deltasign, i;
    word delta, sample;

    sample =
        convertbitdepth(getint(input, inwordsize), inwordsize, outwordsize);
    input += stride * inwordsize / 8;

    delta = sample - lastsample;
    lastsample = sample;
    /* DWVW stores outwordsize bit inter-sample differences as an (outwordsize -
     * 1) bit absolute value plus a sign bit. To make that fit the delta must
     * wrap around. */
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
    dwm = deltawidth - lastdeltawidth; /* delta width modifier */
    lastdeltawidth = deltawidth;
    if (dwm > outwordsize / 2)
      dwm -= outwordsize;
    else if (dwm < -outwordsize / 2)
      dwm += outwordsize;

    dwmsign = dwm < 0;
    dwm = dwmsign ? -dwm : dwm;
    for (i = 0; i < dwm; i++) /* Store dwm in unary */
      putbit(&bw, 0);
    if (dwm < outwordsize / 2) /* Dwm stop bit */
      putbit(&bw, 1);
    if (dwm)
      putbit(&bw, dwmsign);

    for (i = 1; i < deltawidth; i++) /* Store delta in binary */
      putbit(&bw, (delta & bit(deltawidth - 1 - i)) > 0);
    if (delta)
      putbit(&bw, deltasign);
    /* Extra bit for otherwise unrepresentable value -(1 << (outwordsize - 1))
     */
    if (deltasign && delta >= bit(outwordsize - 1) - 1)
      putbit(&bw, delta == bit(outwordsize - 1));
  }
  return (bw.n + 7) / 8;
}

struct bitreader {
  uint8_t *data;
  int32_t n, size;
};

word getbit(struct bitreader *br) {
  word b = 0;
  if (br->n / 8 < br->size) {
    b = (br->data[br->n / 8] & bit(7 - (br->n % 8))) > 0;
    br->n++;
  }
  return b;
}

int decodedwvw(uint8_t *input, uint8_t *inend, uint32_t nsamples,
               word inwordsize, int stride, uint8_t *output, word outwordsize) {
  struct bitreader br = {0};
  word deltawidth = 0, sample = 0;
  uint32_t j;
  br.data = input;
  br.size = inend - input;
  for (j = 0; j < nsamples; j++) {
    word i, delta, dwm; /* "delta width modifier" */

    dwm = 0;
    while (dwm < inwordsize / 2 && !getbit(&br))
      dwm++;
    if (dwm) /* Dwm sign omitted if dwm is zero */
      dwm = getbit(&br) ? -dwm : dwm;

    deltawidth += dwm;
    if (deltawidth >= inwordsize)
      deltawidth -= inwordsize;
    else if (deltawidth < 0)
      deltawidth += inwordsize;

    delta = 0;
    if (deltawidth) {
      delta = 1;
      for (i = 1; i < deltawidth; i++)
        delta = (delta << 1) | getbit(&br);
      delta = getbit(&br) ? -delta : delta;
      /* Trick to represent -(1 << (inwordsize - 1)) */
      if (delta == 1 - bit(inwordsize - 1))
        delta -= getbit(&br);
    }
    if (DEBUG > 1)
      fprintf(stderr, "delta=%lld\n", delta);

    sample += delta;
    if (sample >= bit(inwordsize - 1))
      sample -= bit(inwordsize);
    else if (sample < -bit(inwordsize - 1))
      sample += bit(inwordsize);

    output += putint(convertbitdepth(sample, inwordsize, outwordsize),
                     outwordsize, output);
    output += (stride - 1) * outwordsize / 8;
  }
  return (br.n + 7) / 8;
}

void compress(uint8_t *in, uint8_t *inend, struct comm comm, FILE *f,
              word outwordsize) {
  uint8_t *p, *q, *out;
  int outmax;
  if (getint(in + 8, 32) == 'AIFC' && comm.compressiontype != 'NONE')
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
    int32_t ID = getint(p, 32), size = getint(p + 4, 32);
    if (ID == 'COMM') {
      uint8_t compressinfo[] = "DWVWxDelta With Variable Word Width\x00";
      int compressoff = 18 + 8, compressinfosize = sizeof(compressinfo) - 1;
      memmove(q, p, compressoff);
      putint(outwordsize, 16, q + 14);
      memmove(q + compressoff, compressinfo, compressinfosize);
      q[compressoff + 4] =
          0x1f; /* not allowed to put \x1f in string literal?? */
      putint(18 + compressinfosize, 32, q + 4);
      q += 18 + sizeof(compressinfo) + 8;
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
      putint('SSND', 32, ssnd);
      putint(q - ssnd - 8, 32, ssnd + 4);
      putint(0, 32, ssnd + 8);
      putint(0, 32, ssnd + 12);
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
  if (getint(in + 8, 32) != 'AIFC' || comm.compressiontype != 'DWVW')
    fail("unsupported input AIFC compression format: %4.4s",
         comm.compressiontypestring);
  outmax = inend - in + comm.nchannels * comm.nsamples * (outwordsize / 8);
  if (out = malloc(outmax), !out)
    fail("malloc out failed");
  p = in + 12;
  q = out + 12;
  while (p < inend - 8) {
    int32_t ID = getint(p, 32), size = getint(p + 4, 32);
    int i;
    if (ID == 'COMM') {
      uint8_t compressinfo[] = "NONE\x0enot compressed\x00";
      int compressoff = 18 + 8, compressinfosize = sizeof(compressinfo) - 1;
      memmove(q, p, compressoff);
      putint(outwordsize, 16, q + 14);
      memmove(q + compressoff, compressinfo, compressinfosize);
      putint(18 + compressinfosize, 32, q + 4);
      q += 18 + compressinfosize + 8;
    } else if (ID == 'SSND') {
      uint8_t *ssnd = q, *dwvwstart = p + 16, *dwvwend = p + 8 + size;
      q += 16;
      for (i = 0; i < comm.nchannels; i++) {
        dwvwstart += decodedwvw(dwvwstart, dwvwend, comm.nsamples,
                                comm.wordsize, comm.nchannels,
                                ssnd + 16 + i * (outwordsize / 8), outwordsize);
        if (dwvwstart >= dwvwend)
          fail("read overflow");
        dwvwstart += (dwvwstart - p) & 1;
        q += comm.nsamples * (outwordsize / 8);
      }
      putint('SSND', 32, ssnd);
      putint(q - ssnd - 8, 32, ssnd + 4);
      putint(0, 32, ssnd + 8);
      putint(0, 32, ssnd + 12);
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
  FILE *fin, *fout;
  if (argc != 4 ||
      (strcmp(argv[1], "compress") && strcmp(argv[1], "decompress"))) {
    fputs("Usage: dwvw compress|decompress INFILE OUTFILE\n", stderr);
    exit(1);
  }
  if (fin = fopen(argv[2], "rb"), !fin)
    fail("failed to open %s", argv[2]);
  if (fout = fopen(argv[3], "wb"), !fout)
    fail("failed to open %s", argv[3]);
  in = loadform(fin, &insize);
  inend = in + insize;
  filetype = getint(in + 8, 32);
  if (filetype != 'AIFF' && filetype != 'AIFC')
    fail("invalid file type: %4.4s", in + 8);
  if (findchunk(0, in + 12, inend) != inend) /* validate chunk sizes */
    fail("zero chunk ID found");
  cm = loadcomm(in, inend, filetype);
  if (!strcmp(argv[1], "compress"))
    compress(in, inend, cm, fout, COMPRESSED_WORD_SIZE);
  else
    decompress(in, inend, cm, fout);
}
