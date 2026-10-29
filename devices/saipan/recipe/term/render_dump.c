/* The text renderer for tests: the screen as text in a file, so what the emulator and the PTY did can be checked without a display. */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

typedef struct {
	char *path;
	Cell *cells;
	char *cursor;
} Dump;

static void dump_fill_rect(Renderer *r, Box b, uint32_t rgb, int radius) { (void)r; (void)b; (void)rgb; (void)radius; }
static void dump_draw_text(Renderer *r, int x, int y, const char *s, uint32_t rgb) { (void)r; (void)x; (void)y; (void)s; (void)rgb; }

static void dump_clear(Renderer *r)
{
	Dump *d = r->impl;

	memset(d->cursor, 0, (size_t)r->rows * r->cols);
	for (int i = 0; i < r->rows * r->cols; i++)
		d->cells[i] = (Cell){ 0, DEFAULT_FG, DEFAULT_BG, 0, 1 };
}

static void dump_draw_cell(Renderer *r, int row, int col, const Cell *c, int cursor)
{
	Dump *d = r->impl;

	if (row < 0 || row >= r->rows || col < 0 || col >= r->cols)
		return;
	d->cells[row * r->cols + col] = *c;
	d->cursor[row * r->cols + col] = cursor;
}

static void put_utf8(FILE *f, uint32_t cp)
{
	if (cp < 0x80) fputc(cp ? cp : ' ', f);
	else if (cp < 0x800) { fputc(0xC0 | (cp >> 6), f); fputc(0x80 | (cp & 63), f); }
	else if (cp < 0x10000) { fputc(0xE0 | (cp >> 12), f); fputc(0x80 | ((cp >> 6) & 63), f); fputc(0x80 | (cp & 63), f); }
	else { fputc(0xF0 | (cp >> 18), f); fputc(0x80 | ((cp >> 12) & 63), f); fputc(0x80 | ((cp >> 6) & 63), f); fputc(0x80 | (cp & 63), f); }
}

static void dump_flush(Renderer *r, const char *status)
{
	Dump *d = r->impl;
	char tmp[512];
	FILE *f;

	snprintf(tmp, sizeof(tmp), "%s.tmp", d->path);
	f = fopen(tmp, "w");
	if (!f)
		return;
	fprintf(f, "#%s\n", status ? status : "");
	for (int row = 0; row < r->rows; row++) {
		fputc('|', f);
		for (int col = 0; col < r->cols; col++)
			put_utf8(f, d->cells[row * r->cols + col].ch);
		fputs("|\n", f);
	}
	for (int row = 0; row < r->rows; row++)		/* cells that are not plain: colors and attributes */
		for (int col = 0; col < r->cols; col++) {
			const Cell *c = &d->cells[row * r->cols + col];
			if (c->ch > 32 && (c->fg != DEFAULT_FG || c->bg != DEFAULT_BG || c->attrs))
				fprintf(f, "@%d,%d fg=%06X bg=%06X attrs=%u\n", row, col, c->fg, c->bg, c->attrs);
			if (d->cursor[row * r->cols + col])
				fprintf(f, "cursor@%d,%d\n", row, col);
		}
	fclose(f);
	rename(tmp, d->path);
}

static void dump_close(Renderer *r)
{
	Dump *d = r->impl;

	free(d->path);
	free(d->cells);
	free(d->cursor);
	free(d);
	free(r);
}

Renderer *render_dump_open(const char *path, int rows, int cols)
{
	Renderer *r = calloc(1, sizeof(*r));
	Dump *d = calloc(1, sizeof(*d));

	d->path = strdup(path);
	d->cells = calloc((size_t)rows * cols, sizeof(Cell));
	d->cursor = calloc((size_t)rows * cols, 1);
	r->rows = rows;
	r->cols = cols;
	r->impl = d;
	r->clear = dump_clear;
	r->draw_cell = dump_draw_cell;
	r->flush = dump_flush;
	r->fill_rect = dump_fill_rect;
	r->draw_text = dump_draw_text;
	r->close = dump_close;
	dump_clear(r);
	return r;
}
