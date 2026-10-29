/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * Run the phone's vibration motor for a while (freestanding aarch64, no libc).
 * Usage: buzz DEVICE MILLISECONDS      e.g. buzz /dev/input/event2 200
 * DEVICE is the force-feedback input device of the "regulator-haptic" node (the VIBR LDO); the effect is a full-strength rumble and the
 * motor stops when the program exits.
 *   aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os -o buzz tools/buzz.c
 */
typedef unsigned long u64;
typedef long s64;

static s64 sys(s64 n, s64 a, s64 b, s64 c)
{
	register s64 x8 __asm__("x8") = n, x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
	return x0;
}

struct ff_effect {		/* struct ff_effect with its rumble member; 48 bytes on arm64 */
	unsigned short type;
	short id;
	unsigned short direction;
	unsigned short trigger_button, trigger_interval;
	unsigned short replay_length, replay_delay;
	unsigned short pad;
	unsigned short strong, weak;
	unsigned char rest[28];
};

struct input_event {
	s64 sec, usec;
	unsigned short type, code;
	int value;
};

void c_main(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	struct ff_effect e = { .type = 0x50 /* FF_RUMBLE */, .id = -1, .strong = 0xffff };
	struct input_event ev = { .type = 0x15 /* EV_FF */, .value = 1 };
	s64 ms = 0, ts[2];
	long fd;

	if (argc < 3)
		sys(93, 2, 0, 0);
	for (const char *s = argv[2]; *s >= '0' && *s <= '9'; s++)
		ms = ms * 10 + (*s - '0');
	e.replay_length = ms;
	fd = sys(56, -100, (s64)argv[1], 2 /* O_RDWR */);
	if (fd < 0 || sys(29, fd, 0x40304580 /* EVIOCSFF */, (s64)&e) < 0)
		sys(93, 1, 0, 0);
	ev.code = e.id;
	sys(64, fd, (s64)&ev, sizeof(ev));
	ts[0] = ms / 1000;
	ts[1] = (ms % 1000) * 1000000;
	sys(101, (s64)ts, 0, 0);
	sys(93, 0, 0, 0);
}

__asm__(".global _start\n_start:\n\tmov x0, sp\n\tbl c_main\n");
