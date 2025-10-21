#include "core.h"
#include "../generated/glyphs.h"

PixelBuf next_frame(Arena *arena)
{
	return glyphs[GLYPH_ID_BATTERY_FULL];
	static u32 frame_count = 0;
	frame_count++;
	u32 w = 100;
	u32 h = 10;
	Pixel *data = arena_push_arr(arena, Pixel, w * h);
	PixelBuf pixels = {.w = w, .h = h, .data = data};
	u32 is_odd = frame_count % 2;
	u32 is_even = 1 - is_odd;
	for (u32 i = 0; i < w * h; i++) {
		pixels.data[i].r = is_even * 255;
		pixels.data[i].g = is_odd * 255;
		pixels.data[i].b = 0;
		pixels.data[i].a = 255;
	}
	return pixels;
}

