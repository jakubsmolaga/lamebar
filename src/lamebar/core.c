#include "core.h"
#include "../generated/glyphs.h"
#include <time.h>

typedef struct { u32 year, month, day, hour, minute; } TimeAndDate;

static TimeAndDate
get_time_and_date(void)
{
	TimeAndDate time_and_date = {0};
	time_t current_time = time(NULL);
	struct tm *time = localtime(&current_time);
	time_and_date.year = time->tm_year + 1900;
	time_and_date.month = time->tm_mon + 1;
	time_and_date.day = time->tm_mday;
	time_and_date.hour = time->tm_hour;
	time_and_date.minute = time->tm_min;
	return time_and_date;
}

static GlyphId
digit_to_glyph_id(u32 digit)
{
	// NOTE: this statement is probably gonna be optimzed into
	// just an addition, but writing it out as a switch statement
	// makes the intention more clear
	switch (digit) {
	case 0: return GLYPH_ID_DIGIT_0;
	case 1: return GLYPH_ID_DIGIT_1;
	case 2: return GLYPH_ID_DIGIT_2;
	case 3: return GLYPH_ID_DIGIT_3;
	case 4: return GLYPH_ID_DIGIT_4;
	case 5: return GLYPH_ID_DIGIT_5;
	case 6: return GLYPH_ID_DIGIT_6;
	case 7: return GLYPH_ID_DIGIT_7;
	case 8: return GLYPH_ID_DIGIT_8;
	case 9: return GLYPH_ID_DIGIT_9;
	}
	return 0;
}

static u32
get_glyphs(TimeAndDate td, GlyphId *out)
{
	u32 len = 0;
	out[len++] = digit_to_glyph_id((td.year / 1000) % 10);
	out[len++] = digit_to_glyph_id((td.year / 100 ) % 10);
	out[len++] = digit_to_glyph_id((td.year / 10  ) % 10);
	out[len++] = digit_to_glyph_id((td.year / 1   ) % 10);
	out[len++] = GLYPH_ID_DOT;
	out[len++] = digit_to_glyph_id((td.month / 10) % 10);
	out[len++] = digit_to_glyph_id((td.month / 1 ) % 10);
	out[len++] = GLYPH_ID_DOT;
	out[len++] = digit_to_glyph_id((td.day / 10) % 10);
	out[len++] = digit_to_glyph_id((td.day / 1 ) % 10);
	out[len++] = GLYPH_ID_SPACE;
	out[len++] = GLYPH_ID_PIPE;
	out[len++] = GLYPH_ID_SPACE;
	out[len++] = digit_to_glyph_id((td.hour / 10) % 10);
	out[len++] = digit_to_glyph_id((td.hour / 1 ) % 10);
	out[len++] = GLYPH_ID_COLON;
	out[len++] = digit_to_glyph_id((td.minute / 10) % 10);
	out[len++] = digit_to_glyph_id((td.minute / 1 ) % 10);
	return len;
}

static void
measure_glyphs(GlyphId *glyph_ids, u32 len, u32 *out_w, u32 *out_h)
{
	u32 w = 0;
	u32 h = 0;
	for (u32 i = 0; i < len; i++) {
		GlyphId id = glyph_ids[i];
		w += glyphs[id].w;
		w += 1; // TODO: maybe this should be a configurable value
		if (glyphs[id].h > h) h = glyphs[id].h;
	}
	*out_w = w;
	*out_h = h;
}

PixelBuf next_frame(Arena *arena)
{
	TimeAndDate td = get_time_and_date();
	static GlyphId glyph_ids[1024];
	u32 glyph_ids_len = get_glyphs(td, glyph_ids);
	u32 base_w, base_h;
	measure_glyphs(glyph_ids, glyph_ids_len, &base_w, &base_h);
	u32 padding = 2;
	u32 w = base_w + padding * 2;
	u32 h = base_h + padding * 2;
	Pixel *data = arena_push_arr(arena, Pixel, w * h);
	PixelBuf pixels = {.w = w, .h = h, .data = data};
	Pixel bg = {.r = 0, .g = 0, .b = 0, .a = 255}; // TODO: make this configurable/nicer
	u32 current_x = padding;
	u32 current_y = padding;
	for (u32 i = 0; i < glyph_ids_len; i++) {
		GlyphId id = glyph_ids[i];
		pixelbuf_copy(pixels, glyphs[id], current_x, current_y, 1);
		current_x += glyphs[id].w + 1;
	}
	return pixels;
}

