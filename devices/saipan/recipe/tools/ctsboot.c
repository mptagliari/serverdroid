/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * Start the Chipone TDDI touch controller (ICNL9911C) from its RAM (freestanding aarch64, no libc).
 * Usage: ctsboot SPIDEV FIRMWARE.bin [go|reset|flash|park]   e.g. ctsboot /dev/spidev0.0 /lib/firmware/ICNL9911.bin go
 * "park" puts the chip back in SPI program mode (reset pulse, magic bytes, mode register must read 5): the state in which the bootloader finds
 * it and in which it must be left before a warm reboot, or the bootloader cannot start the panel (black screen with the backlight on).
 * "flash" only pulses the reset (no magic, nothing written) so the chip starts the firmware stored in its own flash, then prints what it reports
 * (the vendor's resume path); the firmware file is not used.
 * "reset" is "go" preceded by the vendor's reset of the chip: GPIO15 (through /dev/gpiochip0) high 1 ms, low 10 ms, high 40 ms, then the
 * program-mode magic bytes CC 33 55 5A (what cts_enter_program_mode() does). The chip's reset line is the only thing it touches besides the above.
 * Without "go" it only checks the preconditions and prints the checksum; with "go" it does the vendor driver's RAM-boot sequence
 * (docs/touch-input-investigation.md, stage T2):
 *   1. the chip must already be in SPI program mode (mode register 0x30011 == 5) and report hardware id 0x9911xx;
 *   2. write the whole firmware file to the chip's RAM at address 0, in 1 KB pieces, and read it back to compare;
 *   3. write the boot parameters {0x015ff0 = 0xCC33555A, 0x08fffc = CRC-32 of the file, 0x08fff8 = size};
 *   4. write 3 (boot from RAM) to the boot mode register 0x30010, wait 100 ms, and read the firmware's type and version in normal mode.
 * What it never does (unless "reset" is given: only the reset pulse above): touch the chip's flash controller or flash (no 0x30033, 0x30008, no erase or program), or write any
 * register other than those listed. All of it is volatile: a power cycle returns the chip to its state before.
 *   aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os -o ctsboot tools/ctsboot.c
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

static long fd;
static u8 fw[0x20000], tx[1500], rx[1500];

static void put(const char *s)
{
	long n = 0;
	while (s[n])
		n++;
	sys(64, 1, (s64)s, n);
}

static void puthex(u32 v, int digits)
{
	char b[12];
	for (int i = 0; i < digits; i++)
		b[i] = "0123456789abcdef"[(v >> (4 * (digits - 1 - i))) & 15];
	b[digits] = 0;
	put(b);
}

static void putdec(u32 v)
{
	char b[12];
	int i = 11;
	b[i] = 0;
	do {
		b[--i] = '0' + v % 10;
		v /= 10;
	} while (v);
	put(b + i);
}

static void die(const char *m)
{
	put(m);
	put("\n");
	sys(93, 1, 0, 0);
}

static void delay_ms(long ms)
{
	s64 ts[2] = { ms / 1000, (ms % 1000) * 1000000 };
	sys(101, (s64)ts, 0, 0);
}

/* GPIO v2 character device: request line 15 of /dev/gpiochip0 as an output, initially high, and return the line fd */
struct gpio_v2_line_request {
	u32 offsets[64];
	char consumer[32];
	struct { u64 flags; u32 num_attrs; u32 padding[5]; u8 attrs[240]; } config;
	u32 num_lines, event_buffer_size, padding[5];
	int fd;
};
struct gpio_v2_line_values { u64 bits, mask; };

static long reset_line_open(void)
{
	static struct gpio_v2_line_request rq;
	long chip = sys(56, -100, (s64)"/dev/gpiochip0", 2);
	const char *name = "ctsboot";

	if (chip < 0)
		die("cannot open /dev/gpiochip0");
	rq.offsets[0] = 15;
	for (int i = 0; name[i]; i++)
		rq.consumer[i] = name[i];
	rq.config.flags = 1 << 3;		/* GPIO_V2_LINE_FLAG_OUTPUT */
	rq.num_lines = 1;
	if (sys(29, chip, 0xC250B407 /* GPIO_V2_GET_LINE_IOCTL */, (s64)&rq) < 0)
		die("cannot take GPIO15 (reset)");
	return rq.fd;
}

static void reset_line_set(long line, int v)
{
	struct gpio_v2_line_values vals = { .bits = v ? 1 : 0, .mask = 1 };
	sys(29, line, 0xC010B40F /* GPIO_V2_LINE_SET_VALUES_IOCTL */, (s64)&vals);
}

static u32 crc32(const u8 *d, u32 len)	/* the vendor's cts_crc32: polynomial 0x04C11DB7, MSB first, initial value 0, no final xor */
{
	u32 crc = 0;
	while (len--) {
		crc ^= (u32)*d++ << 24;
		for (int i = 0; i < 8; i++)
			crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
	}
	return crc;
}

static int xfer(u8 *t, u8 *r, u32 len)
{
	struct spi_ioc_transfer x = { .tx_buf = (u64)t, .rx_buf = (u64)r, .len = len, .speed_hz = 4800000, .bits_per_word = 8 };
	return sys(29, fd, 0x40206b00, (s64)&x) < 0;
}

/* program mode: write {0x60, addr[23:0] big endian, data...} */
static void pwrite(u32 addr, const u8 *d, u32 len)
{
	if (len > 1400)
		die("write too long");
	tx[0] = 0x60;
	tx[1] = addr >> 16;
	tx[2] = addr >> 8;
	tx[3] = addr;
	for (u32 i = 0; i < len; i++)
		tx[4 + i] = d[i];
	if (xfer(tx, rx, len + 4))
		die("spi write failed");
}

/* program mode: read {0x61, addr...} then len + 1 filler bytes; the data starts at byte 5 of the answer */
static void pread(u32 addr, u8 *d, u32 len)
{
	if (len > 1400)
		die("read too long");
	for (u32 i = 0; i < len + 5; i++)
		tx[i] = 0;
	tx[0] = 0x61;
	tx[1] = addr >> 16;
	tx[2] = addr >> 8;
	tx[3] = addr;
	if (xfer(tx, rx, len + 5))
		die("spi read failed");
	for (u32 i = 0; i < len; i++)
		d[i] = rx[5 + i];
}

static void pwritel(u32 addr, u32 v)
{
	u8 b[4] = { v, v >> 8, v >> 16, v >> 24 };
	pwrite(addr, b, 4);
}

/* normal mode read (see ctsread.c): command {0xF1, addr low, addr high, len LE16, CRC16}, pause, then len + 2 bytes */
static int nread(u32 addr, u8 *d, u32 len)
{
	static u8 t[80], r[80];
	u32 crc, z = 0;

	for (int i = 0; i < 80; i++)
		t[i] = 0;
	t[0] = 0xf1;
	t[1] = addr;
	t[2] = addr >> 8;
	t[3] = len;
	t[4] = len >> 8;
	crc = crc32(t, 5);
	t[5] = crc;
	t[6] = crc >> 8;
	if (xfer(t, r, 7))
		return -1;
	delay_ms(1);
	for (int i = 0; i < 80; i++)
		t[i] = 0;
	t[0] = 0xf1;
	if (xfer(t, r, len + 2))
		return -1;
	for (u32 i = 0; i < len; i++) {
		d[i] = r[i];
		z += r[i] == 0;
	}
	crc = crc32(r, len) & 0xffff;
	if ((r[len] | (r[len + 1] << 8)) != crc)
		return -2;
	return z == len ? -3 : 0;	/* a valid CRC over all zeros is no answer */
}

void c_main(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	u8 b[8], mode = 0;
	u32 size = 0, crc, id, go = argc > 3 && (argv[3][0] == 'g' || argv[3][0] == 'r');
	long ffd;
	s64 n;

	if (argc < 3)
		die("usage: ctsboot SPIDEV FIRMWARE.bin [go]");
	fd = sys(56, -100, (s64)argv[1], 2);
	ffd = sys(56, -100, (s64)argv[2], 0);
	if (fd < 0 || ffd < 0)
		die("cannot open the spi device or the firmware file");
	while ((n = sys(63, ffd, (s64)(fw + size), sizeof(fw) - size)) > 0)
		size += n;
	if ((size < 1024 || size >= sizeof(fw)) && !(argc > 3 && (argv[3][0] == 'p' || argv[3][0] == 'f')))	/* park and flash do not use the file */
		die("unexpected firmware size");
	crc = crc32(fw, size);
	put("firmware ");
	putdec(size);
	put(" bytes, crc32 ");
	puthex(crc, 8);
	put("\n");

	b[0] = 0;
	sys(29, fd, 0x40016b01, (s64)b);		/* SPI mode 0 */
	if (argc > 3 && argv[3][0] == 'p') {		/* park: reset, then the program-mode magic, then check the mode */
		static const u8 magic[3] = { 0x33, 0x55, 0x5a };
		long line = reset_line_open();
		u8 m = 0;
		reset_line_set(line, 1);
		delay_ms(1);
		reset_line_set(line, 0);
		delay_ms(10);
		reset_line_set(line, 1);
		delay_ms(40);
		sys(57, line, 0, 0);
		for (int t = 0; t < 5 && (m & 7) != 5; t++) {
			tx[0] = 0xcc;
			for (int i = 0; i < 3; i++)
				tx[1 + i] = magic[i];
			if (xfer(tx, rx, 4))
				die("magic write failed");
			delay_ms(5);
			pread(0x30011, &m, 1);
		}
		put((m & 7) == 5 ? "parked: SPI program mode\n" : "not in program mode\n");
		sys(93, (m & 7) == 5 ? 0 : 1, 0, 0);
	}
	if (argc > 3 && argv[3][0] == 'f') {		/* reset only: the chip boots the firmware in its flash */
		long line = reset_line_open();
		u8 d[2];
		reset_line_set(line, 1);
		delay_ms(1);
		reset_line_set(line, 0);
		delay_ms(10);
		reset_line_set(line, 1);
		delay_ms(40);
		sys(57, line, 0, 0);
		for (int t = 0; t < 40; t++) {
			delay_ms(100);
			if (nread(0x000a, d, 2) == 0) {
				put("flash firmware answers in normal mode after ");
				putdec((t + 1) * 100);
				put(" ms: chip type ");
				puthex(d[0] | (d[1] << 8), 4);
				put("\n");
				break;
			}
		}
		{
			static const struct { u32 a, l; const char *n; } regs[] = {
				{ 0x0009, 1, "lib main version" }, { 0x000c, 2, "version" }, { 0x0010, 1, "ddi version" },
				{ 0x8007, 1, "tx" }, { 0x8008, 1, "rx" }, { 0x8090, 2, "x resolution" }, { 0x8092, 2, "y resolution" },
			};
			int ok = 0;
			for (u32 k = 0; k < sizeof(regs) / sizeof(regs[0]); k++) {
				u8 d2[4] = { 0 };
				int r = nread(regs[k].a, d2, regs[k].l);
				put(regs[k].n);
				put(": ");
				if (r < 0 && r != -3) {
					put("error\n");
					continue;
				}
				ok++;
				for (u32 i = 0; i < regs[k].l; i++) {
					puthex(d2[i], 2);
					put(" ");
				}
				put("\n");
			}
			sys(93, ok ? 0 : 1, 0, 0);
		}
	}
	if (argc > 3 && argv[3][0] == 'r') {		/* the vendor's reset, then program mode */
		static const u8 magic[3] = { 0x33, 0x55, 0x5a };
		long line = reset_line_open();
		reset_line_set(line, 1);
		delay_ms(1);
		reset_line_set(line, 0);
		delay_ms(10);
		reset_line_set(line, 1);
		delay_ms(40);
		tx[0] = 0xcc;
		for (int i = 0; i < 3; i++)
			tx[1 + i] = magic[i];
		if (xfer(tx, rx, 4))
			die("magic write failed");
		put("reset pulsed, magic sent\n");
		sys(57, line, 0, 0);		/* release the line (it stays high) */
	}
	pread(0x30000, b, 4);
	id = b[0] | (b[1] << 8) | (b[2] << 16) | ((u32)b[3] << 24);
	pread(0x30011, &mode, 1);
	put("hardware id ");
	puthex(id, 8);
	put(", mode ");
	putdec(mode & 7);
	put("\n");
	if ((id & 0xfffffff0u) != 0x991110u)
		die("not an ICNL9911C: stop");
	if ((mode & 7) != 5)
		die("the chip is not in SPI program mode: stop");
	if (!go) {
		put("preconditions ok; add \"go\" to load and start the firmware\n");
		sys(93, 0, 0, 0);
	}

	{
		u32 retries = 0;
		for (u32 off = 0; off < size; off += 1024) {
			static u8 back[1024];
			u32 l = size - off < 1024 ? size - off : 1024, try, bad;

			for (try = 0; try < 6; try++) {		/* each piece is written and read back at once; the link drops a piece now and then */
				pwrite(off, fw + off, l);
				pread(off, back, l);
				for (bad = 0; bad < l && back[bad] == fw[off + bad]; bad++)
					;
				if (bad == l)
					break;
				retries++;
			}
			if (try == 6) {
				put("piece at ");
				puthex(off, 6);
				die(" does not read back: stop (nothing was started)");
			}
		}
		put("pieces repeated: ");
		putdec(retries);
		put("\n");
	}
	put("firmware written to RAM and verified\n");

	pwritel(0x015ff0, 0xCC33555A);
	pwritel(0x08fffc, crc);
	pwritel(0x08fff8, size);
	{
		u8 m10, m11;
		pread(0x30010, &m10, 1);
		put("before boot: 0x30010 = ");
		puthex(m10, 2);
		b[0] = 3;
		pwrite(0x30010, b, 1);			/* boot from RAM */
		pread(0x30010, &m10, 1);
		pread(0x30011, &m11, 1);
		put(", after the write: 0x30010 = ");
		puthex(m10, 2);
		put(" 0x30011 = ");
		puthex(m11, 2);
		put("\n");
	}
	for (int t = 0; t < 30; t++) {			/* give the firmware up to 3 s to come up */
		u8 d[2];
		delay_ms(100);
		if (nread(0x000a, d, 2) == 0) {
			put("answers in normal mode after ");
			putdec((t + 1) * 100);
			put(" ms\n");
			break;
		}
	}
	{
		static const struct { u32 a, l; const char *n; } regs[] = {
			{ 0x0000, 1, "work mode" }, { 0x0009, 1, "lib main version" }, { 0x000a, 2, "chip type" }, { 0x000c, 2, "version" },
			{ 0x8007, 1, "tx" }, { 0x8008, 1, "rx" }, { 0x8090, 2, "x resolution" }, { 0x8092, 2, "y resolution" },
		};
		int ok = 0;
		for (u32 k = 0; k < sizeof(regs) / sizeof(regs[0]); k++) {
			u8 d[4] = { 0 };
			int r = nread(regs[k].a, d, regs[k].l);
			put(regs[k].n);
			put(": ");
			if (r < 0) {
				put(r == -3 ? "zero\n" : r == -2 ? "crc error\n" : "transfer error\n");
				continue;
			}
			ok++;
			for (u32 i = 0; i < regs[k].l; i++) {
				puthex(d[i], 2);
				put(" ");
			}
			put("\n");
		}
		put(ok ? "the firmware answers in normal mode\n" : "no answer in normal mode\n");
		sys(93, ok ? 0 : 1, 0, 0);
	}
}

__asm__(".global _start\n_start:\n\tmov x0, sp\n\tbl c_main\n");
