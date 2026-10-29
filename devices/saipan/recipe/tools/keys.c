/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * Turn the phone's buttons into text tokens, one per line, unbuffered (freestanding aarch64, no libc).
 * Usage: keys DEVICE [DEVICE ...]      e.g. keys /dev/input/event0 /dev/input/event1
 * Prints UP (volume up), DOWN (volume down), OK (assistant button) and POWER on key press; UP and DOWN also repeat while held.
 * Each device is opened once and all are polled together. Meant to feed the on-screen menu (rootfs/overlay/usr/local/bin/serverdroid-ui).
 *   aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os -o keys tools/keys.c
 */
typedef unsigned long u64;
typedef unsigned short u16;
typedef long s64;

static s64 sys(s64 n, s64 a, s64 b, s64 c, s64 d)
{
	register s64 x8 __asm__("x8") = n, x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c, x3 __asm__("x3") = d;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return x0;
}

static void say(const char *s)
{
	s64 n = 0;

	while (s[n])
		n++;
	sys(64, 1, (s64)s, n, 0);
}

struct pfd { int fd; short events, revents; };
struct ev { s64 sec, usec; u16 type, code; int value; };

void c_main(s64 *sp)
{
	s64 argc = sp[0];
	char **argv = (char **)(sp + 1);
	struct pfd p[8];
	int n = 0;

	for (int i = 1; i < argc && n < 8; i++) {
		p[n].fd = sys(56, -100, (s64)argv[i], 0, 0);
		p[n].events = 1;
		if (p[n].fd >= 0)
			n++;
	}
	if (!n)
		sys(93, 1, 0, 0, 0);
	for (;;) {
		if (sys(73, (s64)p, n, 0, 0) <= 0)	/* no timeout: wait for a key */
			continue;
		for (int i = 0; i < n; i++) {
			struct ev e;

			if (!(p[i].revents & 1))
				continue;
			while (sys(63, p[i].fd, (s64)&e, sizeof(e), 0) == (s64)sizeof(e)) {
				if (e.type == 1 && (e.value == 1 || (e.value == 2 && (e.code == 114 || e.code == 115)))) {
					if (e.code == 115)
						say("UP\n");
					else if (e.code == 114)
						say("DOWN\n");
					else if (e.code == 583)
						say("OK\n");
					else if (e.code == 116)
						say("POWER\n");
				}
				struct pfd q = { p[i].fd, 1, 0 };
				s64 zero[2] = { 0, 0 };

				if (sys(73, (s64)&q, 1, (s64)zero, 0) <= 0)
					break;
			}
		}
	}
}

__asm__(".global _start\n_start:\n\tmov x0, sp\n\tbl c_main\n");
