/*
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
 * Read registers of the Chipone TDDI touch controller (ICNL9911C) over spidev, in its normal mode (freestanding aarch64, no libc).
 * Usage: ctsread SPIDEV ADDRESS LENGTH [p]     e.g. ctsread /dev/spidev0.0 0x000a 2      (address and length in hexadecimal or decimal)
 * With a fourth argument "p" the read uses the program-mode framing (0x61, a 24 bit address, 5 bytes of overhead, no CRC) for the chip's hardware
 * registers (hardware id at 0x30000, boot mode at 0x30010); it still sends no magic number and does not reset the chip.
 * This only READS: the frame is the vendor driver's normal-mode read (docs/touch-input-investigation.md): a 7 byte command
 * {0xF1, address low, address high, length LE16, CRC16 LE}, a pause, then a read of length + 2 bytes whose last two bytes are the CRC of
 * the data. It never resets the chip, never enters the chip's program mode and never writes a register or the chip's flash.
 * Prints "ADDR: bytes" and whether the CRC matched; exit status 1 if it did not.
 *   aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os -o ctsread tools/ctsread.c
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

static u32 parse(const char *s)
{
	u32 v = 0, base = 10;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	}
	for (; *s; s++)
		v = v * base + (*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
	return v;
}

/* CRC-32 with polynomial 0x04C11DB7, MSB first, initial value 0, no final xor: the vendor's cts_crc32(), computed bit by bit */
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

static void delay_us(long us)
{
	s64 ts[2] = { 0, us * 1000 };
	sys(101, (s64)ts, 0, 0);
}

static int xfer(long fd, u8 *tx, u8 *rx, u32 len)
{
	struct spi_ioc_transfer t = { .tx_buf = (u64)tx, .rx_buf = (u64)rx, .len = len, .speed_hz = 4800000, .bits_per_word = 8 };
	return sys(29, fd, 0x40206b00 /* SPI_IOC_MESSAGE(1) */, (s64)&t) < 0;
}

void c_main(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	u8 tx[8] = { 0 }, rxc[8], rx[80], ctl;
	u32 addr, len, crc;
	long fd;

	if (argc < 4)
		sys(93, 2, 0, 0);
	addr = parse(argv[2]);
	len = parse(argv[3]);
	if (len == 0 || len > 64)
		sys(93, 2, 0, 0);
	fd = sys(56, -100, (s64)argv[1], 2 /* O_RDWR */);
	if (argc > 4) {			/* program-mode read: {0x61, addr[23:16], addr[15:8], addr[7:0]} then len + 1 filler bytes */
		static u8 txp[80], rxp[80];
		if (fd < 0)
			sys(93, 1, 0, 0);
		ctl = 0;
		sys(29, fd, 0x40016b01, (s64)&ctl);
		txp[0] = 0x61;
		txp[1] = addr >> 16;
		txp[2] = addr >> 8;
		txp[3] = addr;
		if (xfer(fd, txp, rxp, len + 5)) {
			put("transfer failed\n");
			sys(93, 1, 0, 0);
		}
		puthex(addr, 6);
		put(" (program mode):");
		for (u32 i = 0; i < len; i++) {
			put(" ");
			puthex(rxp[5 + i], 2);
		}
		put("   raw:");
		for (u32 i = 0; i < len + 5; i++) {
			put(" ");
			puthex(rxp[i], 2);
		}
		put("\n");
		sys(93, 0, 0, 0);
	}
	if (fd < 0) {
		put("cannot open the spi device\n");
		sys(93, 1, 0, 0);
	}
	ctl = 0;			/* SPI mode 0 */
	sys(29, fd, 0x40016b01 /* SPI_IOC_WR_MODE */, (s64)&ctl);
	tx[0] = 0xf1;
	tx[1] = addr & 0xff;	/* low byte first on the wire (the vendor swaps the bytes of its big-endian address) */
	tx[2] = addr >> 8;
	tx[3] = len & 0xff;
	tx[4] = len >> 8;
	crc = crc32(tx, 5);
	tx[5] = crc & 0xff;
	tx[6] = (crc >> 8) & 0xff;
	if (xfer(fd, tx, rxc, 7)) {
		put("command transfer failed\n");
		sys(93, 1, 0, 0);
	}
	delay_us(100);
	for (int i = 0; i < 8; i++)
		tx[i] = 0;
	tx[0] = 0xf1;
	{
		static u8 txr[80];
		txr[0] = 0xf1;
		if (xfer(fd, txr, rx, len + 2)) {
			put("read transfer failed\n");
			sys(93, 1, 0, 0);
		}
	}
	{
		u32 z = 0, f = 0;
		for (u32 i = 0; i < len + 2; i++) {
			z += rx[i] == 0x00;
			f += rx[i] == 0xff;
		}
		if (z == len + 2 || f == len + 2) {	/* the CRC of all zeros is zero: a dead line looks valid */
			put(z == len + 2 ? "no answer (all zero)\n" : "no answer (all 0xff)\n");
			sys(93, 1, 0, 0);
		}
	}
	puthex(addr, 4);
	put(":");
	for (u32 i = 0; i < len; i++) {
		put(" ");
		puthex(rx[i], 2);
	}
	crc = crc32(rx, len) & 0xffff;
	put((rx[len] | (rx[len + 1] << 8)) == crc ? "  crc ok\n" : "  crc MISMATCH (raw tail ");
	if ((rx[len] | (rx[len + 1] << 8)) != crc) {
		puthex(rx[len], 2);
		put(" ");
		puthex(rx[len + 1], 2);
		put(")\n");
		sys(93, 1, 0, 0);
	}
	sys(93, 0, 0, 0);
}

__asm__(".global _start\n_start:\n\tmov x0, sp\n\tbl c_main\n");
