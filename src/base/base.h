#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef static_assert
#define static_assert(cond, msg) _Static_assert(cond, msg)
#endif

#ifndef alignof
#define alignof(t) _Alignof(t)
#endif

typedef uint8_t      u8;
typedef uint16_t     u16;
typedef uint32_t     u32;
typedef uint64_t     u64;
typedef int8_t       i8;
typedef int16_t      i16;
typedef int32_t      i32;
typedef int64_t      i64;

#define KB(x) (x * 1024LL)
#define MB(x) (x * 1024LL * 1024LL)
#define GB(x) (x * 1024LL * 1024LL * 1024LL)

static u64 align_up(u64 x, u64 align) { return (x + align - 1) & ~(align - 1); }
