#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <png.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include "../base/arena.h"
#include "../base/arena.c"

typedef struct Asset Asset;
struct Asset {
	Asset *next;
	PixelBuf buf;
	char *name;
};

static u8*
load_image(Arena *arena, const char *path, u32 *out_w, u32 *out_h)
{
	FILE *fp = fopen(path, "rb");
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png_create_info_struct(png);
	setjmp(png_jmpbuf(png));
	png_init_io(png, fp);
	png_read_info(png, info);

	int width = png_get_image_width(png, info);
	int height = png_get_image_height(png, info);
	int color_type = png_get_color_type(png, info);
	int bit_depth = png_get_bit_depth(png, info);

	// Convert to 8-bit RGBA if needed
	if (bit_depth == 16)
		png_set_strip_16(png);
	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if (color_type == PNG_COLOR_TYPE_RGB ||
	    color_type == PNG_COLOR_TYPE_GRAY ||
	    color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	if (color_type == PNG_COLOR_TYPE_GRAY ||
	    color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);

	png_read_update_info(png, info);

	size_t rowbytes = png_get_rowbytes(png, info);
	size_t image_size = rowbytes * height;
	uint8_t *data = arena_push_raw(arena, image_size, 4);

	uint8_t **row_pointers = arena_push_arr(arena, uint8_t*, height);
	for (int y = 0; y < height; y++)
		row_pointers[y] = data + y * rowbytes;

	png_read_image(png, row_pointers);

	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);

	// RGBA -> BGRA
	for (int i = 0; i < width * height; i++) {
		uint8_t r = data[i * 4 + 0];
		uint8_t g = data[i * 4 + 1];
		uint8_t b = data[i * 4 + 2];
		uint8_t a = data[i * 4 + 3];
		data[i * 4 + 0] = b;
		data[i * 4 + 1] = g;
		data[i * 4 + 2] = r;
		data[i * 4 + 3] = a;
	}

	*out_w = width;
	*out_h = height;
	return data;
}

static Asset*
load_all_images(Arena *arena, const char *root)
{
	Asset *result = NULL;
	DIR *dirp = opendir(root);
	struct dirent *entry;
	while ((entry = readdir(dirp)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0) continue;
		if (strcmp(entry->d_name, "..") == 0) continue;
		char *ext = strrchr(entry->d_name, '.');
		if (strcmp(ext, ".png") != 0) continue;
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
		u32 w, h;
		u8 *data = load_image(arena, path, &w, &h);
		Asset *asset = arena_push(arena, Asset);
		asset->next = result;
		result = asset;
		asset->buf.data = (void*)data;
		asset->buf.w = w;
		asset->buf.h = h;
		// copy filename and remove extension
		asset->name = arena_push_str(arena, entry->d_name);
		strcpy(asset->name, entry->d_name);
		*strrchr(asset->name, '.') = '\0';
	}
	return result;
}

static int
asset_cmp(const void *a, const void *b)
{
	Asset *aa = *(Asset**)a;
	Asset *bb = *(Asset**)b;
	return strcmp(aa->name, bb->name);
}

static Asset*
sort_assets(Asset *assets)
{
	Arena tmp = arena_create();
	Asset **buf = arena_pos(&tmp);
	int count = 0;
	for (Asset *a = assets; a != NULL; a = a->next) {
		buf[count++] = a;
	}
	qsort(buf, count, sizeof(Asset*), asset_cmp);
	for (int i = 0; i+1 < count; i++) {
		buf[i]->next = buf[i+1];
	}
	buf[count-1]->next = NULL;
	assets = buf[0];
	arena_destroy(&tmp);
	return assets;
}

static void
write_header(const char *path, Asset *assets)
{
	Arena tmp = arena_create();
	FILE *fp = fopen(path, "w");
	fprintf(fp, "#pragma once\n");
	fprintf(fp, "#include \"../base/base.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "typedef u8 GlyphId;\n");
	fprintf(fp, "enum {\n");
	int count = 0;
	for (Asset *a = assets; a != NULL; a = a->next) {
		char *upper = arena_push_str(&tmp, a->name);
		for (char *p = upper; *p; p++) *p = toupper(*p);
		fprintf(fp, "\tGLYPH_ID_%s,\n", upper);
		arena_clear(&tmp);
		count++;
	}
	fprintf(fp, "\tGLYPH_ID_COUNT\n");
	fprintf(fp, "};\n");
	fprintf(fp, "\n");
	fprintf(fp, "extern PixelBuf glyphs[GLYPH_ID_COUNT];\n");
	fclose(fp);
	arena_destroy(&tmp);
}

static void
write_c(const char *path, Asset *assets)
{
	Arena tmp = arena_create();
	FILE *fp = fopen(path, "w");
	fprintf(fp, "#include \"glyphs.h\"\n");
	fprintf(fp, "#include \"../base/base.h\"\n");
	fprintf(fp, "\n");
	for (Asset *a = assets; a != NULL; a = a->next) {
		fprintf(fp, "static u8 glyphs_data_%s[] = {", a->name);
		for (u32 i = 0; i < a->buf.w * a->buf.h; i++) {
			Pixel p = a->buf.data[i];
			fprintf(fp, "%u,%u,%u,%u", p.b, p.g, p.r, p.a);
			if (i != a->buf.w * a->buf.h - 1) fprintf(fp, ",");
		}
		fprintf(fp, "};\n");
	}
	fprintf(fp, "PixelBuf glyphs[GLYPH_ID_COUNT] = {\n");
	for (Asset *a = assets; a != NULL; a = a->next) {
		char *upper = arena_push_str(&tmp, a->name);
		for (char *p = upper; *p; p++) *p = toupper(*p);
		fprintf(fp, "\t[GLYPH_ID_%s] = { .data = (Pixel*)glyphs_data_%s, .w = %u, .h = %u },\n", upper, a->name, a->buf.w, a->buf.h);
		arena_clear(&tmp);
	}
	fprintf(fp, "};\n");
	fclose(fp);
	arena_destroy(&tmp);
}

int main(int argc, char **argv) {
	Arena arena = arena_create();
	if (argc != 2) {
		fprintf(stderr, "usage: %s <imgs_dir>\n", argv[0]);
		return 1;
	}
	const char *imgs_dir = argv[1];
	// const char *outpath_h = argv[2];
	// const char *outpath_c = argv[3];
	Asset *assets = load_all_images(&arena, imgs_dir);
	assets = sort_assets(assets);
	for (Asset *asset = assets; asset != NULL; asset = asset->next) {
		printf("%s (%u/%u)\n", asset->name, asset->buf.w, asset->buf.h);
	}
	system("mkdir -p src/generated");
	write_header("src/generated/glyphs.h", assets);
	write_c("src/generated/glyphs.c", assets);
}
