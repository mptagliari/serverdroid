/* The framebuffer renderer: pixels of one rectangle of /dev/fb0, drawn cell by cell, never outside the rectangle. */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "font.h"
#include "render.h"

typedef struct {
	uint8_t *fb;
	size_t fb_len;
	int fd, stride, xres, yres;
	int x0, y0;			/* origin of the region, pixels */
	int roff, goff, boff, rlen, glen, blen;
	Font font, bold;
	int has_bold;
} Fb;

static uint32_t pack(const Fb *f, uint32_t rgb)
{
	uint32_t r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
	return ((r >> (8 - f->rlen)) << f->roff) | ((g >> (8 - f->glen)) << f->goff) | ((b >> (8 - f->blen)) << f->boff);
}

static uint32_t brighten(uint32_t rgb)
{
	uint32_t r = ((rgb >> 16) & 255) + 0x50, g = ((rgb >> 8) & 255) + 0x50, b = (rgb & 255) + 0x50;
	return ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
}

static void fb_clear(Renderer *r)
{
	Fb *f = r->impl;
	uint32_t bg = pack(f, DEFAULT_BG);

	for (int y = 0; y < r->rows * r->cell_h; y++) {
		uint32_t *p = (uint32_t *)(f->fb + (size_t)(f->y0 + y) * f->stride + (size_t)f->x0 * 4);
		for (int x = 0; x < r->cols * r->cell_w; x++)
			p[x] = bg;
	}
}

static void fb_draw_cell(Renderer *r, int row, int col, const Cell *c, int cursor)
{
	Fb *f = r->impl;
	uint32_t fg = c->fg, bg = c->bg, line[64];
	const uint8_t *glyph;
	int w = r->cell_w, h = r->cell_h, bpr = f->font.bytes_per_row;
	uint32_t pfg, pbg;

	if (row < 0 || row >= r->rows || col < 0 || col >= r->cols || w > 64)	/* clipping: nothing outside the region */
		return;
	if (c->attrs & A_BOLD)
		fg = brighten(fg);		/* with the bold glyphs: dark colors stay readable */
	if (((c->attrs & A_REVERSE) != 0) != (cursor != 0)) {
		uint32_t t = fg;
		fg = bg;
		bg = t;
	}
	pfg = pack(f, fg);
	pbg = pack(f, bg);
	glyph = c->ch > 32 ? font_glyph((c->attrs & A_BOLD) && f->has_bold ? &f->bold : &f->font, c->ch) : NULL;
	for (int y = 0; y < h; y++) {
		uint32_t *dst = (uint32_t *)(f->fb + (size_t)(f->y0 + row * h + y) * f->stride + (size_t)(f->x0 + col * w) * 4);
		const uint8_t *bits = glyph ? glyph + (size_t)y * bpr : NULL;
		int under = (c->attrs & A_UNDERLINE) && y >= h - 3 && y < h - 1;
		int strike = (c->attrs & A_STRIKE) && y >= h / 2 - 1 && y <= h / 2;

		for (int x = 0; x < w; x++) {
			int on = under || strike || (bits && (bits[x >> 3] & (0x80 >> (x & 7))));
			line[x] = on ? pfg : pbg;
		}
		memcpy(dst, line, (size_t)w * 4);
	}
}

static void fb_fill_rect(Renderer *r, Box b, uint32_t rgb, int radius)
{
	Fb *f = r->impl;
	uint32_t px = pack(f, rgb);
	int w = r->cols * r->cell_w, h = r->rows * r->cell_h;

	for (int y = 0; y < b.h; y++) {
		int ry = b.y + y;
		if (ry < 0 || ry >= h)
			continue;
		for (int x = 0; x < b.w; x++) {
			int rx = b.x + x;
			int cx = x < radius ? radius - 1 - x : x >= b.w - radius ? x - (b.w - radius) : -1;
			int cy = y < radius ? radius - 1 - y : y >= b.h - radius ? y - (b.h - radius) : -1;
			if (rx < 0 || rx >= w || (cx >= 0 && cy >= 0 && cx * cx + cy * cy > (radius - 1) * (radius - 1) + radius / 2))
				continue;
			((uint32_t *)(f->fb + (size_t)(f->y0 + ry) * f->stride))[f->x0 + rx] = px;
		}
	}
}

static void fb_draw_text(Renderer *r, int x, int y, const char *s, uint32_t rgb)
{
	Fb *f = r->impl;
	uint32_t px = pack(f, rgb);
	int w = r->cols * r->cell_w, h = r->rows * r->cell_h, bpr = f->font.bytes_per_row;

	while (*s) {
		uint32_t cp = (unsigned char)*s++;
		const uint8_t *g;
		if (cp >= 0xC0) {				/* a UTF-8 sequence */
			int n = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : 1;
			cp &= 0x3F >> n;
			while (n-- && ((unsigned char)*s & 0xC0) == 0x80) cp = (cp << 6) | ((unsigned char)*s++ & 63);
		}
		g = font_glyph(&f->font, cp);
		for (int gy = 0; gy < f->font.h; gy++)
			for (int gx = 0; gx < f->font.w; gx++) {
				int rx = x + gx, ry = y + gy;
				if ((g[(size_t)gy * bpr + (gx >> 3)] & (0x80 >> (gx & 7))) && rx >= 0 && rx < w && ry >= 0 && ry < h)
					((uint32_t *)(f->fb + (size_t)(f->y0 + ry) * f->stride))[f->x0 + rx] = px;
			}
		x += f->font.w;
	}
}

static void fb_flush(Renderer *r, const char *status)
{
	(void)r; (void)status;			/* the pixels are already in the framebuffer */
}

static void fb_close(Renderer *r)
{
	Fb *f = r->impl;

	munmap(f->fb, f->fb_len);
	close(f->fd);
	font_free(&f->font);
	font_free(&f->bold);
	free(f);
	free(r);
}

Renderer *render_fb_open(const char *fbdev, const char *font, const char *font_bold, const char *font_tty, int x, int y, int w, int h)
{
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo fx;
	Renderer *r = calloc(1, sizeof(*r));
	Fb *f = calloc(1, sizeof(*f));

	if (!r || !f)
		return NULL;
	f->fd = open(fbdev, O_RDWR);
	if (f->fd < 0 || ioctl(f->fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(f->fd, FBIOGET_FSCREENINFO, &fx) < 0 || v.bits_per_pixel != 32)
		return NULL;
	if (font_load_psf(&f->font, font) < 0 && font_load_console(&f->font, font_tty) < 0)	/* the console font is the fallback */
		return NULL;
	if (font_bold && font_load_psf(&f->bold, font_bold) == 0 && f->bold.w == f->font.w && f->bold.h == f->font.h)
		f->has_bold = 1;
	f->stride = fx.line_length;
	f->xres = v.xres;
	f->yres = v.yres;
	f->roff = v.red.offset; f->rlen = v.red.length;
	f->goff = v.green.offset; f->glen = v.green.length;
	f->boff = v.blue.offset; f->blen = v.blue.length;
	f->fb_len = (size_t)f->stride * v.yres_virtual;
	f->fb = mmap(NULL, f->fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, f->fd, 0);
	if (f->fb == MAP_FAILED)
		return NULL;
	r->cell_w = f->font.w;
	r->cell_h = f->font.h;
	r->cols = w / r->cell_w;
	r->rows = h / r->cell_h;
	if (x < 0 || y < 0 || x + r->cols * r->cell_w > f->xres || y + r->rows * r->cell_h > f->yres)	/* the region must lie inside the screen */
		return NULL;
	f->x0 = x;
	f->y0 = y;
	r->impl = f;
	r->clear = fb_clear;
	r->draw_cell = fb_draw_cell;
	r->flush = fb_flush;
	r->fill_rect = fb_fill_rect;
	r->draw_text = fb_draw_text;
	r->close = fb_close;
	return r;
}
