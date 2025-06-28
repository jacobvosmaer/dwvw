
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define assert(x)                                                              \
  if (!(x))                                                                    \
  __builtin_trap()

typedef int64_t word;

void fail(char *fmt, ...) {
  va_list ap;
  fputs("error: ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

struct bitreader {
  FILE *f;
  int bit, c, nbytes;
};

word nextbit(struct bitreader *br) {
  if (!(br->bit % 8) && br->c >= 0) {
    br->c = fgetc(br->f);
    br->bit = 0;
    if (br->c >= 0)
      br->nbytes++;
  }
  if (br->c < 0) {
    return br->c;
  } else {
    return (br->c & (1 << (7 - br->bit++))) > 0;
  }
}

word Nextbit(struct bitreader *br) {
  word b = nextbit(br);
  if (b < 0)
    fail("unexpected EOF: %d", b);
  return b;
}

word bit(word shift) { return (word)1 << shift; }

struct decoder {
  struct bitreader br;
  word deltawidth, sample, wordsize;
  int dwmstats[32 / 2 + 1];
  int dwstats[32];
};

void decoderinit(struct decoder *d, word wordsize, FILE *f) {
  struct decoder empty = {0};
  *d = empty;
  d->br.f = f;
  d->wordsize = wordsize;
}

char *decodernext(struct decoder *d, word *sample) {
  word dwm = 0; /* "delta width modifier" */
  /* dwm is encoded in unary as a string of zeroes followed by a sign bit */
  while (dwm < d->wordsize / 2 && !Nextbit(&d->br))
    dwm++;
  d->dwmstats[dwm]++;
  if (dwm) { /* delta width is changing */
    dwm *= Nextbit(&d->br) ? -1 : 1;
    d->deltawidth += dwm;
    /* Deltawidth wraps around. This allows the encoding to minimize the
     * absolute value of dwm, which matters because dwm is encoded in unary. */
    if (d->deltawidth < 0)
      d->deltawidth += d->wordsize;
    else if (d->deltawidth >= d->wordsize)
      d->deltawidth -= d->wordsize;
    if (!(d->deltawidth >= 0 && d->deltawidth <= d->wordsize))
      return "delta width out of range";
  }
  d->dwstats[d->deltawidth]++;
  if (d->deltawidth) { /* non-zero delta: sample is changing */
    word i, delta;
    /* Start iteration from 1 because the leading 1 of delta is implied */
    for (i = 1, delta = 1; i < d->deltawidth; i++)
      delta = (delta << 1) | Nextbit(&d->br);
    delta *= Nextbit(&d->br) ? -1 : 1;
    /* The lowest possible value for delta at this point is -(1 << (wordsize
     * -1)). So if wordsize is 8, the lowest possible value is -127. In 2's
     * complement we must also be able to represent -128. To account for this
     * DWVW adds an extra bit. To save space this bit is only present when
     * needed. So -126 is 1111110 1 (no extra bit), -127 is 1111111 1 0 and -128
     * is 1111111 1 1. */
    if (delta == 1 - bit(d->wordsize - 1))
      delta -= Nextbit(&d->br);
    d->sample += delta;
    if (!(d->sample >= -bit(d->wordsize - 1) &&
          d->sample < bit(d->wordsize - 1)))
      return "sample out of range";
  }
  *sample = d->sample;
  return 0;
}

char *decoderclose(struct decoder *d) {
  word b = 0;
  while (d->br.nbytes & 1 && b >= 0)
    b = nextbit(&d->br);
  return b >= 0 ? 0 : "read error";
}

void decoderprintstats(struct decoder *d, FILE *f) {
  int i;
  fputs("dwm stats:\n", f);
  for (i = 0; i < d->wordsize / 2 + 1; i++)
    fprintf(f, "%2d %d\n", i, d->dwmstats[i]);
  fputs("deltawidth stats:\n", f);
  for (i = 0; i < d->wordsize; i++)
    fprintf(f, "%2d %d\n", i, d->dwstats[i]);
}

void putbe(word x, word wordsize, FILE *f) {
  word shift;
  if (x < 0)
    x += bit(wordsize);
  for (shift = wordsize - 8; shift >= 0; shift -= 8)
    fputc(x >> shift, f);
}

int main(int argc, char **argv) {
  word inwordsize, outwordsize, nchannels, nsamples;
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
  while (nchannels--) {
    char *err;
    struct decoder d;
    decoderinit(&d, inwordsize, stdin);
    while (nsamples--) {
      word sample;
      if (err = decodernext(&d, &sample), err)
        fail("decoder: %s", err);
      putbe(sample << (outwordsize - inwordsize), outwordsize, stdout);
    }
    if (err = decoderclose(&d), err)
      fail("decoderclose: %s", err);
    decoderprintstats(&d, stderr);
  }
  return 0;
}
