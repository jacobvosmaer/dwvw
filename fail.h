#ifndef FAIL_H
#define FAIL_H
void fail(char *fmt, ...);
#define assert(x)                                                              \
  if (!(x))                                                                    \
  __builtin_trap()

#endif
