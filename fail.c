
#include "fail.h"
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef DEBUG
#define DEBUG 0
#endif

void fail(char *fmt, ...) {
  va_list ap;
  if (DEBUG)
    assert(0);
  fputs("error: ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

void failerrno(char *fmt, ...) {
  va_list ap;
  if (DEBUG)
    assert(0);
  fputs("error: ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  perror("");
  exit(1);
}
