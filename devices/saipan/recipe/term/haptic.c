/* The haptic pulse; see haptic.h. The effect is uploaded once; a pulse is one write of the play event. */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "haptic.h"

static int effect_id = -1;

int haptic_open(int pulse_ms)
{
	char path[128], name[64];
	struct ff_effect e;
	int fd = -1;

	for (int i = 0; i < 16 && fd < 0; i++) {
		FILE *f;
		snprintf(path, sizeof(path), "/sys/class/input/event%d/device/name", i);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(name, sizeof(name), f) && !strncmp(name, "regulator-haptic", 16)) {
			snprintf(path, sizeof(path), "/dev/input/event%d", i);
			fd = open(path, O_RDWR | O_CLOEXEC);
		}
		fclose(f);
	}
	if (fd < 0)
		return -1;
	memset(&e, 0, sizeof(e));
	e.type = FF_RUMBLE;
	e.id = -1;
	e.replay.length = pulse_ms;
	e.u.rumble.strong_magnitude = 0xffff;
	if (ioctl(fd, EVIOCSFF, &e) < 0) {
		close(fd);
		return -1;
	}
	effect_id = e.id;
	return fd;
}

void haptic_pulse(int fd)
{
	struct input_event ev;

	if (fd < 0)
		return;
	memset(&ev, 0, sizeof(ev));
	ev.type = EV_FF;
	ev.code = effect_id;
	ev.value = 1;
	if (write(fd, &ev, sizeof(ev)) < 0)
		return;		/* a missed pulse is harmless */
}

void haptic_close(int fd)
{
	if (fd >= 0)
		close(fd);
}
