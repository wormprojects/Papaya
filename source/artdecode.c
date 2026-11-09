/**
 * ctrmus - 3DS Music Player
 * Cover art decoding. See artdecode.h.
 */

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>
#include <png.h>

#include "artdecode.h"

/* Interlaced PNGs have to be decoded whole; refuse anything bigger. */
#define PNG_INTERLACED_MAX_PIXELS	(1024 * 1024)
#define MAX_DIMENSION				16384

/* ------------------------------------------------------------------ */
/* Streaming scaler: source rows in, RGB565 out                        */
/* ------------------------------------------------------------------ */

struct scaler
{
	int			sw, sh;		/* source size */
	int			dw, dh;		/* destination size (fits size x size) */
	int			size;		/* destination row stride */
	bool		shrink;		/* box filter (else nearest neighbour) */
	uint16_t*	out;
	int*		xmap;		/* source column -> destination column */
	uint32_t*	acc;		/* dw x {r, g, b, count} for the current row */
	int			accRow;		/* destination row in acc, -1 = none */
	unsigned char*	rgb;	/* sw * 3 scratch row */
};

static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
	return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static int scalerInit(struct scaler* s, int sw, int sh, uint16_t* out,
                      int size)
{
	memset(s, 0, sizeof(*s));
	if(sw <= 0 || sh <= 0 || sw > MAX_DIMENSION || sh > MAX_DIMENSION)
		return -1;

	s->sw = sw;
	s->sh = sh;
	s->size = size;
	s->out = out;
	s->accRow = -1;

	if(sw >= sh)
	{
		s->dw = size;
		s->dh = (int)((long long)sh * size / sw);
	}
	else
	{
		s->dh = size;
		s->dw = (int)((long long)sw * size / sh);
	}
	if(s->dw < 1) s->dw = 1;
	if(s->dh < 1) s->dh = 1;
	s->shrink = sw >= s->dw && sh >= s->dh;

	s->xmap = malloc(sizeof(int) * (size_t)sw);
	s->acc = calloc((size_t)s->dw * 4, sizeof(uint32_t));
	s->rgb = malloc((size_t)sw * 3);
	if(s->xmap == NULL || s->acc == NULL || s->rgb == NULL)
		return -1;

	for(int x = 0; x < sw; x++)
		s->xmap[x] = (int)((long long)x * s->dw / sw);

	return 0;
}

static void scalerFlush(struct scaler* s)
{
	uint16_t* row;

	if(s->accRow < 0)
		return;

	row = s->out + (size_t)s->accRow * s->size;
	for(int x = 0; x < s->dw; x++)
	{
		uint32_t* a = &s->acc[x * 4];

		if(a[3] != 0)
			row[x] = rgb565(a[0] / a[3], a[1] / a[3], a[2] / a[3]);
	}
	memset(s->acc, 0, sizeof(uint32_t) * 4 * (size_t)s->dw);
	s->accRow = -1;
}

/* Feed source row y (RGB, sw * 3 bytes). Rows must come in order. */
static void scalerRow(struct scaler* s, int y, const unsigned char* rgb)
{
	if(s->shrink)
	{
		int ty = (int)((long long)y * s->dh / s->sh);

		if(ty != s->accRow)
		{
			scalerFlush(s);
			s->accRow = ty;
		}

		for(int x = 0; x < s->sw; x++)
		{
			uint32_t* a = &s->acc[s->xmap[x] * 4];

			a[0] += rgb[x * 3];
			a[1] += rgb[x * 3 + 1];
			a[2] += rgb[x * 3 + 2];
			a[3]++;
		}
		return;
	}

	/* Enlarging (tiny art): every destination row whose nearest source
	 * row is y samples this row. */
	for(int ty = 0; ty < s->dh; ty++)
	{
		if((int)((long long)ty * s->sh / s->dh) != y)
			continue;

		for(int tx = 0; tx < s->dw; tx++)
		{
			const unsigned char* p =
				&rgb[(int)((long long)tx * s->sw / s->dw) * 3];

			s->out[(size_t)ty * s->size + tx] = rgb565(p[0], p[1], p[2]);
		}
	}
}

static void scalerFree(struct scaler* s)
{
	free(s->xmap);
	free(s->acc);
	free(s->rgb);
}

/* ------------------------------------------------------------------ */
/* JPEG                                                                */
/* ------------------------------------------------------------------ */

struct jpegErr
{
	struct jpeg_error_mgr	pub;
	jmp_buf					jb;
};

static void jpegErrorExit(j_common_ptr c)
{
	longjmp(((struct jpegErr*)c->err)->jb, 1);
}

static void jpegOutputMessage(j_common_ptr c)
{
	(void)c;
}

static int decodeJpeg(const unsigned char* data, size_t len, uint16_t* out,
                      int size, struct scaler* s)
{
	struct jpeg_decompress_struct	cinfo;
	struct jpegErr					err;
	unsigned char* volatile			row = NULL;

	cinfo.err = jpeg_std_error(&err.pub);
	err.pub.error_exit = jpegErrorExit;
	err.pub.output_message = jpegOutputMessage;

	if(setjmp(err.jb))
	{
		jpeg_destroy_decompress(&cinfo);
		free(row);
		return -1;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, data, (unsigned long)len);
	jpeg_read_header(&cinfo, TRUE);

	{
		bool		cmyk = cinfo.jpeg_color_space == JCS_CMYK ||
		                   cinfo.jpeg_color_space == JCS_YCCK;
		unsigned	longest = cinfo.image_width > cinfo.image_height ?
		                      cinfo.image_width : cinfo.image_height;
		unsigned	denom = 8;

		/* Biggest DCT reduction that still leaves >= size pixels. */
		while(denom > 1 && (longest + denom - 1) / denom < (unsigned)size)
			denom /= 2;

		cinfo.scale_num = 1;
		cinfo.scale_denom = denom;
		cinfo.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;
		cinfo.dct_method = JDCT_IFAST;
		cinfo.do_fancy_upsampling = FALSE;
		cinfo.do_block_smoothing = FALSE;
		jpeg_start_decompress(&cinfo);

		if(scalerInit(s, (int)cinfo.output_width, (int)cinfo.output_height,
		              out, size) != 0 ||
		   (row = malloc((size_t)cinfo.output_width *
		                 (size_t)cinfo.output_components)) == NULL)
		{
			jpeg_destroy_decompress(&cinfo);
			free(row);
			return -1;
		}

		while(cinfo.output_scanline < cinfo.output_height)
		{
			JSAMPROW	rows[1] = { row };
			int			y = (int)cinfo.output_scanline;

			jpeg_read_scanlines(&cinfo, rows, 1);

			if(cmyk)
			{
				/* Adobe CMYK JPEGs are stored inverted. */
				for(unsigned x = 0; x < cinfo.output_width; x++)
				{
					unsigned k = row[x * 4 + 3];

					s->rgb[x * 3]     = (unsigned char)(row[x * 4] * k / 255);
					s->rgb[x * 3 + 1] =
						(unsigned char)(row[x * 4 + 1] * k / 255);
					s->rgb[x * 3 + 2] =
						(unsigned char)(row[x * 4 + 2] * k / 255);
				}
				scalerRow(s, y, s->rgb);
			}
			else
				scalerRow(s, y, row);
		}
	}

	scalerFlush(s);
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	free(row);
	return 0;
}

/* ------------------------------------------------------------------ */
/* PNG                                                                 */
/* ------------------------------------------------------------------ */

struct memReader
{
	const unsigned char*	data;
	size_t					len;
	size_t					pos;
};

static void pngRead(png_structp png, png_bytep out, png_size_t n)
{
	struct memReader* m = png_get_io_ptr(png);

	if(n > m->len - m->pos)
		png_error(png, "truncated");
	memcpy(out, m->data + m->pos, n);
	m->pos += n;
}

static void pngError(png_structp png, png_const_charp msg)
{
	(void)msg;
	png_longjmp(png, 1);
}

static void pngWarning(png_structp png, png_const_charp msg)
{
	(void)png;
	(void)msg;
}

/* RGBA row -> s->rgb, blended over bg. */
static void pngFeedRow(struct scaler* s, int y, const unsigned char* rgba,
                       const unsigned char bg[3])
{
	for(int x = 0; x < s->sw; x++)
	{
		unsigned a = rgba[x * 4 + 3];

		for(int c = 0; c < 3; c++)
			s->rgb[x * 3 + c] = (unsigned char)
				((rgba[x * 4 + c] * a + bg[c] * (255 - a)) / 255);
	}
	scalerRow(s, y, s->rgb);
}

static int decodePng(const unsigned char* data, size_t len, uint16_t* out,
                     int size, uint16_t alphaBg, struct scaler* s)
{
	struct memReader		m = { data, len, 0 };
	png_structp				png;
	png_infop				info;
	unsigned char* volatile	image = NULL;
	png_bytep* volatile		rows = NULL;
	const unsigned char		bg[3] = {
		(unsigned char)(((alphaBg >> 11) & 0x1F) << 3),
		(unsigned char)(((alphaBg >> 5) & 0x3F) << 2),
		(unsigned char)((alphaBg & 0x1F) << 3)
	};

	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, pngError,
	                             pngWarning);
	if(png == NULL)
		return -1;
	info = png_create_info_struct(png);
	if(info == NULL)
	{
		png_destroy_read_struct(&png, NULL, NULL);
		return -1;
	}

	if(setjmp(png_jmpbuf(png)))
	{
		png_destroy_read_struct(&png, &info, NULL);
		free(image);
		free(rows);
		return -1;
	}

	png_set_read_fn(png, &m, pngRead);
	png_read_info(png, info);

	{
		png_uint_32	w = png_get_image_width(png, info);
		png_uint_32	h = png_get_image_height(png, info);
		int			colorType = png_get_color_type(png, info);
		int			passes;

		/* Everything to 8-bit RGBA. */
		png_set_strip_16(png);
		png_set_packing(png);
		if(colorType == PNG_COLOR_TYPE_PALETTE)
			png_set_palette_to_rgb(png);
		if(colorType == PNG_COLOR_TYPE_GRAY &&
		   png_get_bit_depth(png, info) < 8)
			png_set_expand_gray_1_2_4_to_8(png);
		if(png_get_valid(png, info, PNG_INFO_tRNS))
			png_set_tRNS_to_alpha(png);
		if(colorType == PNG_COLOR_TYPE_GRAY ||
		   colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
			png_set_gray_to_rgb(png);
		png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
		passes = png_set_interlace_handling(png);
		png_read_update_info(png, info);

		if(png_get_rowbytes(png, info) != (size_t)w * 4 ||
		   scalerInit(s, (int)w, (int)h, out, size) != 0)
			png_error(png, "setup");

		if(passes > 1)
		{
			/* Interlaced: libpng needs the whole image. */
			if((size_t)w * h > PNG_INTERLACED_MAX_PIXELS ||
			   (image = malloc((size_t)w * h * 4)) == NULL ||
			   (rows = malloc(sizeof(png_bytep) * h)) == NULL)
				png_error(png, "too big");

			for(png_uint_32 y = 0; y < h; y++)
				rows[y] = image + (size_t)y * w * 4;
			png_read_image(png, rows);

			for(png_uint_32 y = 0; y < h; y++)
				pngFeedRow(s, (int)y, rows[y], bg);
		}
		else
		{
			if((image = malloc((size_t)w * 4)) == NULL)
				png_error(png, "oom");

			for(png_uint_32 y = 0; y < h; y++)
			{
				png_read_row(png, image, NULL);
				pngFeedRow(s, (int)y, image, bg);
			}
		}
	}

	scalerFlush(s);
	png_destroy_read_struct(&png, &info, NULL);
	free(image);
	free(rows);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

int artDecode(const unsigned char* data, size_t len, uint16_t* out,
              int size, uint16_t alphaBg, int* w, int* h)
{
	struct scaler	s;
	int				ret = -1;

	memset(&s, 0, sizeof(s));

	if(data == NULL || out == NULL || size <= 0)
		return -1;

	if(len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
		ret = decodeJpeg(data, len, out, size, &s);
	else if(len >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0)
		ret = decodePng(data, len, out, size, alphaBg, &s);

	if(ret == 0)
	{
		*w = s.dw;
		*h = s.dh;
	}
	scalerFree(&s);
	return ret;
}
