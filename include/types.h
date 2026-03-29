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
#define true  1
#define false 0
typedef int bool;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
