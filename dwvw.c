/*
dwvw: a compression/decompression utility for the Typhoon DWVW audio compression
format.

DWVW was invented 1991 by Magnus Lidström and is copyright 1993 by NuEdge
Development.

This decoder is based on documentation in "fmt_typh.rtf" published on
ftp.t0.or.at.
*/

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
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

#define FORM ('F' << 24 | 'O' << 16 | 'R' << 8 | 'M')
#define AIFF ('A' << 24 | 'I' << 16 | 'F' << 8 | 'F')
#define AIFC ('A' << 24 | 'I' << 16 | 'F' << 8 | 'C')
#define COMM ('C' << 24 | 'O' << 16 | 'M' << 8 | 'M')
#define NONE ('N' << 24 | 'O' << 16 | 'N' << 8 | 'E')
#define DWVW ('D' << 24 | 'W' << 16 | 'V' << 8 | 'W')
#define SSND ('S' << 24 | 'S' << 16 | 'N' << 8 | 'D')

#define CHUNK_HEADER 8
#define FORM_HEADER 8

#if __clang__ || __GNUC__
void fail(char *fmt, ...) __attribute__((format(printf, 1, 2)));
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

#define bit(shift) ((int64_t)1 << (shift))

int64_t getuint(uint8_t *p, int width) {
  int i;
  int64_t x;
  assert(width > 0 && width <= 32 && !(width % 8));
  for (i = 0, x = 0; i < width / 8; i++)
    x = (x << 8) | (int64_t)*p++;
  return x;
}

int64_t getint(uint8_t *p, int width) {
  int64_t x = getuint(p, width);
  if (x >= bit(width - 1))
    x -= bit(width);
  return x;
}

int putint(int64_t x, int wordsize, uint8_t *p) {
  int shift;
  if (x < 0)
    x += bit(wordsize);
  for (shift = wordsize - 8; shift >= 0; shift -= 8)
    *p++ = x >> shift;
  return wordsize / 8;
}

int64_t nextchunk(int32_t size) {
  /* The AIFF spec says chunk size may be odd but the next chunk must start at
   * an even offset. */
  return (int64_t)CHUNK_HEADER + (int64_t)size + (int64_t)(size & 1);
}

uint8_t *findchunk(int32_t ID, uint8_t *start, uint8_t *end) {
  uint8_t *p = start;
  while (p < end - CHUNK_HEADER) {
    int32_t size = getint(p + 4, 32);
    if (size < 0 || size > end - (p + CHUNK_HEADER))
      fail("chunk %4.4s: invalid size %d", p, size);
    if (getint(p, 32) == ID)
      return p;
    p += nextchunk(size);
  }
  return end;
}

uint8_t *finduniquechunk(int32_t ID, uint8_t *start, uint8_t *end) {
  uint8_t *chunk = findchunk(ID, start, end);
  if (chunk < end) {
    uint8_t *chunk2, *tail = chunk + nextchunk(getint(chunk + 4, 32));
    if (chunk2 = findchunk(ID, tail, end), chunk2 < end)
      fail("duplicate %4.4s chunk", chunk);
  }
  return chunk;
}

uint8_t *loadform(FILE *f, int32_t *size) {
  uint8_t buf[FORM_HEADER], *p;
  int32_t formsize;
  if (!fread(buf, sizeof(buf), 1, f))
    fail("read AIFF header: short read");
  if (getint(buf, 32) != FORM)
    fail("missing FORM");
  formsize = getint(buf + 4, 32);
  if (formsize < 4 || formsize > INT32_MAX - FORM_HEADER)
    fail("invalid FORM size: %d", formsize);
  *size = formsize + FORM_HEADER;
  if (p = malloc(*size), !p)
    fail("malloc input failed");
  memmove(p, buf, sizeof(buf));
  if (!fread(p + sizeof(buf), formsize, 1, f))
    fail("short read");
  return p;
}

void writeform(FILE *f, uint8_t *start, uint8_t *end) {
  if (end < start || end - start > (ptrdiff_t)INT32_MAX + FORM_HEADER)
    fail("writeform: invalid memory range");
  putint(FORM, 32, start);
  putint(end - start - FORM_HEADER, 32, start + 4);
  putint(AIFC, 32, start + FORM_HEADER);
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
  if (comm = finduniquechunk(COMM, in + FORM_HEADER + 4, inend), comm == inend)
    fail("cannot find COMM chunk");
  cm.size = getint(comm + 4, 32);
  if (cm.size < (filetype == AIFC ? 22 : 18))
    fail("COMM chunk too small: %d", cm.size);
  cm.nchannels = getint(comm + CHUNK_HEADER, 16);
  if (cm.nchannels < 1 || cm.nchannels > MAX_CHANNELS)
    fail("invalid number of channels: %d", cm.nchannels);
  cm.nsamples = getuint(comm + CHUNK_HEADER + 2, 32);
  cm.wordsize = getint(comm + CHUNK_HEADER + 6, 16);
  if (cm.wordsize < 1 || cm.wordsize > 32)
    fail("invalid wordsize: %d", cm.wordsize);
  if (filetype == AIFC) {
    cm.compressiontype = getint(comm + CHUNK_HEADER + 18, 32);
    memmove(&cm.compressiontypestring, comm + CHUNK_HEADER + 18, 4);
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

int64_t convertbitdepth(int64_t sample, int inwordsize, int outwordsize) {
  if (outwordsize > inwordsize)
    return sample << (outwordsize - inwordsize);
  else /* TODO use dithering? */
    return sample >> (inwordsize - outwordsize);
}

int encodedwvw(uint8_t *input, uint32_t nsamples, int inwordsize, int stride,
               uint8_t *output, uint8_t *outputend, int outwordsize) {
  uint32_t j;
  int64_t lastsample = 0;
  int lastwidth = 0;
  struct bitwriter bw = {0};
  bw.data = output;
  bw.size = outputend - output;
  for (j = 0; j < nsamples; j++) {
    int widthdelta, widthdeltasign, width, sampledeltasign, i;
    int64_t sampledelta, sample;

    sample =
        convertbitdepth(getint(input, inwordsize), inwordsize, outwordsize);
    input += stride * inwordsize / 8;

    sampledelta = sample - lastsample;
    lastsample = sample;
    /* DWVW stores outwordsize bit inter-sample differences as an (outwordsize -
     * 1) bit absolute value plus a sign bit. To make that fit the sampledelta
     * must wrap around. */
    if (sampledelta >= bit(outwordsize - 1))
      sampledelta -= bit(outwordsize);
    else if (sampledelta < -bit(outwordsize - 1))
      sampledelta += bit(outwordsize);
    if (DEBUG > 1)
      fprintf(stderr, "sampledelta=%" PRId64 "\n", sampledelta);

    sampledeltasign = sampledelta < 0;
    sampledelta = sampledeltasign ? -sampledelta : sampledelta;
    for (width = 0; (1 << width) <= sampledelta; width++)
      ;
    widthdelta = width - lastwidth;
    lastwidth = width;
    if (widthdelta > outwordsize / 2)
      widthdelta -= outwordsize;
    else if (widthdelta < -outwordsize / 2)
      widthdelta += outwordsize;

    widthdeltasign = widthdelta < 0;
    widthdelta = widthdeltasign ? -widthdelta : widthdelta;
    for (i = 0; i < widthdelta; i++) /* Store widthdelta in unary */
      putbit(&bw, 0);
    if (widthdelta < outwordsize / 2) /* Widthdelta stop bit */
      putbit(&bw, 1);
    if (widthdelta)
      putbit(&bw, widthdeltasign);

    for (i = 1; i < width; i++) /* Store sampledelta in binary */
      putbit(&bw, (sampledelta & bit(width - 1 - i)) > 0);
    if (sampledelta)
      putbit(&bw, sampledeltasign);
    /* Extra bit for otherwise unrepresentable value -(1 << (outwordsize - 1))
     */
    if (sampledeltasign && sampledelta >= bit(outwordsize - 1) - 1)
      putbit(&bw, sampledelta == bit(outwordsize - 1));
  }
  return (bw.n + 7) / 8;
}

struct bitreader {
  uint8_t *data;
  int32_t n, size;
};

int64_t getbit(struct bitreader *br) {
  int64_t b = 0;
  if (br->n / 8 < br->size) {
    b = (br->data[br->n / 8] & bit(7 - (br->n % 8))) > 0;
    br->n++;
  }
  return b;
}

int decodedwvw(uint8_t *input, uint8_t *inend, uint32_t nsamples,
               int inwordsize, int stride, uint8_t *output, int outwordsize) {
  struct bitreader br = {0};
  int width = 0;
  int64_t sample = 0;
  uint32_t j;
  br.data = input;
  br.size = inend - input;
  for (j = 0; j < nsamples; j++) {
    int i, widthdelta;
    int64_t sampledelta;

    widthdelta = 0;
    while (widthdelta < inwordsize / 2 && !getbit(&br))
      widthdelta++;
    if (widthdelta) /* Widthdelta sign omitted if widthdelta is zero */
      widthdelta = getbit(&br) ? -widthdelta : widthdelta;

    width += widthdelta;
    if (width >= inwordsize)
      width -= inwordsize;
    else if (width < 0)
      width += inwordsize;

    sampledelta = 0;
    if (width) {
      sampledelta = 1;
      for (i = 1; i < width; i++)
        sampledelta = (sampledelta << 1) | getbit(&br);
      sampledelta = getbit(&br) ? -sampledelta : sampledelta;
      /* Trick to represent -(1 << (inwordsize - 1)) */
      if (sampledelta == 1 - bit(inwordsize - 1))
        sampledelta -= getbit(&br);
    }
    if (DEBUG > 1)
      fprintf(stderr, "sampledelta=%" PRId64 "\n", sampledelta);

    sample += sampledelta;
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

void compress(uint8_t *in, uint8_t *inend, struct comm comm, FILE *f) {
  uint8_t *p, *q, *out;
  int32_t outmax, maxframesize;
  int outwordsize = COMPRESSED_WORD_SIZE;

  assert(in < inend - FORM_HEADER);
  if (getint(in + FORM_HEADER, 32) == AIFC && comm.compressiontype != NONE)
    fail("unsupported input AIFC compression format: %4.4s",
         comm.compressiontypestring);

  /* Upper bound for nchannels DWVW encoded sample, in bytes */
  maxframesize =
      comm.nchannels * (((outwordsize + outwordsize / 2 + 1) + 7) / 8);
  assert(comm.nsamples < INT32_MAX / maxframesize);
  /* Enough space for the sample data */
  outmax = comm.nsamples * maxframesize;
  assert(inend - in < INT32_MAX - outmax);
  /* Enough space for the rest of the AIFF data */
  outmax += inend - in;
  if (out = malloc(outmax), !out)
    fail("malloc output failed");

  p = in + FORM_HEADER + 4;
  q = out + FORM_HEADER + 4;
  while (p < inend - CHUNK_HEADER) {
    int32_t ID = getint(p, 32), size = getint(p + 4, 32);
    if (ID == COMM) {
      uint8_t compressinfo[] = "DWVWxDelta With Variable Word Width\x00";
      int compressoff = 18, compressinfosize = sizeof(compressinfo) - 1;
      memmove(q, p, compressoff + CHUNK_HEADER);
      putint(outwordsize, 16, q + CHUNK_HEADER + 6);
      memmove(q + compressoff + CHUNK_HEADER, compressinfo, compressinfosize);
      q[compressoff + 4] =
          0x1f; /* not allowed to put \x1f in string literal?? */
      putint(compressoff + compressinfosize, 32, q + 4);
      q += compressoff + compressinfosize + CHUNK_HEADER;
    } else if (ID == SSND) {
      uint8_t *ssnd = q;
      int i;
      q += CHUNK_HEADER + 8;
      for (i = 0; i < comm.nchannels; i++) {
        q += encodedwvw(p + 16 + i * comm.wordsize / 8, comm.nsamples,
                        comm.wordsize, comm.nchannels, q, out + outmax,
                        outwordsize);
        if (q - out >= outmax)
          fail("bit write overflow");
        q += (q - ssnd) & 1;
      }
      putint(SSND, 32, ssnd);
      putint(q - ssnd - CHUNK_HEADER, 32, ssnd + 4);
      putint(0, 32, ssnd + CHUNK_HEADER);
      putint(0, 32, ssnd + CHUNK_HEADER + 4);
    } else {
      memmove(q, p, nextchunk(size));
      q += nextchunk(size);
    }
    p += nextchunk(size);
  }

  writeform(f, out, q);
}

void decompress(uint8_t *in, uint8_t *inend, struct comm comm, FILE *f) {
  int16_t outwordsize = 8 * ((comm.wordsize + 7) / 8);
  uint8_t *p, *q, *out;
  int32_t outmax, framesize;

  assert(in < inend - FORM_HEADER);
  if (getint(in + FORM_HEADER, 32) != AIFC)
    fail("input file is not AIFC");
  if (comm.compressiontype != DWVW)
    fail("unsupported input AIFC compression format: %4.4s",
         comm.compressiontypestring);

  framesize = comm.nchannels * outwordsize / 8;
  assert(comm.nsamples < INT32_MAX / framesize);
  /* Enough room for decompressed audio data */
  outmax = comm.nsamples * framesize;
  assert(inend - in < INT32_MAX - outmax);
  outmax += inend - in; /* Enough room for rest of AIFF data */
  if (out = malloc(outmax), !out)
    fail("malloc out failed");

  p = in + FORM_HEADER + 4;
  q = out + FORM_HEADER + 4;
  while (p < inend - CHUNK_HEADER) {
    int32_t ID = getint(p, 32), size = getint(p + 4, 32);
    if (ID == COMM) {
      uint8_t compressinfo[] = "NONE\x0enot compressed\x00";
      int compressoff = 18 + CHUNK_HEADER,
          compressinfosize = sizeof(compressinfo) - 1;
      memmove(q, p, compressoff);
      putint(outwordsize, 16, q + CHUNK_HEADER + 6);
      memmove(q + compressoff, compressinfo, compressinfosize);
      putint(18 + compressinfosize, 32, q + 4);
      q += 18 + compressinfosize + CHUNK_HEADER;
    } else if (ID == SSND) {
      int i;
      uint8_t *ssnd = q, *dwvwstart = p + CHUNK_HEADER + 8,
              *dwvwend = p + nextchunk(size);
      q += CHUNK_HEADER + 8;
      for (i = 0; i < comm.nchannels; i++) {
        dwvwstart += decodedwvw(dwvwstart, dwvwend, comm.nsamples,
                                comm.wordsize, comm.nchannels,
                                ssnd + 16 + i * (outwordsize / 8), outwordsize);
        if (dwvwstart >= dwvwend)
          fail("read overflow");
        dwvwstart += (dwvwstart - p) & 1;
        q += comm.nsamples * (outwordsize / 8);
      }
      putint(SSND, 32, ssnd);
      putint(q - ssnd - CHUNK_HEADER, 32, ssnd + 4);
      putint(0, 32, ssnd + CHUNK_HEADER);
      putint(0, 32, ssnd + CHUNK_HEADER + 4);
    } else {
      memmove(q, p, nextchunk(size));
      q += nextchunk(size);
    }
    p += nextchunk(size);
  }

  writeform(f, out, q);
}

int main(int argc, char **argv) {
  uint8_t *in, *inend;
  int32_t filetype, insize;
  struct comm comm;
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
  filetype = getint(in + FORM_HEADER, 32);
  if (filetype != AIFF && filetype != AIFC)
    fail("invalid file type: %4.4s", in + FORM_HEADER);
  if (findchunk(0, in + 12, inend) != inend) /* validate chunk sizes */
    fail("zero chunk ID found");
  comm = loadcomm(in, inend, filetype);

  if (!strcmp(argv[1], "compress"))
    compress(in, inend, comm, fout);
  else
    decompress(in, inend, comm, fout);
  if (fclose(fout))
    fail("close failed: %s", argv[3]);
  return 0;
}
