# Native build, on the phone: apk add gcc musl-dev make linux-headers libvterm-dev zlib-dev font-terminus
# Cross build, on the PC (what rootfs/build.sh does): make SYSROOT=/path/to/aarch64-alpine-sysroot
#   the sysroot is an Alpine aarch64 root with musl-dev libvterm-dev zlib-dev linux-headers installed (rootfs/build.sh makes one); the host's
#   aarch64-linux-gnu-gcc is used with the sysroot's musl headers and libraries, linked dynamically against musl (the image has libvterm).
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
SRC = term.c vt.c pty.c font.c render_fb.c render_dump.c input.c kbd.c kbd_draw.c haptic.c host.c sel.c
INPUT_SRC = input_app.c input.c kbd.c kbd_draw.c haptic.c host.c font.c

ifdef SYSROOT
CC = aarch64-linux-gnu-gcc
CFLAGS = -O2 -Wall -Wextra -std=gnu11 -fno-stack-protector -U_FORTIFY_SOURCE -nostdinc \
	-isystem $(shell $(CC) -print-file-name=include) -isystem $(SYSROOT)/usr/include
CRT0 = $(SYSROOT)/usr/lib/crt1.o $(SYSROOT)/usr/lib/crti.o
CRTN = $(SYSROOT)/usr/lib/crtn.o
LINK = -nostdlib -no-pie -Wl,--dynamic-linker,/lib/ld-musl-aarch64.so.1 -L$(SYSROOT)/usr/lib -L$(SYSROOT)/lib
LIBGCC = $(shell $(CC) -print-libgcc-file-name)
ZV = -lz -l:ld-musl-aarch64.so.1 $(LIBGCC)
else
ZV = -lz
endif

all: serverdroid-term serverdroid-input kbdtest
ifndef SYSROOT
all: fbsnap
endif

fbsnap: fbsnap.c
	$(CC) $(CFLAGS) -o $@ fbsnap.c

serverdroid-term: $(SRC) $(wildcard *.h)
	$(CC) $(CFLAGS) $(LINK) -o $@ $(CRT0) $(SRC) -lvterm $(ZV) $(CRTN)

serverdroid-input: $(INPUT_SRC) $(wildcard *.h)
	$(CC) $(CFLAGS) $(LINK) -o $@ $(CRT0) $(INPUT_SRC) $(ZV) $(CRTN)

kbdtest: kbdtest.c kbd.c kbd.h input.h
	$(CC) $(CFLAGS) $(LINK) -o $@ $(CRT0) kbdtest.c kbd.c $(if $(SYSROOT),-l:ld-musl-aarch64.so.1 $(LIBGCC)) $(CRTN)

clean:
	rm -f serverdroid-term serverdroid-input fbsnap kbdtest
