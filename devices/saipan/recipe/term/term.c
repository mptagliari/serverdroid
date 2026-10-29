/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * serverdroid-term: a real interactive terminal for the phone's screen.
 *   vt.c      emulation (libvterm)        pty.c     the shell and its terminal      render_*.c  drawing      input.c  events
 * This file only joins them: it moves bytes between the PTY and the emulator, collects what changed (dirty rectangles), asks the renderer to
 * draw just that, and turns events into keys. Run it from the interface (it blocks until the terminal is left) or on its own.
 *
 *   serverdroid-term [--region X Y W H] [--fb DEV] [--tty DEV] [--font PSF] [--font-bold PSF] [--input PATH]... [--scrollback N] [--shell PATH]
 *                    [--dump FILE --rows R --cols C]   text renderer for tests instead of the framebuffer
 *                    [--once]                          leave when the shell exits, with its status
 *                    [--kbd X Y W H] [--no-kbd]        the on-screen keyboard (default 32 976 672 600; off with the text renderer)
 *                    [--touch-offset N]                raw touch coordinate + N = screen pixel (the panel's hidden margin, 32)
 *                    [--clipboard PATH]                where Copy also writes the text (default /run/clipboard)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "haptic.h"
#include "host.h"
#include "input.h"
#include "kbd.h"
#include "kbd_draw.h"
#include "pty.h"
#include "render.h"
#include "sel.h"
#include "vt.h"

static volatile sig_atomic_t g_quit;

static void on_signal(int s) { (void)s; g_quit = 1; }

typedef struct {
	Vt *vt;
	Pty pty;
	Renderer *rd;
	Input *in;
	const char *shell;
	int rows, cols;
	int *lo, *hi;			/* dirty column range per row, lo > hi when clean */
	int any_dirty;
	int sb_off;			/* lines scrolled back, 0 = live */
	int blink_on, cur_row, cur_col, cur_visible;
	int alive, once, exit_status;
	long last_flush, last_blink;
	Kbd *kbd;
	KbdPaint *paint;
	int haptic;
	int touch_off;
	int to_kbd;			/* the finger that is down belongs to the keyboard */
	int rx, ry;			/* where the terminal region starts on the screen, pixels */
	Sel sel;
	char *clip;			/* what Copy took, for Paste */
	const char *clip_path;		/* also written there, for other programs: /run/clipboard */
	struct { int down, mode, moved, end, item, x0, y0, anchor; long t0; } tp;	/* the finger that is down on the terminal */
} App;

enum { TM_SCROLL = 1, TM_BAR, TM_HANDLE, TM_EXTEND };	/* what that finger does: scroll, press a bar button, drag a handle, extend a new selection */
enum { LONG_PRESS_MS = 500, MOVE_SLOP = 14, HANDLE_REACH = 36, SEL_BG = 0x264f78 };

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void mark(App *a, int r0, int c0, int r1, int c1)
{
	if (r0 < 0) r0 = 0;
	if (c0 < 0) c0 = 0;
	if (r1 >= a->rows) r1 = a->rows - 1;
	if (c1 >= a->cols) c1 = a->cols - 1;
	for (int r = r0; r <= r1; r++) {
		if (c0 < a->lo[r]) a->lo[r] = c0;
		if (c1 > a->hi[r]) a->hi[r] = c1;
		a->any_dirty = 1;
	}
}

static void mark_all(App *a) { mark(a, 0, 0, a->rows - 1, a->cols - 1); }

static void alloc_dirty(App *a)
{
	free(a->lo);
	free(a->hi);
	a->lo = malloc(a->rows * sizeof(int));
	a->hi = malloc(a->rows * sizeof(int));
	for (int r = 0; r < a->rows; r++) { a->lo[r] = a->cols; a->hi[r] = -1; }
}

static void ev_damage(void *u, Rect r) { mark(u, r.r0, r.c0, r.r1, r.c1); }

static void ev_cursor(void *u, int row, int col, int visible)
{
	App *a = u;
	mark(a, a->cur_row, a->cur_col, a->cur_row, a->cur_col);	/* the old place */
	a->cur_row = row;
	a->cur_col = col;
	a->cur_visible = visible;
	a->blink_on = 1;
	a->last_blink = now_ms();
	mark(a, row, col, row, col);
}

static void ev_output(void *u, const char *bytes, size_t len)
{
	App *a = u;
	while (len && a->pty.master >= 0) {
		ssize_t w = write(a->pty.master, bytes, len);
		if (w > 0) { bytes += w; len -= w; }
		else if (w < 0 && errno == EAGAIN) { struct pollfd p = { a->pty.master, POLLOUT, 0 }; poll(&p, 1, 100); }
		else break;
	}
}

static void view_cell(App *a, int r, int c, Cell *out)
{
	int v = r - a->sb_off;

	if (v >= 0) { vt_cell(a->vt, v, c, out); return; }
	if (!vt_scrollback_cell(a->vt, -v - 1, c, out))
		*out = (Cell){ 0, DEFAULT_FG, DEFAULT_BG, 0, 1 };
}

static int cw_of(const App *a) { return a->rd->cell_w ? a->rd->cell_w : 16; }
static int ch_of(const App *a) { return a->rd->cell_h ? a->rd->cell_h : 32; }

static int sel_cell(void *u, int r, int c)
{
	Cell cell;

	view_cell(u, r, c, &cell);
	return cell.ch > ' ' ? (int)cell.ch : 0;
}

/* The bar and the handles, drawn over the cells after every redraw. */
static void draw_overlay(App *a)
{
	SelItem it[3];
	int n = sel_items(&a->sel, cw_of(a), ch_of(a), a->cols * cw_of(a), a->rows * ch_of(a), it);

	if (n) {
		Box all = { it[0].box.x, it[0].box.y, n * it[0].box.w, it[0].box.h };
		a->rd->fill_rect(a->rd, all, 0x2d3035, 18);
		for (int i = 0; i < n; i++) {
			Box b = { it[i].box.x + 3, it[i].box.y + 3, it[i].box.w - 6, it[i].box.h - 6 };
			if (a->tp.down && a->tp.mode == TM_BAR && a->tp.item == i)
				a->rd->fill_rect(a->rd, b, 0x4f545c, 14);
			a->rd->draw_text(a->rd, b.x + (b.w - 16 * (int)strlen(it[i].label)) / 2, b.y + (b.h - 32) / 2, it[i].label, 0xeceff4);
		}
	}
	if (a->sel.active)
		for (int e = 0; e < 2; e++) {
			Box h = sel_handle(&a->sel, e, cw_of(a), ch_of(a));
			a->rd->fill_rect(a->rd, (Box){ h.x + h.w / 2 - 2, h.y - 6, 4, 10 }, 0x3b6ea8, 0);	/* the stem up to the text */
			a->rd->fill_rect(a->rd, h, 0x3b6ea8, h.w / 2);
		}
}

/* For the text renderer's file (tests): the selection and where its buttons and handles are, in raw touch coordinates. */
static void status_selection(const App *a, char *out, size_t size)
{
	SelItem it[3];
	int n = sel_items(&a->sel, cw_of(a), ch_of(a), a->cols * cw_of(a), a->rows * ch_of(a), it), w = 0;

	if (a->sel.active) w += snprintf(out + w, size - w, " sel=%d,%d-%d,%d", a->sel.r0, a->sel.c0, a->sel.r1, a->sel.c1);
	else w += snprintf(out + w, size - w, " sel=none");
	for (int i = 0; i < n; i++)
		w += snprintf(out + w, size - w, " %s=%d,%d", it[i].act == SA_COPY ? "copy" : it[i].act == SA_PASTE ? "paste" : "all",
			      a->rx + it[i].box.x + it[i].box.w / 2 - a->touch_off, a->ry + it[i].box.y + it[i].box.h / 2 - a->touch_off);
	if (a->sel.active)
		for (int e = 0; e < 2; e++) {
			Box h = sel_handle(&a->sel, e, cw_of(a), ch_of(a));
			w += snprintf(out + w, size - w, " h%d=%d,%d", e, a->rx + h.x + h.w / 2 - a->touch_off, a->ry + h.y + h.h / 2 - a->touch_off);
		}
}

static void redraw(App *a)
{
	char status[640];
	int n;

	for (int r = 0; r < a->rows; r++) {
		for (int c = a->lo[r]; c <= a->hi[r]; c++) {
			Cell cell;
			int cursor = a->sb_off == 0 && a->cur_visible && a->blink_on && r == a->cur_row && c == a->cur_col;
			view_cell(a, r, c, &cell);
			if (sel_contains(&a->sel, r, c)) { cell.bg = SEL_BG; cell.fg = 0xffffff; cell.attrs &= ~A_REVERSE; }
			a->rd->draw_cell(a->rd, r, c, &cell, cursor);
		}
		a->lo[r] = a->cols;
		a->hi[r] = -1;
	}
	a->any_dirty = 0;
	snprintf(status, sizeof(status), "rows=%d cols=%d cursor=%d,%d visible=%d alt=%d scrollback=%zu offset=%d alive=%d", a->rows, a->cols,
		 a->cur_row, a->cur_col, a->cur_visible, vt_in_altscreen(a->vt), vt_scrollback_lines(a->vt), a->sb_off, a->alive);
	draw_overlay(a);
	n = strlen(status);
	status_selection(a, status + n, sizeof(status) - n);
	a->rd->flush(a->rd, status);
	a->last_flush = now_ms();
}

static void feed_message(App *a, const char *s) { vt_write(a->vt, s, strlen(s)); }

static int start_shell(App *a)
{
	if (pty_spawn(&a->pty, a->shell, a->rows, a->cols) < 0)
		return -1;
	pty_resize(&a->pty, a->rows, a->cols, a->rd->cell_w * a->cols, a->rd->cell_h * a->rows);
	a->alive = 1;
	return 0;
}

static void child_gone(App *a)
{
	char msg[96];

	if (!a->alive)
		return;
	a->alive = 0;
	a->exit_status = WIFEXITED(a->pty.status) ? WEXITSTATUS(a->pty.status) : 128 + WTERMSIG(a->pty.status);
	if (a->pty.master >= 0) { close(a->pty.master); a->pty.master = -1; }
	snprintf(msg, sizeof(msg), "\r\n[the shell exited with status %d]\r\n[OK: start another   POWER: leave]\r\n", a->exit_status);
	feed_message(a, msg);
	if (a->once)
		g_quit = 1;
}

static void restart_shell(App *a);

static void scroll_to(App *a, int off)
{
	int max = vt_in_altscreen(a->vt) ? 0 : (int)vt_scrollback_lines(a->vt);

	if (off < 0) off = 0;
	if (off > max) off = max;
	if (off != a->sb_off) { a->sb_off = off; sel_clear(&a->sel); mark_all(a); }
}

static void mark_all(App *a);

static void restart_shell(App *a)
{
	if (start_shell(a) == 0) {
		feed_message(a, "\033c");	/* a fresh screen */
		scroll_to(a, 0);
		mark_all(a);
	}
}

static uint32_t next_cp(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t cp;
	int n;

	if (p[0] < 0x80) { cp = p[0]; n = 1; }
	else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { cp = ((p[0] & 31) << 6) | (p[1] & 63); n = 2; }
	else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { cp = ((p[0] & 15) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); n = 3; }
	else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
		cp = ((p[0] & 7) << 18) | ((p[1] & 63) << 12) | ((p[2] & 63) << 6) | (p[3] & 63); n = 4; }
	else { cp = 0xFFFD; n = 1; }
	*s += n;
	return cp;
}

static void resize(App *a, int rows, int cols)
{
	if (rows < 2) rows = 2;
	if (cols < 8) cols = 8;
	if (rows > a->rd->rows) rows = a->rd->rows;
	if (cols > a->rd->cols) cols = a->rd->cols;
	if (rows == a->rows && cols == a->cols)
		return;
	a->rows = rows;
	a->cols = cols;
	alloc_dirty(a);
	vt_resize(a->vt, rows, cols);
	pty_resize(&a->pty, rows, cols, a->rd->cell_w * cols, a->rd->cell_h * rows);	/* TIOCSWINSZ: the shell gets SIGWINCH */
	a->sb_off = 0;
	a->rd->clear(a->rd);
	mark_all(a);
}

static void handle(App *a, const Event *e);

static void kbd_apply(App *a, const KbdOut *o)
{
	for (int i = 0; i < o->nev; i++)
		handle(a, &o->ev[i]);
	if (o->buzz)
		haptic_pulse(a->haptic);
	if (o->dirty && a->paint)
		kbd_paint(a->paint, a->kbd);
}

static void cell_at(const App *a, int lx, int ly, int *row, int *col)
{
	int r = ly / ch_of(a), c = lx / cw_of(a);

	*row = r < 0 ? 0 : r >= a->rows ? a->rows - 1 : r;
	*col = c < 0 ? 0 : c >= a->cols ? a->cols - 1 : c;
}

static void set_clipboard(App *a, char *text)
{
	int fd;

	free(a->clip);
	a->clip = text;
	fd = open(a->clip_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);	/* for the other programs on the phone */
	if (fd >= 0) {
		(void)!write(fd, text, strlen(text));
		close(fd);
	}
}

static void bar_action(App *a, int act)
{
	switch (act) {
	case SA_COPY:
		set_clipboard(a, sel_text(&a->sel, sel_cell, a, a->cols));
		sel_clear(&a->sel);
		break;
	case SA_PASTE: {
		char *own = NULL;
		const char *text = a->clip;
		if (!text || !*text) {			/* another program may have put text in the clipboard file */
			FILE *f = fopen(a->clip_path, "r");
			if (f) {
				own = calloc(1, 4097);
				if (own && fread(own, 1, 4096, f) > 0) text = own;
				fclose(f);
			}
		}
		if (text && *text && a->alive) { scroll_to(a, 0); ev_output(a, text, strlen(text)); }
		free(own);
		sel_clear(&a->sel);
		break;
	}
	case SA_ALL:
		sel_all(&a->sel, a->rows, a->cols);
		break;
	}
	mark_all(a);
}

/* A long press on the terminal: select the word under the finger (Android style); on a blank only the Paste button. */
static void long_press(App *a)
{
	int r, c;

	cell_at(a, a->tp.x0 - a->rx, a->tp.y0 - a->ry, &r, &c);
	sel_word(&a->sel, sel_cell, a, a->cols, r, c);
	if (a->sel.active) { a->tp.mode = TM_EXTEND; a->tp.end = 1; }	/* keep dragging: the end follows the finger */
	else a->tp.mode = 0;
	haptic_pulse(a->haptic);
	mark_all(a);
}

static int long_press_timeout(const App *a, long now)	/* ms until a long press fires, -1 when none is pending */
{
	if (!a->tp.down || a->tp.mode != TM_SCROLL || a->tp.moved || a->to_kbd)
		return -1;
	return a->tp.t0 + LONG_PRESS_MS - now < 1 ? 1 : (int)(a->tp.t0 + LONG_PRESS_MS - now);
}

/* A finger of the touch service (raw panel coordinates). One that goes down on the keyboard stays with it. On the terminal: a drag scrolls (a
 * console row of movement is a line of scrollback), a long press selects, the bar buttons and the handles of a selection take the touch first. */
static void touch(App *a, const Event *e)
{
	int x = e->a + a->touch_off, y = e->b + a->touch_off, lx = x - a->rx, ly = y - a->ry, ch = ch_of(a);
	KbdOut o = { .nev = 0 };

	if (e->touch == 'P') {
		a->to_kbd = a->kbd && kbd_inside(a->kbd, x, y);
		if (!a->to_kbd) {
			SelItem it[3];
			int n = sel_items(&a->sel, cw_of(a), ch, a->cols * cw_of(a), a->rows * ch, it);

			memset(&a->tp, 0, sizeof(a->tp));
			a->tp.down = 1;
			a->tp.mode = TM_SCROLL;
			a->tp.x0 = x;
			a->tp.y0 = y;
			a->tp.anchor = y;
			a->tp.t0 = now_ms();
			for (int i = 0; i < n; i++)
				if (lx >= it[i].box.x && lx < it[i].box.x + it[i].box.w && ly >= it[i].box.y && ly < it[i].box.y + it[i].box.h) {
					a->tp.mode = TM_BAR;
					a->tp.item = i;
					mark_all(a);			/* the pressed button lights up */
				}
			if (a->tp.mode == TM_SCROLL && a->sel.active)
				for (int end = 0; end < 2; end++) {
					Box h = sel_handle(&a->sel, end, cw_of(a), ch);
					int dx = lx - (h.x + h.w / 2), dy = ly - (h.y + h.h / 2);
					if (dx * dx + dy * dy <= HANDLE_REACH * HANDLE_REACH) { a->tp.mode = TM_HANDLE; a->tp.end = end; }
				}
			if (a->tp.mode == TM_SCROLL && (a->sel.active || a->sel.bar)) {	/* a touch elsewhere ends the selection */
				sel_clear(&a->sel);
				mark_all(a);
			}
		}
	}
	if (a->to_kbd) {
		kbd_touch(a->kbd, e->touch, x, y, now_ms(), &o);
		kbd_apply(a, &o);
		if (e->touch == 'R')
			a->to_kbd = 0;
		return;
	}
	if (!a->tp.down)
		return;
	if (e->touch == 'M') {
		if (a->tp.mode == TM_HANDLE || a->tp.mode == TM_EXTEND) {
			int r, c;
			cell_at(a, lx, ly - (a->tp.mode == TM_HANDLE ? 28 : 0), &r, &c);	/* a handle hangs below the text: aim with the text, not the dot */
			sel_move(&a->sel, &a->tp.end, r, c);
			mark_all(a);
		} else if (a->tp.mode == TM_SCROLL) {
			int dx = x - a->tp.x0, dy = y - a->tp.y0;
			if (dx < -MOVE_SLOP || dx > MOVE_SLOP || dy < -MOVE_SLOP || dy > MOVE_SLOP)
				a->tp.moved = 1;
			if (a->tp.moved) {
				while (y - a->tp.anchor <= -ch) { scroll_to(a, a->sb_off - 1); a->tp.anchor -= ch; }	/* finger up: later lines */
				while (y - a->tp.anchor >= ch) { scroll_to(a, a->sb_off + 1); a->tp.anchor += ch; }
			}
		}
	} else if (e->touch == 'R') {
		if (a->tp.mode == TM_BAR) {
			SelItem it[3];
			int n = sel_items(&a->sel, cw_of(a), ch, a->cols * cw_of(a), a->rows * ch, it), i = a->tp.item;
			if (i < n && lx >= it[i].box.x && lx < it[i].box.x + it[i].box.w && ly >= it[i].box.y && ly < it[i].box.y + it[i].box.h)
				bar_action(a, it[i].act);
			else
				mark_all(a);
		}
		a->tp.down = 0;
		a->tp.mode = 0;
	}
}

static void handle(App *a, const Event *e)
{
	switch (e->type) {
	case EV_TEXT:
		scroll_to(a, 0);
		for (const char *s = e->text; *s;)
			vt_char(a->vt, next_cp(&s), e->mods);
		break;
	case EV_KEY:
		scroll_to(a, 0);
		vt_key(a->vt, e->key, e->mods);
		break;
	case EV_CTRL:
		scroll_to(a, 0);
		vt_char(a->vt, e->cp, MOD_CTRL);
		break;
	case EV_SCROLL:
		scroll_to(a, a->sb_off + e->n);
		break;
	case EV_RESIZE:
		resize(a, e->a, e->b);
		break;
	case EV_OK:
		if (a->alive) { scroll_to(a, 0); vt_key(a->vt, VK_ENTER, 0); }
		else restart_shell(a);			/* the shell has exited: OK starts another */
		break;
	case EV_RESTART:
		if (!a->alive)
			restart_shell(a);
		break;
	case EV_EXIT:
		g_quit = 1;
		break;
	case EV_TAP:
		break;		/* a tap is the touch service's own summary of a touch; the keyboard works from the raw ones */
	case EV_TOUCH:
		touch(a, e);
		break;
	}
}

int main(int argc, char **argv)
{
	const char *fbdev = "/dev/fb0", *tty = "/dev/tty1", *dump = NULL, *font = "/usr/share/consolefonts/ter-132n.psf.gz",
		   *font_bold = "/usr/share/consolefonts/ter-132b.psf.gz", *inputs[4] = { "/run/keys.fifo", "/run/term.in" };
	int nin = 0, use_kbd = 1, kx = 32, ky = 976, kw = 672, kh = 600, rx = 32, ry = 64, rw = 672, rh = 896, rows = 24, cols = 80, scrollback = 2000;
	struct sigaction sa = { .sa_handler = on_signal };
	App a = { .shell = "/bin/ash", .touch_off = 32, .haptic = -1, .clip_path = "/run/clipboard" };
	VtEvents ev = { .damage = ev_damage, .cursor = ev_cursor, .output = ev_output, .user = &a };

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--region") && i + 4 < argc) { rx = atoi(argv[i + 1]); ry = atoi(argv[i + 2]); rw = atoi(argv[i + 3]); rh = atoi(argv[i + 4]); i += 4; }
		else if (!strcmp(argv[i], "--fb") && i + 1 < argc) fbdev = argv[++i];
		else if (!strcmp(argv[i], "--tty") && i + 1 < argc) tty = argv[++i];
		else if (!strcmp(argv[i], "--font") && i + 1 < argc) font = argv[++i];
		else if (!strcmp(argv[i], "--font-bold") && i + 1 < argc) font_bold = argv[++i];
		else if (!strcmp(argv[i], "--input") && i + 1 < argc) { if (nin < 4) inputs[nin++] = argv[i + 1]; i++; }
		else if (!strcmp(argv[i], "--scrollback") && i + 1 < argc) scrollback = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--shell") && i + 1 < argc) a.shell = argv[++i];
		else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
		else if (!strcmp(argv[i], "--rows") && i + 1 < argc) rows = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--cols") && i + 1 < argc) cols = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--once")) a.once = 1;
		else if (!strcmp(argv[i], "--kbd") && i + 4 < argc) { kx = atoi(argv[i + 1]); ky = atoi(argv[i + 2]); kw = atoi(argv[i + 3]); kh = atoi(argv[i + 4]); i += 4; use_kbd = 2; }
		else if (!strcmp(argv[i], "--no-kbd")) use_kbd = 0;
		else if (!strcmp(argv[i], "--clipboard") && i + 1 < argc) a.clip_path = argv[++i];
		else if (!strcmp(argv[i], "--touch-offset") && i + 1 < argc) a.touch_off = atoi(argv[++i]);
		else { fprintf(stderr, "usage: see the head of term.c\n"); return 2; }
	}
	if (!nin)
		nin = 2;		/* the interface's tokens and the test FIFO */

	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	if (dump) {
		a.rd = render_dump_open(dump, rows, cols);
	} else {
		a.rd = render_fb_open(fbdev, font, font_bold, tty, rx, ry, rw, rh);
		if (a.rd)
			host_graphics(tty);	/* the console must not draw into the framebuffer while the terminal does */
	}
	if (!a.rd) {
		fprintf(stderr, "serverdroid-term: cannot open the renderer\n");
		return 1;
	}
	a.rx = rx;
	a.ry = ry;
	a.rows = a.rd->rows;
	a.cols = a.rd->cols;
	a.cur_visible = 1;
	a.blink_on = 1;
	alloc_dirty(&a);
	a.vt = vt_new(a.rows, a.cols, scrollback, &ev);
	a.in = input_open((const char *const *)inputs, nin);
	a.rd->clear(a.rd);
	if (!a.vt || !a.in || start_shell(&a) < 0) {
		host_text();
		fprintf(stderr, "serverdroid-term: cannot start\n");
		return 1;
	}
	a.last_blink = now_ms();
	mark_all(&a);
	if (use_kbd && (use_kbd == 2 || !dump)) {
		Box area = { kx, ky, kw, kh };

		a.kbd = kbd_new(area);
		if (!dump) {
			a.paint = kbd_paint_open(fbdev, font, area);
			a.haptic = haptic_open(20);
			host_raw_touch(1);
			if (a.paint)
				kbd_paint(a.paint, a.kbd);
		}
	}

	while (!g_quit) {
		struct pollfd pf[1 + 4];
		int n = 0, timeout, master_idx = -1;
		long now = now_ms();

		if (a.alive && a.pty.master >= 0) { pf[n] = (struct pollfd){ a.pty.master, POLLIN, 0 }; master_idx = n++; }
		for (int i = 0; i < input_nfds(a.in); i++)
			pf[n++] = (struct pollfd){ input_fd(a.in, i), POLLIN, 0 };
		timeout = a.any_dirty ? 16 - (int)(now - a.last_flush) : 530 - (int)(now - a.last_blink);
		if (a.kbd) {
			int kt = kbd_timeout(a.kbd, now);
			if (kt >= 0 && kt < timeout) timeout = kt;
		}
		{
			int lt = long_press_timeout(&a, now);
			if (lt >= 0 && lt < timeout) timeout = lt;
		}
		if (timeout < 1) timeout = 1;
		poll(pf, n, timeout);

		if (master_idx >= 0 && (pf[master_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
			char buf[8192];
			for (;;) {
				ssize_t r = read(a.pty.master, buf, sizeof(buf));
				if (r > 0) vt_write(a.vt, buf, r);
				else if (r < 0 && errno == EAGAIN) break;
				else { pty_reap(&a.pty); if (a.pty.pid == 0) child_gone(&a); break; }	/* end of file or EIO: the shell is gone */
			}
		}
		if (a.alive && pty_reap(&a.pty))
			child_gone(&a);
		for (int i = 0; i < input_nfds(a.in); i++) {
			int idx = n - input_nfds(a.in) + i;
			if (pf[idx].revents & POLLIN) {
				Event evs[32];
				int got = input_read(a.in, i, evs, 32);
				for (int k = 0; k < got; k++)
					handle(&a, &evs[k]);
			}
		}
		now = now_ms();
		if (long_press_timeout(&a, now) == 1 && now - a.tp.t0 >= LONG_PRESS_MS)
			long_press(&a);
		if (a.kbd) {
			KbdOut ko = { .nev = 0 };
			kbd_tick(a.kbd, now, &ko);
			kbd_apply(&a, &ko);
		}
		if (now - a.last_blink >= 530) {
			a.blink_on = !a.blink_on;
			a.last_blink = now;
			mark(&a, a.cur_row, a.cur_col, a.cur_row, a.cur_col);
		}
		if (a.any_dirty && now - a.last_flush >= 16)
			redraw(&a);
	}
	redraw(&a);
	pty_terminate(&a.pty);		/* the shell and everything it started: no orphans */
	host_text();
	a.rd->close(a.rd);
	vt_free(a.vt);
	input_close(a.in);
	if (a.kbd) {
		host_raw_touch(0);
		kbd_paint_close(a.paint);
		kbd_free(a.kbd);
		haptic_close(a.haptic);
	}
	return a.once && !a.alive ? a.exit_status : 0;
}
