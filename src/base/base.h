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

static void
pixelbuf_blend(PixelBuf dst, const PixelBuf src, u32 x, u32 y)
{
	for (u32 row = 0; row < src.h; row++) {
		for (u32 col = 0; col < src.w; col++) {
			Pixel sp = src.data[row * src.w + col];
			Pixel *dp = &dst.data[(y + row) * dst.w + (x + col)];
			u8 sa = sp.a;
			u8 da = dp->a;
			u32 inv_sa = 255 - sa;
			dp->r = (sp.r * sa + dp->r * inv_sa) / 255;
			dp->g = (sp.g * sa + dp->g * inv_sa) / 255;
			dp->b = (sp.b * sa + dp->b * inv_sa) / 255;
			dp->a = sa + ((da * inv_sa) / 255);
		}
	}
}
