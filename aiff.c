
#include "aiff.h"
#include "fail.h"
#include "int.h"
#include <stdlib.h>
#include <string.h>

char *aiffload(struct aiff *aiff, unsigned char *data, int size) {
  unsigned char *p = data, *end;
  char *err = 0;
  int32_t formtype;
  int formsize;
  if (size < 12) {
    err = "negative aiff size";
    goto out;
  }
  if (readint(data, 32) != 'FORM') {
    err = "missing FORM";
    goto out;
  }
  if (formtype = readint(data + 8, 32),
      formtype != 'AIFC' && formtype != 'AIFF') {
    err = "unknown FORM type";
    goto out;
  }
  formsize = readint(data + 4, 32);
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
    ch->ID = readint(p, 32);
    ch->size = readint(p + 4, 32);
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

char *aiffcomm(struct aiff *aiff, struct comm *comm) {
  struct chunk *ch = aiffchunk(aiff, 'COMM');
  unsigned char *p;
  int namesize;
  if (!ch)
    return "COMM not found or duplicated";
  p = ch->data;
  comm->ckID = ch->ID;
  comm->ckDataSize = ch->size;
  comm->numChannels = readint(p, 16);
  p += 2;
  if (comm->numChannels < 1)
    return "must have at least 1 channel";
  comm->numSampleFrames = readuint(p, 32);
  p += 4;
  comm->sampleSize = readint(p, 16);
  p += 2;
  if (comm->sampleSize < 1 || comm->sampleSize > 32)
    return "invalid sample size";
  memmove(comm->sampleRate, p, 10);
  p += 10;
  comm->compressionType = readint(p, 32);
  p += 4;
  comm->compressionName = (char *)p;
  namesize = readuint(p, 8);
  if (ch->size - 24 + (namesize & 1) != namesize)
    return "invalid compression name string";
  return 0;
}

void aifffree(struct aiff *aiff) { free(aiff->chunk); }
