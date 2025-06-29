#include "decoder.h"

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

word Nextbit(struct decoder *d) {
  word b = nextbit(&d->br);
  d->readerror |= b < 0;
  return d->readerror ? 0 : b;
}

void decoderinit(struct decoder *d, word wordsize, FILE *f) {
  struct decoder empty = {0};
  *d = empty;
  d->br.f = f;
  d->wordsize = wordsize;
}

char *Decodernext(struct decoder *d, word *sample) {
  word dwm = 0; /* "delta width modifier" */
  /* dwm is encoded in unary as a string of zeroes followed by a sign bit */
  while (dwm < d->wordsize / 2 && !Nextbit(d))
    dwm++;
  d->dwmstats[dwm]++;
  if (dwm) { /* delta width is changing */
    dwm *= Nextbit(d) ? -1 : 1;
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
      delta = (delta << 1) | Nextbit(d);
    delta *= Nextbit(d) ? -1 : 1;
    /* The lowest possible value for delta at this point is -(1 << (wordsize
     * -1)). So if wordsize is 8, the lowest possible value is -127. In 2's
     * complement we must also be able to represent -128. To account for this
     * DWVW adds an extra bit. To save space this bit is only present when
     * needed. So -126 is 1111110 1 (no extra bit), -127 is 1111111 1 0 and -128
     * is 1111111 1 1. */
    if (delta == 1 - bit(d->wordsize - 1))
      delta -= Nextbit(d);
    d->sample += delta;
    if (!(d->sample >= -bit(d->wordsize - 1) &&
          d->sample < bit(d->wordsize - 1)))
      return "sample out of range";
  }
  *sample = d->sample;
  return 0;
}

char *decodernext(struct decoder *d, word *sample) {
  char *err = Decodernext(d, sample);
  return d->readerror ? "read error" : err;
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
