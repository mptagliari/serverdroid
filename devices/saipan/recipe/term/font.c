#include <fcntl.h>
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <zlib.h>
#include "font.h"

/* <linux/kd.h> without depending on the header being installed */
#define KDFONTOP 0x4B72
#define KD_FONT_OP_GET 1
#define GIO_UNIMAP 0x4B66

struct console_font_op { unsigned int op, flags, width, height, charcount; unsigned char *data; };
struct unipair { unsigned short unicode, fontpos; };
struct unimapdesc { unsigned short entry_ct; struct unipair *entries; };

int font_load_console(Font *f, const char *tty)
{
	struct console_font_op op;
	struct unimapdesc um;
	unsigned char *buf;
	int fd, stride;

	memset(f, 0, sizeof(*f));
	fd = open(tty, O_RDWR | O_NOCTTY);
	if (fd < 0)
		return -1;
	buf = calloc(1024, 32 * 4);		/* up to 1024 glyphs of 32 pixels wide (4 bytes) and 32 rows */
	memset(&op, 0, sizeof(op));
	op.op = KD_FONT_OP_GET;
	op.width = 32;
	op.height = 32;
	op.charcount = 1024;
	op.data = buf;
	if (!buf || ioctl(fd, KDFONTOP, &op) < 0 || !op.width || !op.height || !op.charcount) {
		free(buf);
		close(fd);
		return -1;
	}
	f->w = op.width;
	f->h = op.height;
	f->count = op.charcount;
	f->bytes_per_row = (f->w + 7) / 8;
	stride = f->bytes_per_row * 32;		/* the kernel pads every glyph to 32 rows */
	f->bits = malloc((size_t)f->count * f->bytes_per_row * f->h);
	for (int i = 0; i < f->count; i++)
		memcpy(f->bits + (size_t)i * f->bytes_per_row * f->h, buf + (size_t)i * stride, (size_t)f->bytes_per_row * f->h);
	free(buf);

	f->map = malloc(65536 * sizeof(uint16_t));
	memset(f->map, 0xFF, 65536 * sizeof(uint16_t));
	um.entry_ct = 0;
	um.entries = NULL;
	ioctl(fd, GIO_UNIMAP, &um);		/* fails with ENOMEM and reports how many entries there are */
	if (um.entry_ct) {
		struct unipair *e = calloc(um.entry_ct, sizeof(*e));
		um.entries = e;
		if (e && ioctl(fd, GIO_UNIMAP, &um) == 0)
			for (int i = 0; i < um.entry_ct; i++)
				if (e[i].fontpos < f->count && f->map[e[i].unicode] == 0xFFFF)
					f->map[e[i].unicode] = e[i].fontpos;
		free(e);
	} else {
		for (int i = 0; i < 256 && i < f->count; i++)	/* no map: the first glyphs as Latin-1 */
			f->map[i] = i;
	}
	close(fd);
	return 0;
}

const uint8_t *font_glyph(const Font *f, uint32_t cp)
{
	uint16_t g = cp < 65536 ? f->map[cp] : 0xFFFF;

	if (g == 0xFFFF)
		g = f->map['?'] != 0xFFFF ? f->map['?'] : 0;
	return f->bits + (size_t)g * f->bytes_per_row * f->h;
}

void font_free(Font *f)
{
	free(f->bits);
	free(f->map);
	memset(f, 0, sizeof(*f));
}

/* PSF2: header of 32 bytes (magic 72 b5 4a 86, version, header size, flags, glyph count, bytes per glyph, height, width), the glyphs, then,
 * when flag 1 is set, one entry per glyph: UTF-8 code points that use it, optionally 0xFE and sequences, closed by 0xFF. */
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int font_load_psf(Font *f, const char *path)
{
	gzFile g = gzopen(path, "rb");
	uint8_t *buf = NULL;
	size_t len = 0, cap = 0;
	int n;
	uint32_t hdr, flags, count, charsize, h, w;
	const uint8_t *t, *end;

	memset(f, 0, sizeof(*f));
	if (!g)
		return -1;
	for (;;) {
		if (len + 65536 > cap) {
			cap = cap ? cap * 2 : 1 << 18;
			buf = realloc(buf, cap);
		}
		n = gzread(g, buf + len, 65536);
		if (n <= 0)
			break;
		len += n;
	}
	gzclose(g);
	if (len < 32 || rd32(buf) != 0x864ab572u) {
		free(buf);
		return -1;
	}
	hdr = rd32(buf + 8);
	flags = rd32(buf + 12);
	count = rd32(buf + 16);
	charsize = rd32(buf + 20);
	h = rd32(buf + 24);
	w = rd32(buf + 28);
	if (!count || !charsize || (size_t)hdr + (size_t)count * charsize > len || charsize != ((w + 7) / 8) * h) {
		free(buf);
		return -1;
	}
	f->w = w;
	f->h = h;
	f->bytes_per_row = (w + 7) / 8;
	f->count = count;
	f->bits = malloc((size_t)count * charsize);
	memcpy(f->bits, buf + hdr, (size_t)count * charsize);
	f->map = malloc(65536 * sizeof(uint16_t));
	memset(f->map, 0xFF, 65536 * sizeof(uint16_t));
	if (flags & 1) {
		t = buf + hdr + (size_t)count * charsize;
		end = buf + len;
		for (uint32_t i = 0; i < count && t < end; i++) {
			int seq = 0;
			while (t < end && *t != 0xFF) {
				uint32_t cp;
				int k;
				if (*t == 0xFE) { seq = 1; t++; continue; }
				if (*t < 0x80) { cp = *t; k = 1; }
				else if ((*t & 0xE0) == 0xC0 && t + 1 < end) { cp = ((*t & 31) << 6) | (t[1] & 63); k = 2; }
				else if ((*t & 0xF0) == 0xE0 && t + 2 < end) { cp = ((*t & 15) << 12) | ((t[1] & 63) << 6) | (t[2] & 63); k = 3; }
				else if (t + 3 < end) { cp = 0x10000; k = 4; }
				else break;
				if (!seq && cp < 65536 && f->map[cp] == 0xFFFF)	/* single code points only: combining sequences are not drawn */
					f->map[cp] = i;
				t += k;
			}
			t++;
		}
	} else {
		for (uint32_t i = 0; i < 256 && i < count; i++)
			f->map[i] = i;
	}
	free(buf);
	return 0;
}
