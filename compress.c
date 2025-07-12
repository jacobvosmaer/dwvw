
#include "fail.h"
#include "int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char *findchunk(int32_t ID, unsigned char *start, unsigned char *end) {
  unsigned char *p = start;
  while (p < end - 8) {
    int32_t size = readint(p + 4, 32);
    if (size < 0 || size > end - (p + 8))
      fail("chunk %4.4s: invalid size %d", p, size);
    if (readint(p, 32) == ID)
      return p;
    p += size + 8;
  }
  return end;
}

unsigned char *finduniquechunk(int32_t ID, unsigned char *start,
                               unsigned char *end) {
  unsigned char *chunk = findchunk(ID, start, end), *chunk2;
  if (chunk < end)
    if (chunk2 = findchunk(ID, chunk + 8 + readint(chunk + 4, 32), end),
        chunk2 < end)
      fail("duplicate %4.4s chunk", chunk);
  return chunk;
}

int main(void) {
  unsigned char buf[8], *in, *inend, *comm, *ssnd, *out;
  int32_t filetype, formsize, insize;
  if (!fread(buf, sizeof(buf), 1, stdin))
    fail("read AIFF header: short read");
  if (readint(buf, 32) != 'FORM')
    fail("missing FORM");
  formsize = readint(buf + 4, 32);
  if (formsize < 4 || formsize > INT32_MAX - 8)
    fail("invalid FORM size: %d", formsize);
  insize = formsize + 8;
  if (insize > INT32_MAX / 2)
    fail("input file too large");
  if (in = malloc(insize), !in)
    fail("malloc input failed");
  memmove(in, buf, sizeof(buf));
  if (!fread(in + sizeof(buf), formsize, 1, stdin))
    fail("short read");
  inend = in + insize;
  filetype = readint(in + 8, 32);
  if (filetype != 'AIFF' && filetype != 'AIFC')
    fail("invalid file type: %4.4s", in + 8);
  if (comm = finduniquechunk('COMM', in + 12, inend), comm == inend)
    fail("cannot find COMM chunk");
  if (filetype == 'AIFC' && readint(comm + 8 + 18, 32) != 'NONE')
    fail("unsupported input audio format: %4.4s", comm + 8 + 18);
  if (ssnd = finduniquechunk('SSND', in + 12, inend), ssnd == inend)
    fail("cannot find SSND chunk");
  if (out = malloc(2 * insize), !out)
    fail("malloc output failed");
}
