/*
Decoder for the Typhoon DWVW audio compression format.

DWVW was invented 1991 by Magnus Lidström and is copyright 1993 by NuEdge
Development.

This decoder is based on documentation in "fmt_typh.rtf" published on
ftp.t0.or.at.
*/

#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>
#include <stdio.h>

typedef int64_t word;
#define bit(shift) ((word)1 << (shift))

struct bitreader {
  FILE *f;
  int bit, c, nbytes;
};

struct decoder {
  struct bitreader br;
  word deltawidth, sample, wordsize;
  int readerror;
  int dwmstats[32 / 2 + 1];
  int dwstats[32];
};

void decoderinit(struct decoder *d, word wordsize, FILE *f);
char *decodernext(struct decoder *d, word *sample);
char *decoderclose(struct decoder *d);
void decoderprintstats(struct decoder *d, FILE *f);

#endif
