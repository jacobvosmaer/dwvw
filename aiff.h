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

typedef char extended[10];

struct comm {
  int32_t ckID; /* 'COMM' */
  int32_t ckDataSize;
  int16_t numChannels;      /* # audio channels */
  uint32_t numSampleFrames; /* # sample frames = samples/channel */
  int16_t sampleSize;       /* # bits/sample */
  extended sampleRate;      /* sample_frames/sec */
  int32_t compressionType;  /* compression type ID code */
  char *compressionName;    /* human-readable compression type name */
};

char *aiffload(struct aiff *aiff, unsigned char *data, int size);
struct chunk *aiffchunk(struct aiff *aiff, int32_t ID);
char *aiffcomm(struct aiff *aiff, struct comm *comm);
void aifffree(struct aiff *aiff);

#endif
