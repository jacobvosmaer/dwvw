
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
  int bit, c;
};

word nextbit(struct bitreader *br) {
  if (!(br->bit % 8)) {
    br->c = fgetc(br->f);
    br->bit = 0;
  }
  if (br->c < 0)
    return br->c;
  else
    return (br->c & (1 << (7 - br->bit++))) > 0;
}

word Nextbit(struct bitreader *br) {
  word b = nextbit(br);
  if (b < 0)
    fail("unexpected EOF: %d", b);
  return b;
}

word bit(word shift) { return (word)1 << shift; }

int main(int argc, char **argv) {
  word inwordsize, outwordsize, nchannels, nsamples;
  struct bitreader br = {0};
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
  br.f = stdin;
  while (nchannels--) {
    word deltawidth = 0, sample = 0;
    while (nsamples--) {
      word i, dwm = 0, outsample;
      while (dwm < inwordsize / 2 && !Nextbit(&br))
        dwm++;
      if (dwm) {
        dwm *= Nextbit(&br) ? -1 : 1;
        deltawidth += dwm;
        if (deltawidth < 0)
          deltawidth += inwordsize;
        else if (deltawidth >= inwordsize)
          deltawidth -= inwordsize;
        assert(deltawidth >= 0 && deltawidth <= inwordsize);
      }
      if (deltawidth) {
        word delta = 1;
        for (i = 1; i < deltawidth; i++)
          delta = (delta << 1) | Nextbit(&br);
        delta *= Nextbit(&br) ? -1 : 1;
        if (delta == 1 - bit(inwordsize - 1))
          delta -= Nextbit(&br);
        sample += delta;
        assert(sample >= -bit(inwordsize - 1) && sample < bit(inwordsize - 1));
      }
      outsample = sample << (outwordsize - inwordsize);
      if (outsample < 0)
        outsample += bit(outwordsize);
      for (i = outwordsize - 8; i >= 0; i -= 8)
        putchar(outsample >> i);
    }
  }
  return 0;
}
