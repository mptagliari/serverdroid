/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * The face: what a renderer must offer. The main loop calls draw_cell() for the cells that changed (the dirty ones) and then flush();
 * a renderer never touches anything outside its own region and knows nothing about libvterm, the PTY or the input.
 */
#ifndef TERM_RENDER_H
#define TERM_RENDER_H
#include "cell.h"

typedef struct Renderer Renderer;
struct Renderer {
	int rows, cols;		/* cells that fit in the region */
	int cell_w, cell_h;	/* pixels, 0 for a renderer without pixels */
	void (*clear)(Renderer *r);					/* paint the whole region with the default background */
	void (*draw_cell)(Renderer *r, int row, int col, const Cell *c, int cursor);
	void (*flush)(Renderer *r, const char *status);			/* status: one line of state, used by the dump renderer */
	/* Drawn over the cells after a flush (the selection bar and handles): pixels of the region, clipped to it, no-ops where there are none. */
	void (*fill_rect)(Renderer *r, Box b, uint32_t rgb, int radius);
	void (*draw_text)(Renderer *r, int x, int y, const char *utf8, uint32_t rgb);	/* x, y: top left; transparent background */
	void (*close)(Renderer *r);
	void *impl;
};

/* The framebuffer renderer: draws into the pixel rectangle x, y, w, h of the framebuffer, clipped to it. */
Renderer *render_fb_open(const char *fbdev, const char *font, const char *font_bold, const char *font_tty, int x, int y, int w, int h);

/* The text renderer for tests: keeps the cells in memory and writes them to a file at every flush. */
Renderer *render_dump_open(const char *path, int rows, int cols);

#endif
