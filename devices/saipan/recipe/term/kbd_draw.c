/* The keyboard's painter; see kbd_draw.h. */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "font.h"
#include "kbd_draw.h"

enum {
	C_BG = 0x101114, C_KEY = 0x3c3f44, C_DARK = 0x2a2c30, C_PRESS = 0x7a7f87, C_LATCH = 0x3b6ea8, C_TEXT = 0xeceff4, C_HINT = 0x8d929b,
	C_TRAY = 0x202226, C_SEL = 0x3b6ea8, C_BUBBLE = 0x7a7f87, RADIUS = 12
};

struct KbdPaint {
	uint8_t *fb;
	size_t fb_len;
	int fd, stride;
	int roff, goff, boff, rlen, glen, blen;
	Box clip;
	uint32_t *buf;			/* clip.w x clip.h, already packed for the framebuffer */
	Font font;
	int has_header, masked;
	char prompt[96], text[600];
};

static uint32_t pack(const KbdPaint *p, uint32_t rgb)
{
	uint32_t r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
	return ((r >> (8 - p->rlen)) << p->roff) | ((g >> (8 - p->glen)) << p->goff) | ((b >> (8 - p->blen)) << p->boff);
}

static void put(KbdPaint *p, int x, int y, uint32_t px)	/* x, y relative to the clip */
{
	if (x >= 0 && y >= 0 && x < p->clip.w && y < p->clip.h)
		p->buf[(size_t)y * p->clip.w + x] = px;
}

static void round_rect(KbdPaint *p, Box b, uint32_t rgb)	/* b in screen coordinates */
{
	uint32_t px = pack(p, rgb);
	int x0 = b.x - p->clip.x, y0 = b.y - p->clip.y;

	for (int y = 0; y < b.h; y++)
		for (int x = 0; x < b.w; x++) {
			int cx = x < RADIUS ? RADIUS - 1 - x : x >= b.w - RADIUS ? x - (b.w - RADIUS) : -1;
			int cy = y < RADIUS ? RADIUS - 1 - y : y >= b.h - RADIUS ? y - (b.h - RADIUS) : -1;
			if (cx >= 0 && cy >= 0 && (cx * cx + cy * cy) > (RADIUS - 1) * (RADIUS - 1) + RADIUS / 2)
				continue;			/* outside the rounded corner */
			put(p, x0 + x, y0 + y, px);
		}
}

static uint32_t next_cp(const char **s)
{
	const unsigned char *u = (const unsigned char *)*s;
	uint32_t cp;
	int n;

	if (u[0] < 0x80) { cp = u[0]; n = 1; }
	else if ((u[0] & 0xE0) == 0xC0) { cp = ((u[0] & 31) << 6) | (u[1] & 63); n = 2; }
	else if ((u[0] & 0xF0) == 0xE0) { cp = ((u[0] & 15) << 12) | ((u[1] & 63) << 6) | (u[2] & 63); n = 3; }
	else { cp = 0xFFFD; n = 4; }
	*s += n;
	return cp;
}

static int ncps(const char *s)
{
	int n = 0;
	for (; *s; s++)
		if (((unsigned char)*s & 0xC0) != 0x80) n++;
	return n;
}

static void text(KbdPaint *p, int x, int y, const char *s, int scale, uint32_t rgb)	/* x, y: top left, screen coordinates */
{
	uint32_t px = pack(p, rgb);
	int bpr = p->font.bytes_per_row;

	while (*s) {
		const uint8_t *g = font_glyph(&p->font, next_cp(&s));
		for (int gy = 0; gy < p->font.h; gy++)
			for (int gx = 0; gx < p->font.w; gx++)
				if (g[(size_t)gy * bpr + (gx >> 3)] & (0x80 >> (gx & 7)))
					for (int sy = 0; sy < scale; sy++)
						for (int sx = 0; sx < scale; sx++)
							put(p, x - p->clip.x + gx * scale + sx, y - p->clip.y + gy * scale + sy, px);
		x += p->font.w * scale;
	}
}

static void centered(KbdPaint *p, Box b, const char *label, uint32_t rgb)
{
	int n = ncps(label), scale = n == 1 ? 2 : 1;
	int w = n * p->font.w * scale, h = p->font.h * scale;

	text(p, b.x + (b.w - w) / 2, b.y + (b.h - h) / 2, label, scale, rgb);
}

void kbd_paint_header(KbdPaint *p, const char *prompt, const char *text, int masked)
{
	p->has_header = 1;
	p->masked = masked;
	snprintf(p->prompt, sizeof(p->prompt), "%s", prompt);
	snprintf(p->text, sizeof(p->text), "%s", text);
}

Box kbd_paint_field(const KbdPaint *p)
{
	return (Box){ p->clip.x, p->clip.y + 64, p->clip.w, 88 };
}

static void paint_header(KbdPaint *p)
{
	Box f = kbd_paint_field(p);
	char shown[1800] = "";
	int max = (f.w - 40) / p->font.w, n = 0, skip;
	const char *s = p->text;
	char *d = shown;

	text(p, p->clip.x + 8, p->clip.y + 16, p->prompt, 1, C_HINT);
	round_rect(p, f, C_TRAY);
	for (const char *t = s; *t; t++)
		if (((unsigned char)*t & 0xC0) != 0x80) n++;
	skip = n + 1 > max ? n + 1 - max : 0;		/* the tail that fits, with room for the cursor mark */
	for (; *s; ) {
		const char *e = s + 1;
		while (((unsigned char)*e & 0xC0) == 0x80) e++;
		if (skip > 0) skip--;
		else if (p->masked) { memcpy(d, "\xe2\x80\xa2", 3); d += 3; }
		else { memcpy(d, s, e - s); d += e - s; }
		s = e;
	}
	*d++ = '_';
	*d = 0;
	text(p, f.x + 20, f.y + (f.h - p->font.h) / 2, shown, 1, C_TEXT);
}

void kbd_paint(KbdPaint *p, const Kbd *k)
{
	KbdKeyView v;
	KbdTrayView tv;
	Box bb;
	char bl[16];
	uint32_t bg = pack(p, C_BG);

	for (size_t i = 0; i < (size_t)p->clip.w * p->clip.h; i++)
		p->buf[i] = bg;
	if (p->has_header)
		paint_header(p);
	for (int i = 0; i < kbd_nkeys(k); i++) {
		uint32_t col;
		kbd_key_view(k, i, &v);
		col = v.flags & KF_PRESSED ? C_PRESS : v.flags & KF_LATCHED ? C_LATCH : v.flags & KF_DARK ? C_DARK : C_KEY;
		round_rect(p, v.box, col);
		centered(p, v.box, v.label, C_TEXT);
		if (v.hint[0])
			text(p, v.box.x + v.box.w - p->font.w - 3, v.box.y + 2, v.hint, 1, C_HINT);
	}
	if (kbd_bubble(k, &bb, bl)) {
		round_rect(p, bb, C_BUBBLE);
		centered(p, bb, bl, C_TEXT);
	}
	kbd_tray_view(k, &tv);
	if (tv.n) {
		Box t = tv.tray;
		t.x -= 4; t.w += 8; t.y -= 4; t.h += 8;
		round_rect(p, t, C_TRAY);
		for (int i = 0; i < tv.n; i++) {
			Box ib = tv.item[i].box;
			ib.x += 2; ib.w -= 4;
			if (i == tv.sel)
				round_rect(p, ib, C_SEL);
			centered(p, ib, tv.item[i].label, C_TEXT);
		}
	}
	for (int y = 0; y < p->clip.h; y++)
		memcpy(p->fb + (size_t)(p->clip.y + y) * p->stride + (size_t)p->clip.x * 4, p->buf + (size_t)y * p->clip.w, (size_t)p->clip.w * 4);
}

void kbd_paint_close(KbdPaint *p)
{
	if (!p)
		return;
	munmap(p->fb, p->fb_len);
	close(p->fd);
	font_free(&p->font);
	free(p->buf);
	free(p);
}

KbdPaint *kbd_paint_open(const char *fbdev, const char *font, Box clip)
{
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo fx;
	KbdPaint *p = calloc(1, sizeof(*p));

	if (!p)
		return NULL;
	p->fd = open(fbdev, O_RDWR);
	if (p->fd < 0 || ioctl(p->fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(p->fd, FBIOGET_FSCREENINFO, &fx) < 0 || v.bits_per_pixel != 32 ||
	    font_load_psf(&p->font, font) < 0 || clip.x < 0 || clip.y < 0 || clip.x + clip.w > (int)v.xres || clip.y + clip.h > (int)v.yres) {
		free(p);
		return NULL;
	}
	p->stride = fx.line_length;
	p->roff = v.red.offset; p->rlen = v.red.length;
	p->goff = v.green.offset; p->glen = v.green.length;
	p->boff = v.blue.offset; p->blen = v.blue.length;
	p->fb_len = (size_t)p->stride * v.yres_virtual;
	p->fb = mmap(NULL, p->fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, 0);
	if (p->fb == MAP_FAILED) {
		free(p);
		return NULL;
	}
	p->clip = clip;
	p->buf = malloc((size_t)clip.w * clip.h * 4);
	return p;
}
