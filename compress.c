
#include "fail.h"
#include "int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  unsigned char buf[12], *in;
  int32_t filetype, formsize;
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
  if (readsint(buf, 32) != 'FORM')
    fail("missing FORM");
  formsize = readsint(buf + 4, 32);
  if (formsize < 0)
    fail("invalid FORM size: %d", formsize);
  filetype = readsint(buf + 8, 32);
  if (filetype != 'AIFF' && filetype != 'AIFC')
    fail("invalid file type: %4.4s", buf + 8);
  in = malloc(formsize + 8);
  if (!in)
    fail("malloc failed");
  memmove(in, buf, sizeof(buf));
  if (fread(in, formsize - 4, 1, f) != 1)
    fail("short read from %s", argv[1]);
}
