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

static u32
get_battery_percent(void)
{
	FILE *fp = fopen("/sys/class/power_supply/BAT0/capacity", "r");
	if (!fp) return 0;
	u32 capacity = 0;
	fscanf(fp, "%u", &capacity);
	fclose(fp);
	return capacity;
}

static GlyphId
digit_to_glyph_id(u32 digit)
{
	// NOTE: this statement is probably gonna be optimzed into
	// just an addition, but writing it out as a switch statement
	// makes the intention more clear
	// UPDATE: godbolt seems to agree with me
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

static u32 get_battery_glyphs(u32 battery_percent, GlyphId *out)
{
	u32 len = 0;
	// TODO: this probably should be configurable
	if (battery_percent > 50) {
		out[len++] = GLYPH_ID_BATTERY_FULL;
	} else if (battery_percent > 20) {
		out[len++] = GLYPH_ID_BATTERY_HALF;
	} else {
		out[len++] = GLYPH_ID_BATTERY_LOW;
	}
	out[len++] = GLYPH_ID_SPACE;
	out[len++] = digit_to_glyph_id((battery_percent / 10) % 10);
	out[len++] = digit_to_glyph_id((battery_percent / 1 ) % 10);
	out[len++] = GLYPH_ID_PERCENT;
	return len;
}

static u32
get_glyphs(TimeAndDate td, u32 battery_percent, GlyphId *out)
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
	if (battery_percent > 0) {
		out[len++] = GLYPH_ID_SPACE;
		out[len++] = GLYPH_ID_PIPE;
		out[len++] = GLYPH_ID_SPACE;
		len += get_battery_glyphs(battery_percent, out + len);
	}
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

static void
draw_background(PixelBuf pixels)
{
	Pixel bg = { .r = 0x08, .g = 0x08, .b = 0x08, .a = 0xff };
	for (u32 i = 0; i < pixels.w * pixels.h; i++)
		pixels.data[i] = bg;
}

static void
draw_border(PixelBuf p)
{
	u32 w = p.w, h = p.h;
	Pixel border = { .r = 0xbd, .g = 0xbd, .b = 0xbd, .a = 0xff };
	for (u32 c = 0; c < w; c++) p.data[(0    ) * w + (c    )] = border;
	for (u32 c = 0; c < w; c++) p.data[(h - 1) * w + (c    )] = border;
	for (u32 r = 0; r < h; r++) p.data[(r    ) * w + (0    )] = border;
	for (u32 r = 0; r < h; r++) p.data[(r    ) * w + (w - 1)] = border;
}

static void
draw_glyphs(PixelBuf pixels, GlyphId *glyph_ids, u32 len, u32 x, u32 y)
{
	for (u32 i = 0; i < len; i++) {
		GlyphId id = glyph_ids[i];
		pixelbuf_blend(pixels, glyphs[id], x, y);
		x += glyphs[id].w + 1;
	}
}

PixelBuf
next_frame(Arena *arena)
{
	TimeAndDate td = get_time_and_date();
	u32 battery_percent = get_battery_percent();

	static GlyphId glyph_ids[1024];
	u32 glyph_ids_len = get_glyphs(td, battery_percent, glyph_ids);

	u32 base_w, base_h;
	measure_glyphs(glyph_ids, glyph_ids_len, &base_w, &base_h);

	u32 pad = 2;
	u32 w = base_w + pad * 2;
	u32 h = base_h + pad * 2;
	Pixel *data = arena_push_arr(arena, Pixel, w * h);
	PixelBuf pixels = { .w = w, .h = h, .data = data };

	draw_background(pixels);
	draw_border(pixels);
	draw_glyphs(pixels, glyph_ids, glyph_ids_len, pad, pad);

	return pixels;
}

