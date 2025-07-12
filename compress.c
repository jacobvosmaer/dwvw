
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

unsigned char *loadform(FILE *f, int32_t *size) {
  unsigned char buf[8], *p;
  int32_t formsize;
  if (!fread(buf, sizeof(buf), 1, f))
    fail("read AIFF header: short read");
  if (readint(buf, 32) != 'FORM')
    fail("missing FORM");
  formsize = readint(buf + 4, 32);
  if (formsize < 4 || formsize > INT32_MAX - 8)
    fail("invalid FORM size: %d", formsize);
  *size = formsize + 8;
  if (p = malloc(*size), !p)
    fail("malloc input failed");
  memmove(p, buf, sizeof(buf));
  if (!fread(p + sizeof(buf), formsize, 1, f))
    fail("short read");
  return p;
}

int main(void) {
  unsigned char *in, *inend, *comm, *ssnd, *out, *p, *q;
  int32_t filetype, insize, commsize;
  in = loadform(stdin, &insize);
  if (insize > INT32_MAX / 2)
    fail("input file too large");
  inend = in + insize;
  filetype = readint(in + 8, 32);
  if (filetype != 'AIFF' && filetype != 'AIFC')
    fail("invalid file type: %4.4s", in + 8);
  findchunk(0, in + 12, inend); /* validate chunk sizes */
  if (comm = finduniquechunk('COMM', in + 12, inend), comm == inend)
    fail("cannot find COMM chunk");
  if (commsize = readint(comm + 4, 32), commsize < 22)
    fail("COMM chunk too small: %d", commsize);
  if (filetype == 'AIFC' && readint(comm + 8 + 18, 32) != 'NONE')
    fail("unsupported input audio format: %4.4s", comm + 8 + 18);
  if (ssnd = finduniquechunk('SSND', in + 12, inend), ssnd == inend)
    fail("cannot find SSND chunk");
  if (out = malloc(2 * insize), !out)
    fail("malloc output failed");
  p = in + 12;
  q = out + 12;
  while (p < inend - 8) {
    int32_t ID = readint(p, 32), size = readint(p + 4, 32);
    if (ID == 'COMM') {
      int compressoff = 18 + 8;
      memmove(q, p, compressoff);
      memmove(q + compressoff, "DWVWxDelta With Variable Word Width\x00", 36);
      q[compressoff + 4] =
          0x1f; /* not allowed to put \x1f in string literal?? */
      putbe(18 + 36, 32, q + 4);
      q += 18 + 36 + 8;
    } else if (ID == 'SSND') {
    } else {
      memmove(q, p, size + 8);
      q += size + 8;
    }
    p += size + 8;
  }
  putbe('FORM', 32, out);
  putbe(q - out - 8, 32, out + 4);
  putbe('AIFC', 32, out + 8);
  fwrite(out, q - out, 1, stdout);
}
