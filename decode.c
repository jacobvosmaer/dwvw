
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
  if (!(br->bit % 8)) {
    br->c = fgetc(br->f);
    br->bit = 0;
    br->nbytes++;
  }
  if (br->c < 0) {
    br->nbytes--;
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
};

void decoderinit(struct decoder *d, word wordsize, FILE *f) {
  struct decoder empty = {0};
  *d = empty;
  d->br.f = f;
  d->wordsize = wordsize;
}

int decodernext(struct decoder *d, word *sample) {
  word dwm = 0;
  while (dwm < d->wordsize / 2 && !Nextbit(&d->br))
    dwm++;
  if (dwm) {
    dwm *= Nextbit(&d->br) ? -1 : 1;
    d->deltawidth += dwm;
    if (d->deltawidth < 0)
      d->deltawidth += d->wordsize;
    else if (d->deltawidth >= d->wordsize)
      d->deltawidth -= d->wordsize;
    assert(d->deltawidth >= 0 && d->deltawidth <= d->wordsize);
  }
  if (d->deltawidth) {
    word i, delta;
    for (i = 1, delta = 1; i < d->deltawidth; i++)
      delta = (delta << 1) | Nextbit(&d->br);
    delta *= Nextbit(&d->br) ? -1 : 1;
    if (delta == 1 - bit(d->wordsize - 1))
      delta -= Nextbit(&d->br);
    d->sample += delta;
    assert(d->sample >= -bit(d->wordsize - 1) &&
           d->sample < bit(d->wordsize - 1));
  }
*sample=d->sample;
  return 1;
}

int decoderclose(struct decoder *d) {
  word b = 0;
  while (d->br.nbytes & 1 && b >= 0)
    b = nextbit(&d->br);
  return b >= 0;
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
    struct decoder d;
    decoderinit(&d, inwordsize, stdin);
    while (nsamples--) {
      word sample, outsample, i;
      assert(decodernext(&d, &sample));
      outsample = sample << (outwordsize - inwordsize);
      if (outsample < 0)
        outsample += bit(outwordsize);
      for (i = outwordsize - 8; i >= 0; i -= 8)
        putchar(outsample >> i);
    }
    assert(decoderclose(&d));
  }
  return 0;
}
