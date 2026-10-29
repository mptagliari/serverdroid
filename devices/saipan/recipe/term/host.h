/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * What a program that takes over the screen and the touch panel must set up and undo, shared by serverdroid-term and serverdroid-input:
 * the raw touch events of the touch service (a flag file) and the console's graphics mode (so that the kernel console draws nothing, not even
 * its blinking cursor, into the framebuffer the program is drawing on). Both are undone at exit, also on a signal-driven exit.
 */
#ifndef TERM_HOST_H
#define TERM_HOST_H

void host_raw_touch(int on);		/* /run/touch-raw: while it exists the touch service sends P/M/R events (tools/ctsd.c) */
void host_graphics(const char *tty);	/* hide the console cursor and switch the console to graphics mode; undone at exit or by host_text() */
void host_text(void);			/* back to the console's own mode now */

#endif
