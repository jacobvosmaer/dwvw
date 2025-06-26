
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

int64_t nextbit(struct bitreader *br) {
  if (!(br->bit % 8)) {
    br->c = fgetc(br->f);
    br->bit = 0;
  }
  if (br->c < 0)
    return br->c;
  else
    return (br->c & (1 << (7 - br->bit++))) > 0;
}

int64_t Nextbit(struct bitreader *br) {
  int64_t b = nextbit(br);
  if (b < 0)
    fail("unexpected EOF: %d", b);
  return b;
}

int main(int argc, char **argv) {
  int64_t inwordsize, outwordsize, nchannels, nsamples;
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
br.f=stdin;
  while (nchannels--) {
    int64_t deltawidth = 0, sample = 0;
    while (nsamples--) {
      int64_t i, b, dwm = 0, delta = 0, outsample;
      while (b = Nextbit(&br), !b && dwm < inwordsize / 2)
        dwm++;
      if (dwm) {
        dwm *= Nextbit(&br) ? -1 : 1;
        deltawidth += dwm;
        delta = 1;
        for (i = 0; i < deltawidth; i++)
          delta = (delta << 1) | Nextbit(&br);
        delta *= Nextbit(&br) ? -1 : 1;
        if (delta == 1 - (1 << (inwordsize - 1)))
          delta -= Nextbit(&br);
      }
      sample += delta;
      outsample = sample << (outwordsize - inwordsize);
      for (i = outwordsize - 8; i >= 0; i -= 8)
        putchar(outsample >> i);
    }
  }
}
