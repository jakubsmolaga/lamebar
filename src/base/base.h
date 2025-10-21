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

#define align_up(x, align) (((x) + (align) - 1) & ~((align) - 1))

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

typedef struct { u8 b, g, r, a; } Pixel;

typedef struct PixelBuf PixelBuf;
struct PixelBuf {
	Pixel *data;
	u32 w, h;
};

static void
pixelbuf_copy(PixelBuf dst, const PixelBuf src, u32 x, u32 y, u32 scale)
{
	u32 dst_stride = dst.w;
	u32 src_stride = src.w;
	u32 block_w    = src.w * scale;
	u32 block_h    = src.h * scale;

	for (u32 sy = 0; sy < src.h; ++sy) {
		const Pixel *src_row = src.data + sy * src_stride;
		Pixel *dst_row       = dst.data + (y + sy * scale) * dst_stride + x;

		// Expand one source row horizontally into the destination
		for (u32 sx = 0; sx < src.w; ++sx) {
			Pixel c = src_row[sx];
			Pixel *p = dst_row + sx * scale;
			for (u32 i = 0; i < scale; ++i)
				p[i] = c;
		}

		// Now duplicate that expanded line vertically
		Pixel *dst_line = dst_row;
		for (u32 dy = 1; dy < scale; ++dy) {
			memcpy(dst_line + dy * dst_stride, dst_line,
				block_w * sizeof(Pixel));
		}
	}
}
