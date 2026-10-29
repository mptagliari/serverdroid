/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * serverdroid-input: ask for a line of text with the on-screen keyboard, for the interface (the Wi-Fi password, a command). It takes the whole
 * screen: a prompt, a text field and the keyboard of kbd.c at the bottom. The text goes to standard output, without a newline.
 *   serverdroid-input [--prompt TEXT] [--initial TEXT] [--password] [--timeout SECONDS]
 *                     [--input FIFO] [--kbd X Y W H] [--touch-offset N] [--no-paint]    (the last four are for tests)
 * Exit status: 0 the text was confirmed (the Enter bar, or the assistant button), 1 cancelled (the power button, ESC, or no input for the timeout).
 * Tapping the text field shows or hides a masked text. The touches come from the touch service in its raw mode, like the terminal's.
 */
#define _GNU_SOURCE
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "haptic.h"
#include "host.h"
#include "input.h"
#include "kbd.h"
#include "kbd_draw.h"

static volatile sig_atomic_t g_quit;
static void on_signal(int s) { (void)s; g_quit = 1; }

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

typedef struct {
	char text[512];
	size_t len;
	int done, ok, masked, dirty;
} Line;

static void append(Line *l, const char *s)
{
	size_t n = strlen(s);

	if (l->len + n < sizeof(l->text)) {
		memcpy(l->text + l->len, s, n + 1);
		l->len += n;
		l->dirty = 1;
	}
}

static void erase_last(Line *l)
{
	if (!l->len)
		return;
	do l->len--; while (l->len && ((unsigned char)l->text[l->len] & 0xC0) == 0x80);
	l->text[l->len] = 0;
	l->dirty = 1;
}

static void handle(Line *l, const Event *e)
{
	switch (e->type) {
	case EV_TEXT:
		if (!(e->mods & ~MOD_SHIFT)) append(l, e->text);
		break;
	case EV_KEY:
		if (e->key == VK_BACKSPACE) erase_last(l);
		else if (e->key == VK_ENTER) { l->done = 1; l->ok = 1; }
		else if (e->key == VK_ESCAPE) l->done = 1;
		break;
	case EV_OK:
		l->done = 1;
		l->ok = 1;
		break;
	case EV_EXIT:
		l->done = 1;
		break;
	default:
		break;			/* Ctrl characters, scrolling, taps: not for a line of text */
	}
}

int main(int argc, char **argv)
{
	const char *prompt = "", *initial = "", *fbdev = "/dev/fb0", *tty = "/dev/tty1", *inputs[1] = { "/run/keys.fifo" };
	const char *font = "/usr/share/consolefonts/ter-132n.psf.gz";
	int timeout_s = 120, paint_on = 1, touch_off = 32;
	Box kb = { 32, 976, 672, 600 };
	Line line = { .text = "" };
	struct sigaction sa = { .sa_handler = on_signal };
	Kbd *k;
	KbdPaint *paint = NULL;
	Input *in;
	int haptic = -1;
	long last_input;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--prompt") && i + 1 < argc) prompt = argv[++i];
		else if (!strcmp(argv[i], "--initial") && i + 1 < argc) initial = argv[++i];
		else if (!strcmp(argv[i], "--password")) line.masked = 1;
		else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout_s = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--input") && i + 1 < argc) inputs[0] = argv[++i];
		else if (!strcmp(argv[i], "--kbd") && i + 4 < argc) { kb = (Box){ atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]), atoi(argv[i + 4]) }; i += 4; }
		else if (!strcmp(argv[i], "--touch-offset") && i + 1 < argc) touch_off = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--no-paint")) paint_on = 0;
		else { fprintf(stderr, "usage: see the head of input_app.c\n"); return 2; }
	}
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	append(&line, initial);
	k = kbd_new(kb);
	in = input_open(inputs, 1);
	if (!k || !in) {
		fprintf(stderr, "serverdroid-input: cannot start\n");
		return 2;
	}
	if (paint_on) {
		paint = kbd_paint_open(fbdev, font, (Box){ 32, 64, 672, kb.y + kb.h - 64 });
		if (!paint) {
			fprintf(stderr, "serverdroid-input: cannot open the framebuffer\n");
			return 2;
		}
		host_graphics(tty);
		host_raw_touch(1);
		haptic = haptic_open(20);
		kbd_paint_header(paint, prompt, line.text, line.masked);
		kbd_paint(paint, k);
	}
	last_input = now_ms();
	while (!g_quit && !line.done) {
		struct pollfd pf[2];
		int n = 0, timeout = 1000, kt;
		long now = now_ms();
		KbdOut ko = { .nev = 0 };

		for (int i = 0; i < input_nfds(in); i++)
			pf[n++] = (struct pollfd){ input_fd(in, i), POLLIN, 0 };
		kt = kbd_timeout(k, now);
		if (kt >= 0 && kt < timeout) timeout = kt;
		poll(pf, n, timeout);
		for (int i = 0; i < input_nfds(in); i++) {
			Event evs[32];
			int got = pf[i].revents & POLLIN ? input_read(in, i, evs, 32) : 0;
			for (int j = 0; j < got; j++) {
				const Event *e = &evs[j];
				last_input = now_ms();
				if (e->type == EV_TOUCH) {
					int x = e->a + touch_off, y = e->b + touch_off;
					KbdOut o = { .nev = 0 };
					if (paint && e->touch == 'P' && !kbd_inside(k, x, y)) {	/* a tap on the text field shows or hides the text */
						Box f = kbd_paint_field(paint);
						if (x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h) { line.masked = !line.masked; line.dirty = 1; }
					}
					kbd_touch(k, e->touch, x, y, now_ms(), &o);
					for (int q = 0; q < o.nev; q++) handle(&line, &o.ev[q]);
					if (o.buzz) haptic_pulse(haptic);
					if (o.dirty || line.dirty) { if (paint) { kbd_paint_header(paint, prompt, line.text, line.masked); kbd_paint(paint, k); } line.dirty = 0; }
				} else {
					handle(&line, e);
				}
			}
		}
		kbd_tick(k, now_ms(), &ko);
		for (int q = 0; q < ko.nev; q++) handle(&line, &ko.ev[q]);
		if (ko.buzz) haptic_pulse(haptic);
		if (paint && (ko.dirty || line.dirty)) {
			kbd_paint_header(paint, prompt, line.text, line.masked);
			kbd_paint(paint, k);
			line.dirty = 0;
		}
		if (now_ms() - last_input > timeout_s * 1000L)
			break;
	}
	if (paint) {
		host_raw_touch(0);
		host_text();
		kbd_paint_close(paint);
		haptic_close(haptic);
	}
	input_close(in);
	kbd_free(k);
	if (!line.ok)
		return 1;
	fwrite(line.text, 1, line.len, stdout);
	return 0;
}
