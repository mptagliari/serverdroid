/* Save the framebuffer to a file through mmap (the way serverdroid-term draws): fbsnap FILE. Used by demo-fb.sh to check that nothing was drawn outside the terminal's region. */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	int fd = open("/dev/fb0", O_RDWR), out;
	size_t len;
	unsigned char *p;

	if (argc < 2 || fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0)
		return 1;
	len = (size_t)f.line_length * v.yres;
	p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
	out = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (p == MAP_FAILED || out < 0 || write(out, p, len) != (ssize_t)len)
		return 1;
	return 0;
}
