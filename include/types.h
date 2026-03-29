#pragma once
// BlueyOS Type Definitions
// "Mum, what's a uint32_t?" - Bingo
// "It's a big number, sweetheart." - Chilli
// Episode ref: "Baby Race" - everybody grows at their own pace, even type sizes

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint32_t size_t;
typedef uint32_t uintptr_t;
typedef int      ssize_t;

#define NULL  ((void*)0)

// bool / true / false
// C23 (GCC 15+ default) promotes bool to a keyword; guard the typedef so we
// don't collide. Using -std=gnu11 in CFLAGS avoids the issue for kernel
// builds, but the guard ensures correctness if this header is ever included
// in a different translation unit or a future standard revision.
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  // C23+: bool, true, false are keywords — nothing to define
#elif defined(__cplusplus)
  // C++: same deal
#else
  #define true  1
  #define false 0
  typedef int bool;
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
