
#include "fail.h"
#include "int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  unsigned char buf[8], *in;
  int32_t filetype, formsize;
  FILE *f;
  if (argc != 3) {
    fputs("Usage: compress IN OUT\n", stderr);
    exit(1);
  }
  f = fopen(argv[1], "r");
  if (!f)
    failerrno("open %s", argv[1]);
  if (!fread(buf, sizeof(buf), 1, f))
    fail("read AIFF header: short read");
  if (readsint(buf, 32) != 'FORM')
    fail("missing FORM");
  formsize = readsint(buf + 4, 32);
  if (formsize < 4 || formsize > INT32_MAX - 8)
    fail("invalid FORM size: %d", formsize);
  in = malloc(formsize + 8);
  if (!in)
    fail("malloc failed");
  memmove(in, buf, sizeof(buf));
  if (!fread(in + sizeof(buf), formsize, 1, f))
    fail("short read from %s", argv[1]);
  filetype = readsint(in + 8, 32);
  if (filetype != 'AIFF' && filetype != 'AIFC')
    fail("invalid file type: %4.4s", in + 8);
}
