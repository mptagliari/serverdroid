#include <errno.h>
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "input.h"

#define MAXIN 4
struct Input {
	int fd[MAXIN], n;
	char buf[MAXIN][1024];
	int len[MAXIN];
};

Input *input_open(const char *const *paths, int n)
{
	Input *in = calloc(1, sizeof(*in));

	for (int i = 0; i < n && i < MAXIN; i++) {
		struct stat st;
		if (stat(paths[i], &st) < 0)
			mkfifo(paths[i], 0600);
		in->fd[in->n] = open(paths[i], O_RDWR | O_NONBLOCK | O_CLOEXEC);	/* read-write: a FIFO with no writer is not an end of file */
		if (in->fd[in->n] >= 0)
			in->n++;
	}
	return in;
}

int input_nfds(const Input *in) { return in->n; }
int input_fd(const Input *in, int i) { return in->fd[i]; }

static const struct { const char *name; VtKey key; } keynames[] = {
	{ "enter", VK_ENTER }, { "tab", VK_TAB }, { "backspace", VK_BACKSPACE }, { "escape", VK_ESCAPE }, { "up", VK_UP },
	{ "down", VK_DOWN }, { "left", VK_LEFT }, { "right", VK_RIGHT }, { "ins", VK_INS }, { "del", VK_DEL }, { "home", VK_HOME },
	{ "end", VK_END }, { "pgup", VK_PAGEUP }, { "pgdn", VK_PAGEDOWN },
};

static int parse_key(const char *s, VtKey *key, int *mods)
{
	char name[32];
	const char *plus = strchr(s, '+');
	size_t n = plus ? (size_t)(plus - s) : strlen(s);

	if (n >= sizeof(name))
		return 0;
	memcpy(name, s, n);
	name[n] = 0;
	*mods = 0;
	for (const char *m = plus; m; m = strchr(m + 1, '+')) {
		if (!strncmp(m + 1, "ctrl", 4)) *mods |= MOD_CTRL;
		else if (!strncmp(m + 1, "alt", 3)) *mods |= MOD_ALT;
		else if (!strncmp(m + 1, "shift", 5)) *mods |= MOD_SHIFT;
	}
	if (name[0] == 'f' && name[1] >= '1' && name[1] <= '9') {
		int f = atoi(name + 1);
		if (f >= 1 && f <= 12) { *key = VK_F1 + f - 1; return 1; }
	}
	for (size_t i = 0; i < sizeof(keynames) / sizeof(keynames[0]); i++)
		if (!strcmp(name, keynames[i].name)) { *key = keynames[i].key; return 1; }
	return 0;
}

static int parse_line(const char *l, Event *e)
{
	memset(e, 0, sizeof(*e));
	if (!strncmp(l, "T ", 2)) { e->type = EV_TEXT; snprintf(e->text, sizeof(e->text), "%s", l + 2); return 1; }
	if (!strncmp(l, "K ", 2)) { e->type = EV_KEY; return parse_key(l + 2, &e->key, &e->mods); }
	if (!strncmp(l, "C ", 2) && l[2]) { e->type = EV_CTRL; e->cp = (unsigned char)l[2]; return 1; }
	if (!strncmp(l, "SCROLL ", 7)) { e->type = EV_SCROLL; e->n = atoi(l + 7); return 1; }
	if (!strncmp(l, "RESIZE ", 7)) { e->type = EV_RESIZE; return sscanf(l + 7, "%d %d", &e->a, &e->b) == 2; }
	if (!strcmp(l, "RESTART")) { e->type = EV_RESTART; return 1; }
	if (!strcmp(l, "EXIT") || !strcmp(l, "POWER")) { e->type = EV_EXIT; return 1; }
	if (!strcmp(l, "UP")) { e->type = EV_SCROLL; e->n = 1; return 1; }
	if (!strcmp(l, "DOWN")) { e->type = EV_SCROLL; e->n = -1; return 1; }
	if (!strcmp(l, "OK")) { e->type = EV_OK; return 1; }
	if ((l[0] == 'P' || l[0] == 'M' || l[0] == 'R') && l[1] == ' ') { e->type = EV_TOUCH; e->touch = l[0]; return sscanf(l + 2, "%d %d", &e->a, &e->b) == 2; }
	if (!strncmp(l, "TAP ", 4)) { e->type = EV_TAP; return sscanf(l + 4, "%d %d", &e->a, &e->b) == 2; }
	return 0;		/* LONG and anything unknown are ignored */
}

int input_read(Input *in, int i, Event *out, int max)
{
	int got = 0;
	ssize_t r = read(in->fd[i], in->buf[i] + in->len[i], sizeof(in->buf[i]) - in->len[i] - 1);

	if (r > 0)
		in->len[i] += r;
	for (;;) {
		char *nl = memchr(in->buf[i], '\n', in->len[i]);
		size_t used;

		if (!nl) {
			if (in->len[i] >= (int)sizeof(in->buf[i]) - 1)	/* a line that never ends: drop it */
				in->len[i] = 0;
			break;
		}
		*nl = 0;
		if (got < max && parse_line(in->buf[i], &out[got]))
			got++;
		used = (nl - in->buf[i]) + 1;
		memmove(in->buf[i], in->buf[i] + used, in->len[i] - used);
		in->len[i] -= used;
	}
	return got;
}

void input_close(Input *in)
{
	for (int i = 0; i < in->n; i++)
		close(in->fd[i]);
	free(in);
}
