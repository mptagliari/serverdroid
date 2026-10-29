/* Text selection logic; see sel.h. */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <stdlib.h>
#include <string.h>
#include "sel.h"

enum { BAR_H = 64, BAR_GAP = 14, ITEM_W = 104, HANDLE = 44 };

void sel_clear(Sel *s) { memset(s, 0, sizeof(*s)); }

void sel_word(Sel *s, SelCell cell, void *user, int cols, int row, int col)
{
	int c0 = col, c1 = col;

	sel_clear(s);
	if (!cell(user, row, col)) {			/* a blank: nothing to select, only Paste */
		s->bar = 1;
		s->r0 = s->r1 = row;
		s->c0 = s->c1 = col;
		return;
	}
	while (c0 > 0 && cell(user, row, c0 - 1)) c0--;
	while (c1 < cols - 1 && cell(user, row, c1 + 1)) c1++;
	s->active = s->bar = 1;
	s->r0 = s->r1 = row;
	s->c0 = c0;
	s->c1 = c1;
}

void sel_all(Sel *s, int rows, int cols)
{
	s->active = s->bar = 1;
	s->r0 = 0;
	s->c0 = 0;
	s->r1 = rows - 1;
	s->c1 = cols - 1;
}

static int before(int ra, int ca, int rb, int cb) { return ra < rb || (ra == rb && ca < cb); }

void sel_move(Sel *s, int *end, int row, int col)
{
	int *r = *end ? &s->r1 : &s->r0, *c = *end ? &s->c1 : &s->c0;
	int *ro = *end ? &s->r0 : &s->r1, *co = *end ? &s->c0 : &s->c1;

	*r = row;
	*c = col;
	if (*end ? before(*r, *c, *ro, *co) : before(*ro, *co, *r, *c)) {	/* crossed: swap the two ends and keep following the one the finger holds */
		int tr = *r, tc = *c;
		*r = *ro; *c = *co;
		*ro = tr; *co = tc;
		*end = !*end;
	}
}

int sel_contains(const Sel *s, int row, int col)
{
	if (!s->active || row < s->r0 || row > s->r1)
		return 0;
	if (row == s->r0 && col < s->c0) return 0;
	if (row == s->r1 && col > s->c1) return 0;
	return 1;
}

static void put_cp(char **d, uint32_t cp)
{
	char *p = *d;

	if (cp < 0x80) *p++ = cp;
	else if (cp < 0x800) { *p++ = 0xC0 | (cp >> 6); *p++ = 0x80 | (cp & 63); }
	else if (cp < 0x10000) { *p++ = 0xE0 | (cp >> 12); *p++ = 0x80 | ((cp >> 6) & 63); *p++ = 0x80 | (cp & 63); }
	else { *p++ = 0xF0 | (cp >> 18); *p++ = 0x80 | ((cp >> 12) & 63); *p++ = 0x80 | ((cp >> 6) & 63); *p++ = 0x80 | (cp & 63); }
	*d = p;
}

char *sel_text(const Sel *s, SelCell cell, void *user, int cols)
{
	size_t cap = (size_t)(s->r1 - s->r0 + 1) * (cols * 4 + 1) + 1;
	char *out = malloc(cap), *d = out;

	if (!s->active || !out) {
		if (out) *out = 0;
		return out;
	}
	for (int r = s->r0; r <= s->r1; r++) {
		int c0 = r == s->r0 ? s->c0 : 0, c1 = r == s->r1 ? s->c1 : cols - 1, last = c0 - 1;
		for (int c = c0; c <= c1; c++)
			if (cell(user, r, c)) last = c;
		for (int c = c0; c <= last; c++) {	/* a trailing blank run is dropped; blanks inside the line are spaces */
			uint32_t cp = cell(user, r, c);
			put_cp(&d, cp ? cp : ' ');
		}
		if (r < s->r1)
			*d++ = '\n';
	}
	*d = 0;
	return out;
}

int sel_items(const Sel *s, int cw, int ch, int region_w, int region_h, SelItem out[3])
{
	static const struct { int act; const char *label; } defs[3] = { { SA_COPY, "Copy" }, { SA_PASTE, "Paste" }, { SA_ALL, "All" } };
	int n = s->active ? 3 : s->bar ? 1 : 0, x, y;

	if (!n)
		return 0;
	x = s->c0 * cw - (n * ITEM_W) / 2;
	if (x + n * ITEM_W > region_w) x = region_w - n * ITEM_W;
	if (x < 0) x = 0;
	y = s->r0 * ch - BAR_H - BAR_GAP;			/* above the first row */
	if (y < 0) y = (s->r1 + 1) * ch + HANDLE + BAR_GAP;	/* no room: below the last row and its handle */
	if (y + BAR_H > region_h) y = region_h - BAR_H;
	for (int i = 0; i < n; i++) {
		const int d = n == 1 ? 1 : i;
		out[i].act = defs[d].act;
		strcpy(out[i].label, defs[d].label);
		out[i].box = (Box){ x + i * ITEM_W, y, ITEM_W, BAR_H };
	}
	return n;
}

Box sel_handle(const Sel *s, int end, int cw, int ch)
{
	int r = end ? s->r1 : s->r0;
	int x = end ? (s->c1 + 1) * cw : s->c0 * cw;

	return (Box){ x - HANDLE / 2, (r + 1) * ch, HANDLE, HANDLE };
}
