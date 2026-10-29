/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * Touch service for the Chipone TDDI controller: starts the chip and turns what it reports into the tokens of the on-screen interface
 * (freestanding aarch64, no libc).
 * Usage: ctsd SPIDEV FIFO       e.g. ctsd /dev/spidev0.0 /run/keys.fifo       (with "-" as FIFO the tokens go to standard output)
 *
 * Start: pulses the chip's reset (GPIO15 through /dev/gpiochip0: high 1 ms, low 10 ms, high 40 ms, as the vendor driver does), after which
 * the chip runs the firmware stored in its own flash, and waits for it to answer in normal mode (the reset is repeated up to three times).
 * Nothing is written to the chip. Then it polls the touch report (register 0x1000) every 8 ms while touched and every 25 ms when idle.
 * Tokens, one per line, the same FIFO and style as /usr/local/bin/keys:
 *   DOWN / UP   the finger dragged 32 pixels up / down (one console row of the 16x32 font), repeated while the drag goes on: touch scrolling
 *   TAP x y     a short touch that did not move (raw panel coordinates, 0..719 and 0..1599)
 *   LONG x y    a touch held for 700 ms without moving
 * While the file /run/touch-raw exists (the terminal creates it while it runs) a touch that starts is not summarized but passed on as it
 * happens, for the on-screen keyboard: "P x y" when the finger goes down, "M x y" when it has moved 4 pixels, "R x y" when it lifts.
 * Only the first finger is followed. A report that fails its CRC is skipped (the link drops one now and then).
 * See docs/touch-input-investigation.md.
 *   aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os -o ctsd tools/ctsd.c
 */
typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long s64;

static s64 sys(s64 n, s64 a, s64 b, s64 c)
{
	register s64 x8 __asm__("x8") = n, x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
	return x0;
}

struct spi_ioc_transfer {
	u64 tx_buf, rx_buf;
	u32 len, speed_hz;
	u16 delay_usecs;
	u8 bits_per_word, cs_change, tx_nbits, rx_nbits, word_delay_usecs, pad;
};
struct gpio_v2_line_request {
	u32 offsets[64];
	char consumer[32];
	struct { u64 flags; u32 num_attrs; u32 padding[5]; u8 attrs[240]; } config;
	u32 num_lines, event_buffer_size, padding[5];
	int fd;
};
struct gpio_v2_line_values { u64 bits, mask; };

static long fd, out = 1;
static u8 tx[96], rx[96];

static void say(const char *s)
{
	long n = 0;
	while (s[n])
		n++;
	sys(64, out, (s64)s, n);
}

static int fmt(char *b, u32 v)
{
	char t[12];
	int i = 0, n = 0;
	do {
		t[i++] = '0' + v % 10;
		v /= 10;
	} while (v);
	while (i)
		b[n++] = t[--i];
	return n;
}

static void token(const char *name, int with_xy, u32 x, u32 y)	/* one write per line so that lines never interleave in the FIFO */
{
	char b[48];
	int n = 0;
	while (*name)
		b[n++] = *name++;
	if (with_xy) {
		b[n++] = ' ';
		n += fmt(b + n, x);
		b[n++] = ' ';
		n += fmt(b + n, y);
	}
	b[n++] = '\n';
	sys(64, out, (s64)b, n);
}

static void delay_ms(long ms)
{
	s64 ts[2] = { ms / 1000, (ms % 1000) * 1000000 };
	sys(101, (s64)ts, 0, 0);
}

static long now_ms(void)
{
	s64 ts[2];
	sys(113, 1, (s64)ts, 0);
	return ts[0] * 1000 + ts[1] / 1000000;
}

static u32 crc32(const u8 *d, u32 len)
{
	u32 crc = 0;
	while (len--) {
		crc ^= (u32)*d++ << 24;
		for (int i = 0; i < 8; i++)
			crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
	}
	return crc;
}

static int xfer(u32 len)
{
	struct spi_ioc_transfer x = { .tx_buf = (u64)tx, .rx_buf = (u64)rx, .len = len, .speed_hz = 4800000, .bits_per_word = 8 };
	return sys(29, fd, 0x40206b00, (s64)&x) < 0;
}

static int nread(u32 addr, u8 *d, u32 len)	/* normal mode read, see tools/ctsread.c; 0 when the CRC matched */
{
	u32 crc;

	for (u32 i = 0; i < 8; i++)
		tx[i] = 0;
	tx[0] = 0xf1;
	tx[1] = addr;
	tx[2] = addr >> 8;
	tx[3] = len;
	tx[4] = len >> 8;
	crc = crc32(tx, 5);
	tx[5] = crc;
	tx[6] = crc >> 8;
	if (xfer(7))
		return -1;
	{
		s64 ts[2] = { 0, 150000 };
		sys(101, (s64)ts, 0, 0);
	}
	for (u32 i = 0; i < len + 2; i++)
		tx[i] = 0;
	tx[0] = 0xf1;
	if (xfer(len + 2))
		return -1;
	for (u32 i = 0; i < len; i++)
		d[i] = rx[i];
	crc = crc32(rx, len) & 0xffff;
	return (rx[len] | (rx[len + 1] << 8)) == crc ? 0 : -2;
}

static void reset_chip(void)
{
	static struct gpio_v2_line_request rq;
	struct gpio_v2_line_values v = { .bits = 1, .mask = 1 };
	long chip = sys(56, -100, (s64)"/dev/gpiochip0", 2);
	const char *name = "ctsd";

	if (chip < 0)
		return;
	rq.offsets[0] = 15;
	for (int i = 0; name[i]; i++)
		rq.consumer[i] = name[i];
	rq.config.flags = 1 << 3;		/* output */
	rq.num_lines = 1;
	if (sys(29, chip, 0xC250B407, (s64)&rq) < 0) {
		sys(57, chip, 0, 0);
		return;
	}
	sys(29, rq.fd, 0xC010B40F, (s64)&v);
	delay_ms(1);
	v.bits = 0;
	sys(29, rq.fd, 0xC010B40F, (s64)&v);
	delay_ms(10);
	v.bits = 1;
	sys(29, rq.fd, 0xC010B40F, (s64)&v);
	delay_ms(40);
	sys(57, rq.fd, 0, 0);
	sys(57, chip, 0, 0);
}

static int start_chip(void)
{
	for (int attempt = 0; attempt < 3; attempt++) {
		u8 d[2];
		reset_chip();
		for (int t = 0; t < 30; t++) {
			delay_ms(100);
			if (nread(0x000a, d, 2) == 0 && (d[0] | d[1]))
				return 0;
		}
	}
	return -1;
}

void c_main(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	u8 d[72], mode = 0;
	int down = 0, moved = 0, id = -1, sent_long = 0, raw = 0;
	long t_down = 0, t_last_touch = 0;
	u32 x0 = 0, y0 = 0, x = 0, y = 0, anchor = 0, lx = 0, ly = 0;

	if (argc < 3)
		sys(93, 2, 0, 0);
	fd = sys(56, -100, (s64)argv[1], 2);
	if (fd < 0) {
		say("ctsd: cannot open the spi device\n");
		sys(93, 1, 0, 0);
	}
	if (!(argv[2][0] == '-' && argv[2][1] == 0)) {
		out = sys(56, -100, (s64)argv[2], 2 | 04000 /* O_RDWR | O_NONBLOCK */);
		if (out < 0) {
			sys(33, (s64)argv[2], 010600 /* FIFO, 0600 */, 0);	/* mknod: the interface may not have made the FIFO yet */
			out = sys(56, -100, (s64)argv[2], 2 | 04000);
		}
		if (out < 0)
			sys(93, 1, 0, 0);
	}
	sys(29, fd, 0x40016b01, (s64)&mode);
	if (start_chip()) {
		say("ctsd: the touch controller does not answer\n");
		sys(93, 1, 0, 0);
	}
	for (;;) {
		long now = now_ms();
		int n, i, found = -1;

		if (nread(0x1000, d, 72) != 0) {
			delay_ms(8);
			continue;
		}
		n = d[1];
		if (n > 10)
			n = 0;			/* nonsense count: a corrupted report */
		for (i = 0; i < n; i++) {		/* follow the finger already followed, else the first one that is down */
			const u8 *p = d + 2 + 7 * i;
			if (p[6] == 4)
				continue;
			if (id >= 0 && p[0] == id) {
				found = i;
				break;
			}
			if (id < 0 && found < 0)
				found = i;
		}
		if (found >= 0) {
			const u8 *p = d + 2 + 7 * found;
			x = p[1] | (p[2] << 8);
			y = p[3] | (p[4] << 8);
			t_last_touch = now;
			if (!down) {
				down = 1;
				moved = 0;
				sent_long = 0;
				id = p[0];
				x0 = x;
				y0 = y;
				anchor = y;
				t_down = now;
				raw = sys(48, -100, (s64)"/run/touch-raw", 0) == 0;	/* faccessat: the terminal wants the touches as they happen */
				lx = x;
				ly = y;
				if (raw)
					token("P", 1, x, y);
			} else if (raw) {
				int ddx = (int)x - (int)lx, ddy = (int)y - (int)ly;
				if (ddx >= 4 || ddx <= -4 || ddy >= 4 || ddy <= -4) {
					token("M", 1, x, y);
					lx = x;
					ly = y;
				}
			} else {
				int dy = (int)y - (int)anchor;
				int dx = (int)x - (int)x0;
				if (!moved && ((dy < 0 ? -dy : dy) > 24 || (dx < 0 ? -dx : dx) > 40))
					moved = 1;
				while (moved && (int)y - (int)anchor <= -32) {	/* finger moved up: the content goes up, the selection down */
					token("DOWN", 0, 0, 0);
					anchor -= 32;
				}
				while (moved && (int)y - (int)anchor >= 32) {
					token("UP", 0, 0, 0);
					anchor += 32;
				}
				if (!moved && !sent_long && now - t_down >= 700) {
					token("LONG", 1, x, y);
					sent_long = 1;
				}
			}
		} else if (down) {			/* released */
			if (raw)
				token("R", 1, x, y);
			else if (!moved && !sent_long && now - t_down < 700)
				token("TAP", 1, x0, y0);
			down = 0;
			id = -1;
		}
		delay_ms((down || now - t_last_touch < 1000) ? 8 : 25);
	}
}

__asm__(".global _start\n_start:\n\tmov x0, sp\n\tbl c_main\n");
