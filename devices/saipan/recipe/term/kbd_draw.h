/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * The keyboard's painter: draws the state of a Kbd (kbd.h) into its own rectangle of the framebuffer, never outside it. The picture is composed
 * in memory and copied to the screen in one pass, so a key press does not flicker. It uses the same PSF font as the terminal, scaled 2x for
 * the one-character labels.
 */
#ifndef TERM_KBD_DRAW_H
#define TERM_KBD_DRAW_H
#include "kbd.h"

typedef struct KbdPaint KbdPaint;

KbdPaint *kbd_paint_open(const char *fbdev, const char *font, Box clip);	/* NULL on failure */
void kbd_paint(KbdPaint *p, const Kbd *k);
void kbd_paint_close(KbdPaint *p);

/* Optional header for a full-screen entry: a prompt line and a text field (the tail of the text that fits, a cursor mark, bullets when masked)
 * at the top of the clip rectangle. kbd_paint draws it with the keyboard. */
void kbd_paint_header(KbdPaint *p, const char *prompt, const char *text, int masked);
Box kbd_paint_field(const KbdPaint *p);		/* where the text field is, for hit tests */

#endif
