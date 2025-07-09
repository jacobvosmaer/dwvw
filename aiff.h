#ifndef AIFF_H
#define AIFF_H

#include <stdint.h>

struct chunk {
  int32_t ID, size;
  unsigned char *data;
};

struct aiff {
  struct chunk *chunk;
  int nchunk;
  int32_t form;
};

char *aiffload(struct aiff *aiff, unsigned char *data, int size);
struct chunk *aiffchunk(struct aiff *aiff, int32_t ID);
void aifffree(struct aiff *aiff);

#endif
