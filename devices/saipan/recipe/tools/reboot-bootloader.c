/* Reboot the phone into the bootloader (fastboot) from a running Linux.
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 *
 * BusyBox reboot cannot pass an argument to the reboot syscall; the Motorola/MediaTek
 * kernel reads the "bootloader" argument and tells the bootloader to stay in fastboot,
 * as Android's "adb reboot bootloader" does.
 *
 * Freestanding aarch64 program (no libc), so it builds with a bare cross compiler:
 *   aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os -o reboot-bootloader reboot-bootloader.c
 */
#define SYS_sync   81
#define SYS_reboot 142
#define MAGIC1     0xfee1dead
#define MAGIC2     672274793 /* 0x28121969 */
#define RESTART2   0xA1B2C3D4

static long sys4(long n, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return x0;
}

void _start(void)
{
	sys4(SYS_sync, 0, 0, 0, 0);
	sys4(SYS_reboot, MAGIC1, MAGIC2, RESTART2, (long)"bootloader");
	for (;;)
		;
}
