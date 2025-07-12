
#include "fail.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  unsigned char buf[12];
  FILE *f;
  if (argc != 3) {
    fputs("Usage: compress IN OUT\n", stderr);
    exit(1);
  }
  f = fopen(argv[1], "r");
  if (!f)
    failerrno("open %s", argv[1]);
  if (fread(buf, 1, sizeof(buf), f) != sizeof(buf))
    fail("read AIFF header: short read");
}
