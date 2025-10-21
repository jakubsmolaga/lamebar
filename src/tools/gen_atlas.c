#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <png.h>
#include <dirent.h>
#include <string.h>

typedef struct Image Image;
struct Image {
	unsigned char **data;
	char *filename;
	int w, h;
	int x, y; // used for packing
};

static Image
load_image(const char *path)
{
	FILE *fp = fopen(path, "rb");
	const char *ver = PNG_LIBPNG_VER_STRING;
	png_struct *png = png_create_read_struct(ver, 0, 0, 0);
	png_info *info = png_create_info_struct(png);
	png_init_io(png, fp);
	png_read_info(png, info);
	png_uint_32 w  = png_get_image_width(png, info);
	png_uint_32 h = png_get_image_height(png, info);
	png_byte **data = malloc(sizeof(png_bytep) * h);
	int rowbytes = png_get_rowbytes(png, info);
	for (png_uint_32 y = 0; y < h; y++) {
		data[y] = malloc(rowbytes);
	}
	png_read_image(png, data);
	png_destroy_read_struct(&png, &info, 0);
	fclose(fp);
	return (Image) { 
		.data=data, 
		.w=w, 
		.h=h,
	};
}

static char*
concat(const char *a, const char *b)
{
	static char buf[1024];
	sprintf(buf, "%s/%s", a, b);
	return strdup(buf);
}

static int
is_ent_png(const struct dirent *entry)
{
	if (entry->d_type != DT_REG)
		return 0;
	char *ext = strrchr(entry->d_name, '.');
	if (strcmp(ext, ".png") != 0)
		return 0;
	return 1;
}

static Image*
load_all_images(const char *root, int *out_len)
{
	struct dirent **entries;
	int n = scandir(root, &entries, is_ent_png, alphasort);
	Image *imgs = malloc(sizeof(Image) * n);
	int len = 0;
	for (int i=0; i<n; i++) {
		char *name = entries[i]->d_name;
		char *path = concat(root, name);
		imgs[len] = load_image(path);
		imgs[len].filename = name;
		len++;
	}
	*out_len = len;
	return imgs;
}

static int
cmp_imgs_by_height(const void *ptr_a, const void *ptr_b)
{
	const Image *img_a = ptr_a;
	const Image *img_b = ptr_b;
	if (img_a->h < img_b->h)
		return -1;
	if (img_a->h > img_b->h)
		return 1;
	return 0;
}

static int
pack_imgs_into(Image *imgs, int imgs_len, int w, int h)
{
	qsort(imgs, imgs_len, sizeof(Image), cmp_imgs_by_height);
	int cur_y = 0;
	int cur_x = 0;
	int next_y = 0;
	for (int i=0; i<imgs_len; i++) {
		Image *img = &imgs[i];
		if (cur_x + img->w >= w) {
			cur_y = next_y;
			cur_x = 0;
		}
		if (cur_y + img->h >= h) {
			return 1;
		}
		img->x = cur_x;
		img->y = cur_y;
		cur_x += img->w;
		if (cur_y + img->h > next_y)
			next_y = cur_y + img->h;
	}
	return 0;
}

static uint8_t**
get_atlas_buffer(const Image *imgs, int imgs_len, int w, int h)
{
	unsigned char **data = malloc(sizeof(uint8_t*) * h);
	for (int i=0; i<h; i++)
		data[i] = calloc(4 * w, sizeof(uint8_t));
	for (int i=0; i<imgs_len; i++) {
		const Image *img = &imgs[i];
		for (int dy=0; dy<img->h; dy++) {
			for (int dx = 0; dx < img->w; dx++) {
				int srcx = dx * 4;
				int srcy = dy;
				int dstx = (img->x + dx) * 4;
				int dsty = (img->y + dy);
				uint8_t *src = &img->data[srcy][srcx];
				uint8_t *dst = &data[dsty][dstx];
				memcpy(dst, src, 4);
			}
		}
	}
	return data;
}

static void
to_upper(char *s)
{
	for (char *p = s; *p; p++) {
		if (*p >= 'a' && *p <= 'z') {
			*p -= 'a' - 'A';
		}
	}
}

static void
write_header_file(const char *path, Image *imgs, int imgs_len, int w, int h)
{
	FILE *fp = fopen(path, "w");
	fprintf(fp, "#pragma once\n");
	fprintf(fp, "\n");
	fprintf(fp, "typedef unsigned char GlyphId;\n");
	fprintf(fp, "enum {\n");
	for (int i=0; i<imgs_len; i++) {
		char *s = strdup(imgs[i].filename);
		*strrchr(s, '.') = '\0';
		to_upper(s);
		fprintf(fp, "\tGLYPH_%s,\n", s);
	}
	fprintf(fp, "\tGLYPH_COUNT,\n");
	fprintf(fp, "};\n");
	fprintf(fp, "\n");
	fprintf(fp, "typedef struct { float x, y, w, h; } GlyphInfo;\n");
	fprintf(fp, "extern GlyphInfo glyph_infos[GLYPH_COUNT];\n");
	fprintf(fp, "static const int atlas_w = %d;\n", w);
	fprintf(fp, "static const int atlas_h = %d;\n", h);
	fprintf(fp, "extern const unsigned char atlas_pixels[];\n");
	fclose(fp);
}

static void
write_source_file(const char *path, Image *imgs, int imgs_len, int w, int h, uint8_t **pixels)
{
	FILE *fp = fopen(path, "w");
	fprintf(fp, "#include \"glyphs.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "GlyphInfo glyph_infos[GLYPH_COUNT] = {\n");
	for (int i=0; i<imgs_len; i++) {
		char *s = strdup(imgs[i].filename);
		*strrchr(s, '.') = '\0';
		to_upper(s);
		fprintf(fp, "\t[GLYPH_%s] = { %d, %d, %d, %d },\n", s,
			imgs[i].x, imgs[i].y, imgs[i].w, imgs[i].h);
	}
	fprintf(fp, "};\n");
	fprintf(fp, "\n");
	fprintf(fp, "const unsigned char atlas_pixels[] = {\n");
	for (int row = 0; row < h; row++) {
		fprintf(fp, "\t");
		for (int col = 0; col < w; col++) {
			fprintf(fp, "%d,", pixels[row][col * 4 + 0]);
			fprintf(fp, "%d,", pixels[row][col * 4 + 1]);
			fprintf(fp, "%d,", pixels[row][col * 4 + 2]);
			fprintf(fp, "%d,", pixels[row][col * 4 + 3]);
		}
		fprintf(fp, "\n");
	}
	fprintf(fp, "};\n");
	fclose(fp);
}

int main(int argc, char **argv) {
	if (argc != 4) {
		fprintf(stderr, "usage: %s <imgs_dir> <outpath_h> <outpath_c>\n", argv[0]);
		return 1;
	}
	const char *imgs_dir = argv[1];
	const char *outpath_h = argv[2];
	const char *outpath_c = argv[3];
	int imgs_len;
	Image *imgs = load_all_images(imgs_dir, &imgs_len);
	int w = 1, h = 1;
	while (pack_imgs_into(imgs, imgs_len, w, h)) {
		w *= 2;
		h *= 2;
	}
	uint8_t **data = get_atlas_buffer(imgs, imgs_len, w, h);
	write_header_file(outpath_h, imgs, imgs_len, w, h);
	write_source_file(outpath_c, imgs, imgs_len, w, h, data);
}
