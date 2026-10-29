/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * Text selection, logic only (no pixels, no libvterm): the Android way. A long press selects the word under the finger; two handles move the
 * ends; a floating bar offers Copy, Paste and All. This file knows the selection as a range of screen cells, finds words, extracts text and lays
 * out the bar and the handles in region pixels; the main loop does the touch handling and the renderer draws what is laid out here.
 */
#ifndef TERM_SEL_H
#define TERM_SEL_H
#include "cell.h"

typedef struct {
	int active;		/* a range is selected: (r0,c0) to (r1,c1), inclusive, in reading order */
	int r0, c0, r1, c1;
	int bar;		/* the floating bar is shown (with a selection: Copy Paste All; without one: Paste, above the cell r0,c0) */
} Sel;

typedef int (*SelCell)(void *user, int row, int col);	/* the code point of a cell, 0 when it is blank */

enum { SA_COPY = 1, SA_PASTE, SA_ALL };

typedef struct {
	int act;
	Box box;		/* region pixels */
	char label[8];
} SelItem;

void sel_clear(Sel *s);
void sel_word(Sel *s, SelCell cell, void *user, int cols, int row, int col);	/* long press: the word at the cell, or just the Paste bar on a blank */
void sel_all(Sel *s, int rows, int cols);
void sel_move(Sel *s, int *end, int row, int col);	/* move end 0 (start) or 1 (end) to a cell; they swap when they cross, *end follows */
int sel_contains(const Sel *s, int row, int col);
char *sel_text(const Sel *s, SelCell cell, void *user, int cols);	/* malloc'd UTF-8: rows joined by newlines, trailing blanks trimmed */
int sel_items(const Sel *s, int cw, int ch, int region_w, int region_h, SelItem out[3]);	/* the bar's buttons, 0 when there is no bar */
Box sel_handle(const Sel *s, int end, int cw, int ch);		/* the touch area of a handle (region pixels); drawn as a circle */

#endif
