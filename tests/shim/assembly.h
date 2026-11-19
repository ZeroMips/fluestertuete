// Portable replacement for Helix' assembly.h, for host builds only.
#ifndef _ASSEMBLY_H
#define _ASSEMBLY_H
typedef long long Word64;
static inline int MULSHIFT32(int x, int y) { return (int)(((long long)x * y) >> 32); }
static inline int FASTABS(int x) { return x < 0 ? -x : x; }
static inline Word64 MADD64(Word64 sum, int x, int y) { return sum + (long long)x * y; }
static inline Word64 SAR64(Word64 x, int n) { return x >> n; }
static inline Word64 SHL64(Word64 x, int n) { return x << n; }
static inline int CLZ(int x) { return x ? __builtin_clz((unsigned)x) : 32; }
#endif
