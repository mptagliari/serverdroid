/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * Terminal emulation: a thin wrapper around libvterm (the only file that includes <vterm.h>).
 * It feeds bytes from the child to the emulator, turns keys and characters into the byte sequences libvterm produces for the child, keeps a
 * limited scrollback, and reports what changed on the screen. It knows nothing about pixels, framebuffers or the PTY.
 */
#ifndef TERM_VT_H
#define TERM_VT_H
#include <stddef.h>
#include "cell.h"

typedef enum {
	VK_ENTER = 1, VK_TAB, VK_BACKSPACE, VK_ESCAPE, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
	VK_INS, VK_DEL, VK_HOME, VK_END, VK_PAGEUP, VK_PAGEDOWN,
	VK_F1 = 100	/* VK_F1 + n - 1 for Fn, n = 1..12 */
} VtKey;

enum { MOD_SHIFT = 1, MOD_ALT = 2, MOD_CTRL = 4 };

typedef struct {
	void (*damage)(void *user, Rect r);			/* cells that changed on the live screen */
	void (*cursor)(void *user, int row, int col, int visible);
	void (*bell)(void *user);
	void (*output)(void *user, const char *bytes, size_t len);	/* bytes for the child (keys, answers to queries) */
	void *user;
} VtEvents;

typedef struct Vt Vt;

Vt *vt_new(int rows, int cols, size_t max_scrollback_lines, const VtEvents *ev);
void vt_free(Vt *vt);
void vt_write(Vt *vt, const char *bytes, size_t len);		/* bytes read from the child */
void vt_resize(Vt *vt, int rows, int cols);
void vt_key(Vt *vt, VtKey key, int mods);
void vt_char(Vt *vt, uint32_t codepoint, int mods);
void vt_cell(Vt *vt, int row, int col, Cell *out);		/* live screen */
size_t vt_scrollback_lines(const Vt *vt);
int vt_scrollback_cell(const Vt *vt, size_t line_from_newest, int col, Cell *out);	/* 0 when there is no such cell */
void vt_cursor(const Vt *vt, int *row, int *col, int *visible);
int vt_in_altscreen(const Vt *vt);
int vt_rows(const Vt *vt);
int vt_cols(const Vt *vt);

#endif
