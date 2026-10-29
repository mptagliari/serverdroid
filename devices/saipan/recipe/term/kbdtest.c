/* Unit tests of the keyboard logic (kbd.c), no screen needed: gcc -Wall -o kbdtest kbdtest.c kbd.c && ./kbdtest */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <stdio.h>
#include <string.h>
#include "kbd.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Kbd *k;
static long T;

static void center(const char *label, int *x, int *y)
{
	int i = kbd_key_at(k, label);
	KbdKeyView v;
	if (i < 0 && label[0] >= 'a' && label[0] <= 'z' && !label[1]) {	/* with shift on the key shows the capital */
		char up[2] = { label[0] - 32, 0 };
		i = kbd_key_at(k, up);
	}
	if (i < 0) { *x = *y = -1; return; }
	kbd_key_view(k, i, &v);
	*x = v.box.x + v.box.w / 2;
	*y = v.box.y + v.box.h / 2;
}

static KbdOut press(const char *label, int hold_ms, int release)
{
	KbdOut o = { 0 };
	int x, y;
	center(label, &x, &y);
	T += 1000;
	kbd_touch(k, 'P', x, y, T, &o);
	if (hold_ms) { KbdOut t = { 0 }; T += hold_ms; kbd_tick(k, T, &t); o.nev += 0; if (t.nev) { memcpy(&o.ev[o.nev], t.ev, sizeof(Event) * t.nev); o.nev += t.nev; } o.dirty |= t.dirty; o.buzz |= t.buzz; }
	if (release) {
		KbdOut r = { 0 };
		kbd_touch(k, 'R', x, y, T, &r);
		if (r.nev) { memcpy(&o.ev[o.nev], r.ev, sizeof(Event) * r.nev); o.nev += r.nev; }
	}
	return o;
}

static const char *typed(const char *label)	/* tap a key and return the text it typed ("" if not text) */
{
	static char buf[300];
	KbdOut o = press(label, 0, 1);
	buf[0] = 0;
	if (o.nev == 1 && o.ev[0].type == EV_TEXT) strcpy(buf, o.ev[0].text);
	else if (o.nev == 1 && o.ev[0].type == EV_CTRL) sprintf(buf, "^%c", o.ev[0].cp);
	else sprintf(buf, "<%d events, type %d>", o.nev, o.nev ? (int)o.ev[0].type : -1);
	return buf;
}

int main(int argc, char **argv)
{
	Box area = { 32, 976, 672, 600 };
	const char *letters = "qwertyuiopasdfghjklzxcvbnm";

	k = kbd_new(area);

	if (argc == 3 && !strcmp(argv[1], "center")) {		/* for test.sh: where the key with this label is, in screen pixels */
		int x, y;
		center(argv[2], &x, &y);
		printf("%d %d\n", x, y);
		return x < 0;
	}

	/* geometry: every key inside the area, none overlapping */
	for (int p = 0; p < 3; p++) {
		int n = kbd_nkeys(k);
		for (int i = 0; i < n; i++) {
			KbdKeyView a, b;
			kbd_key_view(k, i, &a);
			CHECK(a.box.x >= area.x && a.box.y >= area.y && a.box.x + a.box.w <= area.x + area.w && a.box.y + a.box.h <= area.y + area.h,
			      "page %d key %d (%s) outside the area", p, i, a.label);
			for (int j = i + 1; j < n; j++) {
				kbd_key_view(k, j, &b);
				CHECK(a.box.x + a.box.w <= b.box.x || b.box.x + b.box.w <= a.box.x || a.box.y + a.box.h <= b.box.y || b.box.y + b.box.h <= a.box.y,
				      "page %d keys %s and %s overlap", p, a.label, b.label);
			}
		}
		{ KbdOut o = { 0 }; int x, y; center(p == 0 ? "?123" : p == 1 ? "=\\<" : "?123", &x, &y);
		  kbd_touch(k, 'P', x, y, T += 1000, &o); kbd_touch(k, 'R', x, y, T, &o); }
	}
	{ KbdOut o = { 0 }; int x, y; center("ABC", &x, &y); kbd_touch(k, 'P', x, y, T += 1000, &o); kbd_touch(k, 'R', x, y, T, &o); }
	CHECK(kbd_key_at(k, "q") >= 0, "back on the letters page");

	/* every letter types itself from the center of its key */
	for (const char *c = letters; *c; c++) {
		char l[2] = { *c, 0 };
		CHECK(!strcmp(typed(l), l), "letter %s typed '%s'", l, typed(l));
	}
	CHECK(!strcmp(typed(" "), " "), "space");
	CHECK(!strcmp(typed(","), ","), "comma");
	CHECK(!strcmp(typed("."), "."), "period");

	/* a touch in the gap between q and w goes to one of them; a touch above the area is ignored */
	{
		int xq, y, xw; KbdOut o = { 0 };
		center("q", &xq, &y); center("w", &xw, &y);
		kbd_touch(k, 'P', (xq + xw) / 2, y, T += 1000, &o);
		kbd_touch(k, 'R', (xq + xw) / 2, y, T, &o);
		CHECK(o.nev == 1 && o.ev[0].type == EV_TEXT && (o.ev[0].text[0] == 'q' || o.ev[0].text[0] == 'w'), "a touch in the gap gives q or w");
		memset(&o, 0, sizeof(o));
		kbd_touch(k, 'P', xq, area.y - 5, T += 1000, &o);
		CHECK(!kbd_active(k) && o.nev == 0, "a touch above the area is ignored");
	}

	/* shift: one letter, then off; a double tap is caps lock; one more tap ends it */
	{ KbdOut o = press("\xe2\x96\xb2", 0, 1); (void)o; }
	CHECK(!strcmp(typed("a"), "A"), "shift then a");
	CHECK(!strcmp(typed("a"), "a"), "shift was one-shot");
	press("\xe2\x96\xb2", 0, 1); T -= 800; press("\xe2\x96\xb2", 0, 1);	/* two taps within 400 ms */
	CHECK(!strcmp(typed("a"), "A") && !strcmp(typed("b"), "B"), "caps lock");
	press("\xe2\x96\xb2\xe2\x96\xb2", 0, 1);
	CHECK(!strcmp(typed("a"), "a"), "caps lock off");

	/* accents by long press: the first item is a-tilde; sliding right picks the next ones */
	{
		KbdOut o = press("a", 400, 0);
		KbdTrayView tv;
		kbd_tray_view(k, &tv);
		CHECK(tv.n == 5 && !strcmp(tv.item[0].label, "\xc3\xa3") && o.buzz, "tray of a: %d items, first %s", tv.n, tv.item[0].label);
		CHECK(tv.tray.y + tv.tray.h <= area.y + area.h && tv.tray.y >= area.y && tv.tray.x >= area.x && tv.tray.x + tv.tray.w <= area.x + area.w, "tray inside the area");
		{ KbdOut m = { 0 }, r = { 0 };
		  kbd_touch(k, 'M', tv.item[2].box.x + 5, tv.item[2].box.y + 10, T += 50, &m);
		  kbd_touch(k, 'R', tv.item[2].box.x + 5, tv.item[2].box.y + 10, T, &r);
		  CHECK(r.nev == 1 && r.ev[0].type == EV_TEXT && !strcmp(r.ev[0].text, "\xc3\xa2"), "slide to the third item types a-circumflex"); }
	}
	/* a short hold does not open the tray, and the release types the letter */
	{ KbdOut o = press("a", 200, 1); CHECK(o.nev == 1 && !strcmp(o.ev[0].text, "a"), "a 200 ms touch types a"); }
	/* e: accents first, the digit last */
	{
		KbdTrayView tv; press("e", 400, 0); kbd_tray_view(k, &tv);
		CHECK(tv.n == 5 && !strcmp(tv.item[4].label, "3") && !strcmp(tv.item[0].label, "\xc3\xa9"), "tray of e");
		{ KbdOut r = { 0 }; kbd_touch(k, 'M', 700, tv.item[0].box.y, T += 10, &r); kbd_touch(k, 'R', 700, tv.item[0].box.y, T, &r);
		  CHECK(r.nev == 1 && !strcmp(r.ev[0].text, "3"), "sliding past the end picks the last item"); }
	}
	/* shift applies to the tray */
	{
		press("\xe2\x96\xb2", 0, 1);
		KbdTrayView tv; press("a", 400, 0); kbd_tray_view(k, &tv);
		{ KbdOut r = { 0 }; kbd_touch(k, 'R', tv.item[0].box.x + 3, tv.item[0].box.y, T += 10, &r);
		  CHECK(r.nev == 1 && !strcmp(r.ev[0].text, "\xc3\x83"), "shifted tray gives A-tilde"); }
	}
	/* moving off the key before releasing types the key under the finger */
	{
		int xa, ya, xs, ys; KbdOut o = { 0 };
		center("a", &xa, &ya); center("s", &xs, &ys);
		kbd_touch(k, 'P', xa, ya, T += 1000, &o); kbd_touch(k, 'M', xs, ys, T += 20, &o); kbd_touch(k, 'R', xs, ys, T += 20, &o);
		CHECK(o.nev == 1 && !strcmp(o.ev[0].text, "s"), "slid to s");
	}
	/* the preview bubble stays inside the area */
	{
		int x, y; KbdOut o = { 0 }; Box b; char l[16];
		center("p", &x, &y); kbd_touch(k, 'P', x, y, T += 1000, &o);
		CHECK(kbd_bubble(k, &b, l) && b.x >= area.x && b.x + b.w <= area.x + area.w && b.y >= area.y, "bubble of p inside the area");
		kbd_touch(k, 'R', x, y, T, &o);
	}
	/* ctrl and alt are one-shot */
	press("CTRL", 0, 1);
	CHECK(!strcmp(typed("c"), "^c"), "ctrl+c");
	CHECK(!strcmp(typed("c"), "c"), "ctrl was one-shot");
	/* the slash of the function row: a tap types it, a long press offers the backslash and the bar */
	CHECK(!strcmp(typed("/"), "/"), "slash");
	{ KbdTrayView tv; press("/", 400, 0); kbd_tray_view(k, &tv); CHECK(tv.n == 2 && !strcmp(tv.item[0].label, "\\") && !strcmp(tv.item[1].label, "|"), "slash tray");
	  { KbdOut r = { 0 }; kbd_touch(k, 'M', tv.item[1].box.x + 3, tv.item[1].box.y, T += 10, &r); kbd_touch(k, 'R', tv.item[1].box.x + 3, tv.item[1].box.y, T += 10, &r); CHECK(r.nev == 1 && !strcmp(r.ev[0].text, "|"), "bar from the slash tray"); } }
	press("CTRL", 0, 1);
	{ KbdOut o = press("\xe2\x86\x90", 0, 1); CHECK(o.nev == 1 && o.ev[0].type == EV_KEY && o.ev[0].key == VK_LEFT && o.ev[0].mods == MOD_CTRL, "ctrl+left"); }
	/* delete acts on press, repeats while held after 400 ms, and the release adds nothing */
	{
		int x, y, n = 0; KbdOut o = { 0 }, t;
		center("\xe2\x97\x80", &x, &y);
		kbd_touch(k, 'P', x, y, T += 1000, &o);
		CHECK(o.nev == 1 && o.ev[0].type == EV_KEY && o.ev[0].key == VK_BACKSPACE, "delete acts on press");
		memset(&t, 0, sizeof(t)); kbd_tick(k, T + 300, &t); CHECK(t.nev == 0, "no repeat before 400 ms");
		memset(&t, 0, sizeof(t)); kbd_tick(k, T + 410, &t); n += t.nev;
		memset(&t, 0, sizeof(t)); kbd_tick(k, T + 480, &t); n += t.nev;
		CHECK(n == 2, "repeats after 400 ms (%d)", n);
		CHECK(kbd_timeout(k, T + 480) > 0, "a repeat timer is pending");
		memset(&t, 0, sizeof(t)); kbd_touch(k, 'R', x, y, T + 500, &t); CHECK(t.nev == 0, "release adds nothing");
		CHECK(kbd_timeout(k, T + 600) < 0, "no timer when nothing is held");
	}
	/* enter and tab */
	{ KbdOut o = press("Enter", 0, 1); CHECK(o.nev == 1 && o.ev[0].key == VK_ENTER, "enter"); }
	{ KbdOut o = press("TAB", 0, 1); CHECK(o.nev == 1 && o.ev[0].key == VK_TAB, "tab"); }
	/* ESC long press opens a tray (downwards) with Home first */
	{
		KbdOut o = press("ESC", 400, 0); KbdTrayView tv; kbd_tray_view(k, &tv);
		CHECK(tv.n == 4 && !strcmp(tv.item[0].label, "Home") && tv.tray.y > area.y && tv.tray.y + tv.tray.h <= area.y + area.h, "ESC tray");
		{ KbdOut r = { 0 }; kbd_touch(k, 'R', tv.item[0].box.x + 3, tv.item[0].box.y, T += 10, &r); CHECK(r.nev == 1 && r.ev[0].key == VK_HOME, "Home"); }
		(void)o;
	}
	/* a plain ESC tap */
	{ KbdOut o = press("ESC", 0, 1); CHECK(o.nev == 1 && o.ev[0].key == VK_ESCAPE, "escape"); }
	/* symbol pages */
	press("?123", 0, 1);
	CHECK(!strcmp(typed("@"), "@") && !strcmp(typed("1"), "1") && !strcmp(typed("-"), "-") && !strcmp(typed("/"), "/"), "symbols page");
	press("=\\<", 0, 1);
	CHECK(!strcmp(typed("|"), "|") && !strcmp(typed("~"), "~") && !strcmp(typed("\\"), "\\") && !strcmp(typed("{"), "{"), "second symbols page");
	press("?123", 0, 1);
	press("ABC", 0, 1);
	CHECK(!strcmp(typed("q"), "q"), "back to the letters");

	/* every key with alternatives opens a tray inside the area, on every page, with and without shift */
	for (int p = 0; p < 3; p++) {
		for (int i = 0; i < kbd_nkeys(k); i++) {
			KbdKeyView v; KbdTrayView tv; KbdOut o = { 0 }; KbdOut r = { 0 };
			kbd_key_view(k, i, &v);
			kbd_touch(k, 'P', v.box.x + v.box.w / 2, v.box.y + v.box.h / 2, T += 1000, &o);
			kbd_tick(k, T + 500, &o);
			kbd_tray_view(k, &tv);
			if (tv.n)
				CHECK(tv.tray.x >= area.x && tv.tray.x + tv.tray.w <= area.x + area.w && tv.tray.y >= area.y && tv.tray.y + tv.tray.h <= area.y + area.h,
				      "tray of %s inside the area", v.label);
			kbd_touch(k, 'R', v.box.x + 1, v.box.y + 1, T + 510, &r);
		}
		{ KbdOut o = { 0 }; int x, y; center(p == 0 ? "?123" : p == 1 ? "=\\<" : "?123", &x, &y);
		  kbd_touch(k, 'P', x, y, T += 1000, &o); kbd_touch(k, 'R', x, y, T, &o); }
	}
	kbd_free(k);
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
