#pragma once
#include <intrin.h>

#ifdef __cplusplus
extern "C" {
#endif
static __inline unsigned __int64 __rdtsc(void) { return __builtin_readcyclecounter(); }
static __inline void __cpuid(int info[4], int leaf) {
    (void)leaf;
    info[0] = info[1] = info[2] = info[3] = 0;
}
#ifdef __cplusplus
}
#endif
