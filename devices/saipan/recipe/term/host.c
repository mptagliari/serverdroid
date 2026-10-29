/* See host.h. */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "host.h"

#define RAW_FLAG "/run/touch-raw"
#define KDGETMODE 0x4B3B
#define KDSETMODE 0x4B3A
#define KD_TEXT 0
#define KD_GRAPHICS 1

static int g_fd = -1, g_old = KD_TEXT, g_atexit;

static void raw_off(void) { unlink(RAW_FLAG); }

void host_raw_touch(int on)
{
	if (!on) {
		raw_off();
		return;
	}
	int fd = open(RAW_FLAG, O_CREAT | O_WRONLY | O_CLOEXEC, 0644);

	if (fd >= 0) {
		close(fd);
		if (!g_atexit) {
			g_atexit = 1;
			atexit(raw_off);
		}
	}
}

void host_text(void)
{
	if (g_fd >= 0) {
		ioctl(g_fd, KDSETMODE, g_old);
		(void)!write(g_fd, "\033[?25h", 6);	/* the console's own cursor back */
		close(g_fd);
		g_fd = -1;
	}
}

void host_graphics(const char *tty)
{
	g_fd = open(tty, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (g_fd < 0)
		return;
	ioctl(g_fd, KDGETMODE, &g_old);
	(void)!write(g_fd, "\033[?25l", 6);	/* hidden while still in text mode: in graphics mode the console ignores writes, and this erases the drawn cursor */
	ioctl(g_fd, KDSETMODE, KD_GRAPHICS);
	atexit(host_text);
}
