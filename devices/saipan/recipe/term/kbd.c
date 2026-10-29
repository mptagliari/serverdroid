/* The on-screen keyboard logic; see kbd.h. No pixels here, no libvterm, no framebuffer. */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <stdlib.h>
#include <string.h>
#include "kbd.h"

enum { KA_CHAR, KA_SHIFT, KA_PAGE, KA_CTRL, KA_ALT, KA_KEY };
enum { PAGES = 3, MAXKEYS = 48, ROWS = 6, LONG_MS = 350, REPEAT_FIRST_MS = 400, REPEAT_MS = 60, GAPX = 16, GAPY = 20, NEAR_MAX = 60, QUARTERS = 40 };
enum { DF_REP = 1, DF_DARK = 2, DF_HINT = 4 };

typedef struct {
	const char *label;	/* the key's face; for KA_CHAR also the text it types */
	int act, arg;		/* KA_PAGE: the page; KA_KEY: a VtKey */
	int q;			/* width in quarters of a key (a standard key is 4: ten fill a row) */
	const char *alts;	/* long-press tray: items separated by spaces, "@name" for the keys home end pgup pgdn */
	int flags;
	char hint;
} Def;

typedef struct {
	Box box;
	const Def *def;
} Key;

typedef struct {
	char label[8], text[8];
	int key;		/* a VtKey, or 0 when the item types text */
} Item;

struct Kbd {
	Box area;
	int rowh;
	Key keys[PAGES][MAXKEYS];
	int nkeys[PAGES];
	int page, shift, ctrl, alt;	/* shift: 0 off, 1 next letter, 2 caps */
	long now, shift_tap;
	int down, cur, first, fired;
	long t_down, t_rep;
	int tray_n, sel;
	Item item[10];
	Box item_box[10], tray;
};

#define EX { "ESC", KA_KEY, VK_ESCAPE, 5, "@home @end @pgup @pgdn", DF_DARK, 0 }, \
	{ "TAB", KA_KEY, VK_TAB, 5, "| ~ - / \\ _", DF_DARK, 0 }, \
	{ "CTRL", KA_CTRL, 0, 5, NULL, DF_DARK, 0 }, { "/", KA_CHAR, 0, 5, "\\ |", DF_DARK, 0 }, \
	{ "\xe2\x86\x90", KA_KEY, VK_LEFT, 5, NULL, DF_REP | DF_DARK, 0 }, { "\xe2\x86\x93", KA_KEY, VK_DOWN, 5, NULL, DF_REP | DF_DARK, 0 }, \
	{ "\xe2\x86\x91", KA_KEY, VK_UP, 5, NULL, DF_REP | DF_DARK, 0 }, { "\xe2\x86\x92", KA_KEY, VK_RIGHT, 5, NULL, DF_REP | DF_DARK, 0 }
#define C(l, a) { l, KA_CHAR, 0, 4, a, 0, 0 }
#define CH(l, a, h) { l, KA_CHAR, 0, 4, a, DF_HINT, h }
#define BKSP { "\xe2\x97\x80", KA_KEY, VK_BACKSPACE, 6, NULL, DF_REP | DF_DARK, 0 }
#define BOTTOM(pagelabel, target) \
	{ pagelabel, KA_PAGE, target, 6, NULL, DF_DARK, 0 }, C(",", "; : - _ ( ) /"), { " ", KA_CHAR, 0, 16, NULL, 0, 0 }, C(".", "\xe2\x80\xa6 ! ? '"), \
	{ "-", KA_CHAR, 0, 5, "_", 0, 0 }, { "~", KA_CHAR, 0, 5, "` ^", 0, 0 }

/* Enter is a bar of its own above the function keys: out of the middle of the letters, where a slip sends a command by accident */
static const Def row_enter[] = { { "Enter", KA_KEY, VK_ENTER, 40, NULL, DF_DARK, 0 } };
static const Def row_ex[] = { EX };
static const Def alpha1[] = {
	CH("q", "1", '1'), CH("w", "2", '2'), CH("e", "\xc3\xa9 \xc3\xaa \xc3\xa8 \xc3\xab 3", '3'), CH("r", "4", '4'), CH("t", "5", '5'),
	CH("y", "6", '6'), CH("u", "\xc3\xba \xc3\xbc \xc3\xb9 \xc3\xbb 7", '7'), CH("i", "\xc3\xad \xc3\xae \xc3\xac \xc3\xaf 8", '8'),
	CH("o", "\xc3\xb5 \xc3\xb3 \xc3\xb4 \xc3\xb2 \xc3\xb6 9", '9'), CH("p", "0", '0') };
static const Def alpha2[] = {
	C("a", "\xc3\xa3 \xc3\xa1 \xc3\xa2 \xc3\xa0 \xc3\xa4"), C("s", NULL), C("d", NULL), C("f", NULL), C("g", NULL), C("h", NULL), C("j", NULL),
	C("k", NULL), C("l", NULL) };
static const Def alpha3[] = {
	{ "\xe2\x96\xb2", KA_SHIFT, 0, 6, NULL, DF_DARK, 0 }, C("z", NULL), C("x", NULL), C("c", "\xc3\xa7"), C("v", NULL), C("b", NULL),
	C("n", "\xc3\xb1"), C("m", NULL), BKSP };
static const Def alpha4[] = { BOTTOM("?123", 1) };
static const Def sym1_1[] = { C("1", NULL), C("2", NULL), C("3", NULL), C("4", NULL), C("5", NULL), C("6", NULL), C("7", NULL), C("8", NULL),
	C("9", NULL), C("0", NULL) };
static const Def sym1_2[] = {
	C("@", NULL), C("#", NULL), C("$", "\xe2\x82\xac \xc2\xa3 \xc2\xa5"), C("%", NULL), C("&", NULL), C("-", "_"), C("+", "= ~ ^"),
	C("(", "[ { <"), C(")", "] } >"), C("/", "\\ |") };
static const Def sym1_3[] = {
	{ "=\\<", KA_PAGE, 2, 6, NULL, DF_DARK, 0 }, C("*", "` ^"), C("\"", NULL), C("'", NULL), C(":", NULL), C(";", NULL),
	C("!", "\xc2\xa1"), C("?", "\xc2\xbf"), BKSP };
static const Def sym2_1[] = { C("~", NULL), C("`", NULL), C("|", NULL), C("^", NULL), C("=", NULL), C("_", NULL), C("{", NULL), C("}", NULL),
	C("[", NULL), C("]", NULL) };
static const Def sym2_2[] = { C("<", NULL), C(">", NULL), C("\\", NULL), C("/", NULL), C("\xc2\xbf", NULL), C("\xc2\xa1", NULL),
	C("\xe2\x82\xac", NULL), C("\xc2\xa3", NULL), C("\xc2\xa5", NULL), C("\xc2\xb0", NULL) };
static const Def sym2_3[] = {
	{ "?123", KA_PAGE, 1, 6, NULL, DF_DARK, 0 }, C("\xc2\xb1", NULL), C("\xc3\x97", NULL), C("\xc3\xb7", NULL), C("\xc2\xa7", NULL),
	C("\xc2\xb6", NULL), C("\xc2\xa9", NULL), C("\xc2\xae", NULL), BKSP };
static const Def sym_bottom[] = { BOTTOM("ABC", 0) };

typedef struct { const Def *defs; int n, lead; } Row;
#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const Row page_rows[PAGES][ROWS] = {
	{ { row_enter, N(row_enter), 0 }, { row_ex, N(row_ex), 0 }, { alpha1, N(alpha1), 0 }, { alpha2, N(alpha2), 2 }, { alpha3, N(alpha3), 0 }, { alpha4, N(alpha4), 0 } },
	{ { row_enter, N(row_enter), 0 }, { row_ex, N(row_ex), 0 }, { sym1_1, N(sym1_1), 0 }, { sym1_2, N(sym1_2), 0 }, { sym1_3, N(sym1_3), 0 }, { sym_bottom, N(sym_bottom), 0 } },
	{ { row_enter, N(row_enter), 0 }, { row_ex, N(row_ex), 0 }, { sym2_1, N(sym2_1), 0 }, { sym2_2, N(sym2_2), 0 }, { sym2_3, N(sym2_3), 0 }, { sym_bottom, N(sym_bottom), 0 } },
};

static uint32_t first_cp(const char *s)
{
	const unsigned char *u = (const unsigned char *)s;
	if (u[0] < 0x80) return u[0];
	if ((u[0] & 0xE0) == 0xC0) return ((u[0] & 31) << 6) | (u[1] & 63);
	if ((u[0] & 0xF0) == 0xE0) return ((u[0] & 15) << 12) | ((u[1] & 63) << 6) | (u[2] & 63);
	return 0xFFFD;
}

static int put_cp(char *d, uint32_t cp)
{
	if (cp < 0x80) { d[0] = cp; return 1; }
	if (cp < 0x800) { d[0] = 0xC0 | (cp >> 6); d[1] = 0x80 | (cp & 63); return 2; }
	d[0] = 0xE0 | (cp >> 12); d[1] = 0x80 | ((cp >> 6) & 63); d[2] = 0x80 | (cp & 63);
	return 3;
}

static int ncps(const char *s)
{
	int n = 0;
	for (; *s; s++)
		if (((unsigned char)*s & 0xC0) != 0x80) n++;
	return n;
}

/* shift applied to one character: ASCII and Latin-1 lowercase letters */
static uint32_t shifted(uint32_t cp)
{
	if ((cp >= 'a' && cp <= 'z') || (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)) return cp - 32;
	return cp;
}

static void shift_text(const Kbd *k, const char *in, char *out)	/* out has room for 8 bytes */
{
	uint32_t cp = first_cp(in);
	int n = put_cp(out, k->shift ? shifted(cp) : cp);
	out[n] = 0;
}

static const Def *def_of(const Kbd *k, int i) { return k->keys[k->page][i].def; }

Kbd *kbd_new(Box area)
{
	Kbd *k = calloc(1, sizeof(*k));

	k->area = area;
	k->rowh = area.h / ROWS;
	k->cur = k->first = -1;
	for (int p = 0; p < PAGES; p++) {
		for (int r = 0; r < ROWS; r++) {
			const Row *row = &page_rows[p][r];
			int q0 = row->lead;
			for (int i = 0; i < row->n; i++) {
				const Def *d = &row->defs[i];
				Key *key = &k->keys[p][k->nkeys[p]++];
				key->def = d;
				key->box.x = area.x + q0 * area.w / QUARTERS + GAPX / 2;
				key->box.w = d->q * area.w / QUARTERS - GAPX;
				key->box.y = area.y + r * k->rowh + GAPY / 2;
				key->box.h = k->rowh - GAPY;
				q0 += d->q;
			}
		}
	}
	return k;
}

void kbd_free(Kbd *k) { free(k); }
Box kbd_area(const Kbd *k) { return k->area; }
int kbd_nkeys(const Kbd *k) { return k->nkeys[k->page]; }
int kbd_active(const Kbd *k) { return k->down; }

int kbd_inside(const Kbd *k, int x, int y)
{
	return x >= k->area.x && x < k->area.x + k->area.w && y >= k->area.y && y < k->area.y + k->area.h;
}

static int nearest(const Kbd *k, int x, int y)
{
	int best = -1;
	long bd = 0, bc = 0;

	for (int i = 0; i < k->nkeys[k->page]; i++) {
		const Box *b = &k->keys[k->page][i].box;
		int dx = x < b->x ? b->x - x : x >= b->x + b->w ? x - (b->x + b->w - 1) : 0;
		int dy = y < b->y ? b->y - y : y >= b->y + b->h ? y - (b->y + b->h - 1) : 0;
		long d = (long)dx * dx + (long)dy * dy;
		long cx = x - (b->x + b->w / 2), cy = y - (b->y + b->h / 2), c = cx * cx + cy * cy;
		if (best < 0 || d < bd || (d == bd && c < bc)) { best = i; bd = d; bc = c; }
	}
	if (best >= 0 && bd > (long)NEAR_MAX * NEAR_MAX)
		return -1;
	return best;
}

static Event *push(KbdOut *o)
{
	Event *e = &o->ev[o->nev < 3 ? o->nev++ : 3];
	memset(e, 0, sizeof(*e));
	return e;
}

static void clear_oneshot(Kbd *k, KbdOut *o)
{
	if (k->ctrl || k->alt || k->shift == 1) { k->ctrl = k->alt = 0; if (k->shift == 1) k->shift = 0; o->dirty = 1; }
}

static void emit_text(Kbd *k, KbdOut *o, const char *text)
{
	char t[8];
	uint32_t cp;
	Event *e = push(o);

	shift_text(k, text, t);
	cp = first_cp(t);
	if (k->ctrl && cp < 0x80) {
		e->type = EV_CTRL;
		e->cp = cp >= 'A' && cp <= 'Z' ? cp + 32 : cp;
	} else {
		e->type = EV_TEXT;
		strcpy(e->text, t);
		e->mods = k->alt ? MOD_ALT : 0;
	}
	clear_oneshot(k, o);
}

static void emit_key(Kbd *k, KbdOut *o, int vk)
{
	Event *e = push(o);

	e->type = EV_KEY;
	e->key = vk;
	e->mods = (k->ctrl ? MOD_CTRL : 0) | (k->alt ? MOD_ALT : 0);
	clear_oneshot(k, o);
}

static void activate(Kbd *k, int i, KbdOut *o)
{
	const Def *d = def_of(k, i);

	switch (d->act) {
	case KA_CHAR: emit_text(k, o, d->label); break;
	case KA_KEY: emit_key(k, o, d->arg); break;
	case KA_PAGE: k->page = d->arg; o->dirty = 1; break;
	case KA_CTRL: k->ctrl = !k->ctrl; o->dirty = 1; break;
	case KA_ALT: k->alt = !k->alt; o->dirty = 1; break;
	case KA_SHIFT:
		if (k->shift == 0) { k->shift = 1; k->shift_tap = k->now; }
		else if (k->shift == 1 && k->now - k->shift_tap < 400) k->shift = 2;	/* a double tap: caps lock */
		else k->shift = 0;
		o->dirty = 1;
		break;
	}
}

static int key_item(const char *name, Item *it)
{
	static const struct { const char *name, *label; int key; } named[] = {
		{ "home", "Home", VK_HOME }, { "end", "End", VK_END }, { "pgup", "PgUp", VK_PAGEUP }, { "pgdn", "PgDn", VK_PAGEDOWN } };
	for (int i = 0; i < N(named); i++)
		if (!strcmp(name, named[i].name)) {
			strcpy(it->label, named[i].label);
			it->text[0] = 0;
			it->key = named[i].key;
			return 1;
		}
	return 0;
}

static void open_tray(Kbd *k, int ki, KbdOut *o)
{
	const Def *d = def_of(k, ki);
	const Box *kb = &k->keys[k->page][ki].box;
	const char *s = d->alts;
	int n = 0, total = 0;

	while (s && *s && n < 10) {
		char tok[16];
		int l = 0;
		Item *it = &k->item[n];
		while (*s == ' ') s++;
		while (*s && *s != ' ' && l < 15) tok[l++] = *s++;
		tok[l] = 0;
		if (!l) break;
		if (tok[0] == '@' && key_item(tok + 1, it)) {
		} else {
			shift_text(k, tok, it->text);
			strcpy(it->label, it->text);
			it->key = 0;
		}
		k->item_box[n].w = ncps(it->label) * 16 + 24 > 60 ? ncps(it->label) * 16 + 24 : 60;
		total += k->item_box[n].w;
		n++;
	}
	if (!n)
		return;
	k->tray.x = kb->x;
	if (k->tray.x + total > k->area.x + k->area.w) k->tray.x = k->area.x + k->area.w - total;
	if (k->tray.x < k->area.x) k->tray.x = k->area.x;
	k->tray.w = total;
	k->tray.h = kb->h;
	k->tray.y = kb->y - k->rowh >= k->area.y ? kb->y - k->rowh : kb->y + k->rowh;	/* the top row opens its tray downwards */
	for (int i = 0, x = k->tray.x; i < n; i++) {
		k->item_box[i].x = x;
		k->item_box[i].y = k->tray.y;
		k->item_box[i].h = kb->h;
		x += k->item_box[i].w;
	}
	k->tray_n = n;
	k->sel = 0;
	o->buzz = 1;
	o->dirty = 1;
}

static void tray_select(Kbd *k, int x, KbdOut *o)
{
	int sel = x < k->item_box[0].x ? 0 : k->tray_n - 1;

	for (int i = 0; i < k->tray_n; i++)
		if (x >= k->item_box[i].x && x < k->item_box[i].x + k->item_box[i].w) { sel = i; break; }
	if (sel != k->sel) { k->sel = sel; o->dirty = 1; }
}

void kbd_touch(Kbd *k, char type, int x, int y, long now, KbdOut *o)
{
	k->now = now;
	if (type == 'P') {
		if (!kbd_inside(k, x, y))
			return;
		k->down = 1;
		k->tray_n = 0;
		k->fired = 0;
		k->cur = k->first = nearest(k, x, y);
		k->t_down = now;
		o->dirty = 1;
		if (k->cur >= 0) {
			o->buzz = 1;
			if (def_of(k, k->cur)->flags & DF_REP) {	/* delete and the arrows act at once, then repeat */
				activate(k, k->cur, o);
				k->fired = 1;
				k->t_rep = now + REPEAT_FIRST_MS;
			}
		}
	} else if (type == 'M' && k->down) {
		if (k->tray_n) {
			tray_select(k, x, o);
		} else {
			int n = nearest(k, x, y);
			if (n != k->cur) { k->cur = n; k->t_down = now; o->dirty = 1; }
		}
	} else if (type == 'R' && k->down) {
		k->down = 0;
		if (k->tray_n) {
			const Item *it = &k->item[k->sel];
			if (it->key) emit_key(k, o, it->key);
			else emit_text(k, o, it->text);
			k->tray_n = 0;
		} else if (k->cur >= 0 && !k->fired) {
			activate(k, k->cur, o);
		}
		k->cur = k->first = -1;
		o->dirty = 1;
	}
}

void kbd_tick(Kbd *k, long now, KbdOut *o)
{
	const Def *d;

	k->now = now;
	if (!k->down || k->cur < 0)
		return;
	d = def_of(k, k->cur);
	if (!k->tray_n && !k->fired && d->alts && now - k->t_down >= LONG_MS)
		open_tray(k, k->cur, o);
	else if (k->fired && (d->flags & DF_REP) && k->cur == k->first && now >= k->t_rep) {
		activate(k, k->cur, o);
		k->t_rep = now + REPEAT_MS;
	}
}

int kbd_timeout(const Kbd *k, long now)
{
	const Def *d;
	long t;

	if (!k->down || k->cur < 0)
		return -1;
	d = def_of(k, k->cur);
	if (!k->tray_n && !k->fired && d->alts)
		t = k->t_down + LONG_MS - now;
	else if (k->fired && (d->flags & DF_REP) && k->cur == k->first)
		t = k->t_rep - now;
	else
		return -1;
	return t < 1 ? 1 : (int)t;
}

void kbd_key_view(const Kbd *k, int i, KbdKeyView *v)
{
	const Key *key = &k->keys[k->page][i];
	const Def *d = key->def;

	memset(v, 0, sizeof(*v));
	v->box = key->box;
	if (d->act == KA_CHAR) shift_text(k, d->label, v->label);
	else if (d->act == KA_SHIFT && k->shift == 2) strcpy(v->label, "\xe2\x96\xb2\xe2\x96\xb2");
	else strcpy(v->label, d->label);
	if (d->flags & DF_HINT) v->hint[0] = d->hint;
	if (d->flags & DF_DARK) v->flags |= KF_DARK;
	if (k->down && i == k->cur) v->flags |= KF_PRESSED;
	if ((d->act == KA_SHIFT && k->shift) || (d->act == KA_CTRL && k->ctrl) || (d->act == KA_ALT && k->alt)) v->flags |= KF_LATCHED;
}

void kbd_tray_view(const Kbd *k, KbdTrayView *v)
{
	memset(v, 0, sizeof(*v));
	v->n = k->tray_n;
	v->sel = k->sel;
	v->tray = k->tray;
	for (int i = 0; i < k->tray_n; i++) {
		v->item[i].box = k->item_box[i];
		strcpy(v->item[i].label, k->item[i].label);
	}
}

int kbd_bubble(const Kbd *k, Box *box, char label[16])
{
	KbdKeyView v;
	const Key *key;

	if (!k->down || k->tray_n || k->cur < 0)
		return 0;
	key = &k->keys[k->page][k->cur];
	if (key->def->act != KA_CHAR || key->def->q > 5)	/* characters on ordinary keys; not the space bar */
		return 0;
	kbd_key_view(k, k->cur, &v);
	*box = key->box;
	box->x -= 10;
	box->w += 20;
	box->y -= k->rowh;
	if (box->x < k->area.x) box->x = k->area.x;
	if (box->x + box->w > k->area.x + k->area.w) box->x = k->area.x + k->area.w - box->w;
	strcpy(label, v.label);
	return 1;
}

int kbd_key_at(const Kbd *k, const char *label)
{
	KbdKeyView v;

	for (int i = 0; i < k->nkeys[k->page]; i++) {
		kbd_key_view(k, i, &v);
		if (!strcmp(v.label, label)) return i;
	}
	return -1;
}
