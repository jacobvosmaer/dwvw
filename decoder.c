/*
Decoder for the Typhoon DWVW audio compression format.

DWVW was invented 1991 by Magnus Lidström and is copyright 1993 by NuEdge
Development.

This decoder is based on documentation in "fmt_typh.rtf" published on
ftp.t0.or.at.
*/

#include "decoder.h"

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

int Decodernext(struct decoder *d, word *sample) {
  word dwm = 0; /* "delta width modifier" */
  /* Dwm is encoded in unary as a string of zeroes followed by a stop bit and a
   * sign bit. */
  while (dwm < d->wordsize / 2 && !nextbit(&d->br))
    dwm++;
  d->dwmstats[dwm]++;
  if (dwm) { /* deltawidth is changing */
    dwm *= nextbit(&d->br) ? -1 : 1;
    d->deltawidth += dwm;
    /* Deltawidth wraps around. This allows the encoding to minimize the
     * absolute value of dwm, which matters because dwm is encoded in unary. */
    d->deltawidth -= d->wordsize;
    while (d->deltawidth < 0)
      d->deltawidth += d->wordsize;
  }
  d->dwstats[d->deltawidth]++;
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
    d->sample += delta;
    if (!(d->sample >= -bit(d->wordsize - 1) &&
          d->sample < bit(d->wordsize - 1)))
      return -1;
  }
  *sample = d->sample;
  return 0;
}

int decodernext(struct decoder *d, word *sample) {
  int err = Decodernext(d, sample);
  if (overflow(&d->br))
    err--;
  return err;
}
int decoderpos(struct decoder *d) { return d->br.bit / 8 + 1; }

void decoderprintstats(struct decoder *d, FILE *f) {
  int i;
  fputs("dwm stats:\n", f);
  for (i = 0; i < d->wordsize / 2 + 1; i++)
    fprintf(f, "%2d %d\n", i, d->dwmstats[i]);
  fputs("deltawidth stats:\n", f);
  for (i = 0; i < d->wordsize; i++)
    fprintf(f, "%2d %d\n", i, d->dwstats[i]);
}
