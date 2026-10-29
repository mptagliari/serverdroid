#include <stdlib.h>
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <string.h>
#include <vterm.h>
#include "vt.h"

typedef struct { Cell *cells; int cols; } SbLine;

struct Vt {
	VTerm *vt;
	VTermScreen *screen;
	VtEvents ev;
	int rows, cols;
	int cur_row, cur_col, cur_visible, altscreen;
	SbLine *sb;		/* ring of scrollback lines, oldest at sb_start */
	size_t sb_cap, sb_len, sb_start;
};

static uint32_t rgb_of(Vt *v, VTermColor c)
{
	if (VTERM_COLOR_IS_DEFAULT_FG(&c))
		return DEFAULT_FG;
	if (VTERM_COLOR_IS_DEFAULT_BG(&c))
		return DEFAULT_BG;
	vterm_screen_convert_color_to_rgb(v->screen, &c);
	return ((uint32_t)c.rgb.red << 16) | ((uint32_t)c.rgb.green << 8) | c.rgb.blue;
}

static void convert(Vt *v, const VTermScreenCell *in, Cell *out)
{
	out->ch = in->chars[0];
	out->width = in->width > 1 ? 2 : 1;
	out->attrs = (in->attrs.bold ? A_BOLD : 0) | (in->attrs.underline ? A_UNDERLINE : 0) | (in->attrs.italic ? A_ITALIC : 0) |
		     (in->attrs.blink ? A_BLINK : 0) | (in->attrs.reverse ? A_REVERSE : 0) | (in->attrs.strike ? A_STRIKE : 0);
	out->fg = rgb_of(v, in->fg);
	out->bg = rgb_of(v, in->bg);
}

static int cb_damage(VTermRect r, void *u)
{
	Vt *v = u;
	if (v->ev.damage)
		v->ev.damage(v->ev.user, (Rect){ r.start_row, r.start_col, r.end_row - 1, r.end_col - 1 });
	return 1;
}

static int cb_movecursor(VTermPos pos, VTermPos old, int visible, void *u)
{
	Vt *v = u;
	(void)old;
	v->cur_row = pos.row;
	v->cur_col = pos.col;
	v->cur_visible = visible;
	if (v->ev.cursor)
		v->ev.cursor(v->ev.user, pos.row, pos.col, visible);
	return 1;
}

static int cb_settermprop(VTermProp prop, VTermValue *val, void *u)
{
	Vt *v = u;
	if (prop == VTERM_PROP_CURSORVISIBLE) {
		v->cur_visible = val->boolean;
		if (v->ev.cursor)
			v->ev.cursor(v->ev.user, v->cur_row, v->cur_col, v->cur_visible);
	} else if (prop == VTERM_PROP_ALTSCREEN) {
		v->altscreen = val->boolean;
	}
	return 1;
}

static int cb_bell(void *u)
{
	Vt *v = u;
	if (v->ev.bell)
		v->ev.bell(v->ev.user);
	return 1;
}

static int cb_sb_pushline(int cols, const VTermScreenCell *cells, void *u)
{
	Vt *v = u;
	SbLine *line;

	if (!v->sb_cap)
		return 0;
	if (v->sb_len == v->sb_cap) {		/* full: drop the oldest */
		line = &v->sb[v->sb_start];
		free(line->cells);
		v->sb_start = (v->sb_start + 1) % v->sb_cap;
		v->sb_len--;
	}
	line = &v->sb[(v->sb_start + v->sb_len) % v->sb_cap];
	line->cells = calloc(cols, sizeof(Cell));
	if (!line->cells)
		return 0;
	line->cols = cols;
	for (int i = 0; i < cols; i++)
		convert(v, &cells[i], &line->cells[i]);
	v->sb_len++;
	return 1;
}

static int cb_sb_popline(int cols, VTermScreenCell *cells, void *u)
{
	(void)cols; (void)cells; (void)u;
	return 0;		/* growing the window does not pull lines back from the scrollback */
}

static int cb_sb_clear(void *u)
{
	Vt *v = u;
	for (size_t i = 0; i < v->sb_len; i++)
		free(v->sb[(v->sb_start + i) % v->sb_cap].cells);
	v->sb_len = 0;
	v->sb_start = 0;
	return 1;
}

static const VTermScreenCallbacks screen_cb = {
	.damage = cb_damage, .movecursor = cb_movecursor, .settermprop = cb_settermprop, .bell = cb_bell,
	.sb_pushline = cb_sb_pushline, .sb_popline = cb_sb_popline, .sb_clear = cb_sb_clear,
};

static void cb_output(const char *s, size_t len, void *u)
{
	Vt *v = u;
	if (v->ev.output)
		v->ev.output(v->ev.user, s, len);
}

Vt *vt_new(int rows, int cols, size_t max_scrollback_lines, const VtEvents *ev)
{
	Vt *v = calloc(1, sizeof(*v));
	VTermState *state;
	VTermColor fg, bg;

	if (!v)
		return NULL;
	v->ev = *ev;
	v->rows = rows;
	v->cols = cols;
	v->cur_visible = 1;
	v->sb_cap = max_scrollback_lines;
	if (v->sb_cap)
		v->sb = calloc(v->sb_cap, sizeof(SbLine));
	v->vt = vterm_new(rows, cols);
	vterm_set_utf8(v->vt, 1);
	vterm_output_set_callback(v->vt, cb_output, v);
	state = vterm_obtain_state(v->vt);
	vterm_color_rgb(&fg, DEFAULT_FG >> 16, (DEFAULT_FG >> 8) & 255, DEFAULT_FG & 255);
	vterm_color_rgb(&bg, DEFAULT_BG >> 16, (DEFAULT_BG >> 8) & 255, DEFAULT_BG & 255);
	vterm_state_set_default_colors(state, &fg, &bg);
	v->screen = vterm_obtain_screen(v->vt);
	vterm_screen_set_callbacks(v->screen, &screen_cb, v);
	vterm_screen_enable_altscreen(v->screen, 1);
	vterm_screen_set_damage_merge(v->screen, VTERM_DAMAGE_ROW);
	vterm_screen_reset(v->screen, 1);
	return v;
}

void vt_free(Vt *v)
{
	if (!v)
		return;
	cb_sb_clear(v);
	free(v->sb);
	vterm_free(v->vt);
	free(v);
}

void vt_write(Vt *v, const char *bytes, size_t len)
{
	vterm_input_write(v->vt, bytes, len);
	vterm_screen_flush_damage(v->screen);
}

void vt_resize(Vt *v, int rows, int cols)
{
	v->rows = rows;
	v->cols = cols;
	vterm_set_size(v->vt, rows, cols);
	vterm_screen_flush_damage(v->screen);
}

static const struct { VtKey k; VTermKey vk; } keymap[] = {
	{ VK_ENTER, VTERM_KEY_ENTER }, { VK_TAB, VTERM_KEY_TAB }, { VK_BACKSPACE, VTERM_KEY_BACKSPACE },
	{ VK_ESCAPE, VTERM_KEY_ESCAPE }, { VK_UP, VTERM_KEY_UP }, { VK_DOWN, VTERM_KEY_DOWN },
	{ VK_LEFT, VTERM_KEY_LEFT }, { VK_RIGHT, VTERM_KEY_RIGHT }, { VK_INS, VTERM_KEY_INS },
	{ VK_DEL, VTERM_KEY_DEL }, { VK_HOME, VTERM_KEY_HOME }, { VK_END, VTERM_KEY_END },
	{ VK_PAGEUP, VTERM_KEY_PAGEUP }, { VK_PAGEDOWN, VTERM_KEY_PAGEDOWN },
};

void vt_key(Vt *v, VtKey key, int mods)
{
	VTermModifier m = (mods & MOD_SHIFT ? VTERM_MOD_SHIFT : 0) | (mods & MOD_ALT ? VTERM_MOD_ALT : 0) | (mods & MOD_CTRL ? VTERM_MOD_CTRL : 0);

	if (key >= VK_F1 && key < VK_F1 + 12) {
		vterm_keyboard_key(v->vt, VTERM_KEY_FUNCTION(key - VK_F1 + 1), m);
		return;
	}
	for (size_t i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++)
		if (keymap[i].k == key) {
			vterm_keyboard_key(v->vt, keymap[i].vk, m);
			return;
		}
}

void vt_char(Vt *v, uint32_t cp, int mods)
{
	VTermModifier m = (mods & MOD_SHIFT ? VTERM_MOD_SHIFT : 0) | (mods & MOD_ALT ? VTERM_MOD_ALT : 0) | (mods & MOD_CTRL ? VTERM_MOD_CTRL : 0);
	vterm_keyboard_unichar(v->vt, cp, m);
}

void vt_cell(Vt *v, int row, int col, Cell *out)
{
	VTermScreenCell c;
	VTermPos p = { row, col };

	if (row < 0 || row >= v->rows || col < 0 || col >= v->cols || !vterm_screen_get_cell(v->screen, p, &c)) {
		memset(out, 0, sizeof(*out));
		out->fg = DEFAULT_FG;
		out->bg = DEFAULT_BG;
		out->width = 1;
		return;
	}
	convert(v, &c, out);
	if (c.chars[0] == (uint32_t)-1)		/* the right half of a wide character */
		out->ch = 0;
}

size_t vt_scrollback_lines(const Vt *v)
{
	return v->sb_len;
}

int vt_scrollback_cell(const Vt *v, size_t from_newest, int col, Cell *out)
{
	const SbLine *line;

	if (from_newest >= v->sb_len)
		return 0;
	line = &v->sb[(v->sb_start + v->sb_len - 1 - from_newest) % v->sb_cap];
	if (col < 0 || col >= line->cols)
		return 0;
	*out = line->cells[col];
	return 1;
}

void vt_cursor(const Vt *v, int *row, int *col, int *visible)
{
	*row = v->cur_row;
	*col = v->cur_col;
	*visible = v->cur_visible;
}

int vt_in_altscreen(const Vt *v) { return v->altscreen; }
int vt_rows(const Vt *v) { return v->rows; }
int vt_cols(const Vt *v) { return v->cols; }
