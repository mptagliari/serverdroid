/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * The glyphs. The preferred source is a PSF2 file (Terminus 16x32 from the Alpine package font-terminus, with its Unicode table: all of Latin-1,
 * so the Portuguese accents are there, and a bold variant). The fallback is the kernel's own console font (the one fbcon uses) through the
 * console ioctls, which only has the 256 glyphs of code page 437 (no a-tilde, no o-tilde). Only this file knows how a font is read.
 */
#ifndef TERM_FONT_H
#define TERM_FONT_H
#include <stdint.h>

typedef struct {
	int w, h;			/* glyph size in pixels */
	int bytes_per_row;
	int count;			/* glyphs in the font */
	uint8_t *bits;			/* glyph i at bits + i * bytes_per_row * h */
	uint16_t *map;			/* Unicode code point -> glyph index, 0xFFFF when missing; 65536 entries */
} Font;

int font_load_console(Font *f, const char *tty);	/* 0 on success */
int font_load_psf(Font *f, const char *path);		/* a PSF2 console font, plain or gzipped, with its Unicode table; 0 on success */
const uint8_t *font_glyph(const Font *f, uint32_t codepoint);	/* the glyph of '?' when the code point is missing */
void font_free(Font *f);

#endif
