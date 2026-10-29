/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * The input: events from FIFOs, one text line each. The same events come from the interface's tokens (the buttons and the touch service write
 * UP, DOWN, OK, POWER, TAP x y to /run/keys.fifo) and from a test FIFO with a small protocol. The on-screen keyboard will be one more writer.
 *
 *   T <text>            characters typed (UTF-8, the rest of the line)
 *   K <key>[+mods]      enter tab backspace escape up down left right ins del home end pgup pgdn f1..f12; mods ctrl alt shift
 *   C <letter>          Control + letter (C c is Ctrl+C)
 *   SCROLL <n>          n lines back in time (negative: forward)
 *   RESIZE <rows> <cols>
 *   RESTART, EXIT
 *   UP / DOWN           buttons or a touch drag: scroll one line back / forward
 *   OK                  the assistant button: Enter, or restart when the shell has exited
 *   POWER               leave the terminal
 *   TAP <x> <y>         a short touch (raw panel coordinates), ignored by the terminal
 *   P|M|R <x> <y>       a finger pressed, moved, released (raw panel coordinates): what the touch service sends while /run/touch-raw exists;
 *                       the keyboard and the touch scrolling take these
 */
#ifndef TERM_INPUT_H
#define TERM_INPUT_H
#include <stdint.h>
#include "vt.h"

typedef enum { EV_TEXT, EV_KEY, EV_CTRL, EV_SCROLL, EV_RESIZE, EV_RESTART, EV_EXIT, EV_OK, EV_TAP, EV_TOUCH } EvType;

typedef struct {
	EvType type;
	char text[256];		/* EV_TEXT */
	VtKey key;		/* EV_KEY */
	int mods;
	uint32_t cp;		/* EV_CTRL */
	int n;			/* EV_SCROLL */
	int a, b;		/* EV_RESIZE rows, cols; EV_TAP and EV_TOUCH x, y (raw panel coordinates) */
	char touch;		/* EV_TOUCH: 'P' pressed, 'M' moved, 'R' released */
} Event;

typedef struct Input Input;

Input *input_open(const char *const *paths, int n);
int input_nfds(const Input *in);
int input_fd(const Input *in, int i);
int input_read(Input *in, int i, Event *out, int max);	/* reads what is available on fd i, returns the events parsed */
void input_close(Input *in);

#endif
