
#include "aiff.h"
#include "fail.h"
#include "int.h"
#include <stdlib.h>

char *aiffload(struct aiff *aiff, unsigned char *data, int size) {
  unsigned char *p = data, *end;
  char *err = 0;
  int32_t formtype;
  int formsize;
  if (size < 12) {
    err = "negative aiff size";
    goto out;
  }
  if (readsint(data, 32) != 'FORM') {
    err = "missing FORM";
    goto out;
  }
  if (formtype = readsint(data + 8, 32),
      formtype != 'AIFC' && formtype != 'AIFF') {
    err = "unknown FORM type";
    goto out;
  }
  formsize = readsint(data + 4, 32);
  if (formsize != size - 8) {
    err = "invalid FORM size";
    goto out;
  }
  for (p = data + 12, end = data + formsize; p < end;) {
    struct chunk *ch;
    aiff->chunk =
        realloc(aiff->chunk, (++(aiff->nchunk)) * sizeof(*aiff->chunk));
    assert(aiff->chunk);
    ch = aiff->chunk + aiff->nchunk - 1;
    ch->ID = readsint(p, 32);
    ch->size = readsint(p + 4, 32);
    if (ch->size < 0) {
      err = "negative chunk size";
      goto out;
    }
    ch->data = p + 8;
    p += 8 + ch->size;
  }
out:
  if (err)
    free(aiff->chunk);
  return err;
}

struct chunk *aiffchunk(struct aiff *aiff, int32_t ID) {
  struct chunk *ch, *out = 0;
  for (ch = aiff->chunk; ch < aiff->chunk + aiff->nchunk; ch++) {
    if (ch->ID == ID) {
      if (out)
        return 0;
      else
        out = ch;
    }
  }
  return out;
}

void aifffree(struct aiff *aiff) { free(aiff->chunk); }
