/* The cell and rectangle types shared by the emulator wrapper, the renderers and the main loop. Nothing here depends on libvterm. */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#ifndef TERM_CELL_H
#define TERM_CELL_H
#include <stdint.h>

enum { A_BOLD = 1, A_UNDERLINE = 2, A_ITALIC = 4, A_BLINK = 8, A_REVERSE = 16, A_STRIKE = 32 };

typedef struct {
	uint32_t ch;		/* first code point of the cell, 0 for an empty cell */
	uint32_t fg, bg;	/* 0xRRGGBB, the default colors already resolved */
	uint16_t attrs;		/* A_* bits */
	uint8_t width;		/* columns the glyph spans (1 or 2) */
} Cell;

typedef struct { int r0, c0, r1, c1; } Rect;	/* inclusive on both ends */
typedef struct { int x, y, w, h; } Box;		/* pixels; w and h are sizes */

#define DEFAULT_FG 0xE0E0E0u
#define DEFAULT_BG 0x000000u

#endif
