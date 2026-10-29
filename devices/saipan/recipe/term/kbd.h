/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * The on-screen keyboard, logic only: the layout, which key a finger is on, shift/caps, the symbol pages, long-press accents, key repeat and
 * the sticky Ctrl and Alt. It knows nothing about pixels, the framebuffer or the PTY: touches go in, input events (the ones the FIFO protocol
 * of input.h also carries) and a haptic request come out, and the painter (kbd_draw.c) reads the state through the view functions.
 *
 * Touches are the raw ones of the touch service (P = pressed, M = moved, R = released), already converted to screen pixels. A key acts when
 * the finger is released over it, like Gboard; delete and the arrows act on press and repeat while held. Holding a key that has alternatives
 * opens a tray above it; sliding picks one and releasing types it.
 */
#ifndef TERM_KBD_H
#define TERM_KBD_H
#include "input.h"

typedef struct Kbd Kbd;

typedef struct {
	Event ev[4];
	int nev;
	int buzz;		/* a haptic pulse is due */
	int dirty;		/* what is on screen must be painted again */
} KbdOut;

enum { KF_PRESSED = 1, KF_LATCHED = 2, KF_DARK = 4 };	/* KF_DARK: a function key (a darker look) */

typedef struct {
	Box box;
	char label[16];		/* UTF-8, what the key shows now (shift and the page applied) */
	char hint[2];		/* a small corner character (the digit of the top row), or empty */
	int flags;
} KbdKeyView;

typedef struct {
	int n;			/* items in the tray, 0 when it is closed */
	struct { Box box; char label[8]; } item[10];
	int sel;
	Box tray;
} KbdTrayView;

Kbd *kbd_new(Box area);
void kbd_free(Kbd *k);
int kbd_inside(const Kbd *k, int x, int y);			/* a point inside the keyboard area */
int kbd_active(const Kbd *k);					/* a finger is down on the keyboard */
void kbd_touch(Kbd *k, char type, int x, int y, long now_ms, KbdOut *out);	/* type: 'P', 'M' or 'R' */
void kbd_tick(Kbd *k, long now_ms, KbdOut *out);		/* long press and repeat timers */
int kbd_timeout(const Kbd *k, long now_ms);			/* milliseconds until the next timer, -1 for none */

Box kbd_area(const Kbd *k);
int kbd_nkeys(const Kbd *k);
void kbd_key_view(const Kbd *k, int i, KbdKeyView *out);
void kbd_tray_view(const Kbd *k, KbdTrayView *out);
int kbd_bubble(const Kbd *k, Box *box, char label[16]);		/* the enlarged preview of the pressed key, 0 when there is none */
int kbd_key_at(const Kbd *k, const char *label);		/* tests: index of the key whose current label is this, -1 if none */

#endif
