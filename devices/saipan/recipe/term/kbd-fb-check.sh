#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Runs the terminal with its keyboard on the real framebuffer, with the interface frozen, taps and holds some keys through the test FIFO and
# checks from framebuffer snapshots that (1) the keyboard and its tray were drawn, (2) not a pixel changed outside the terminal region and the
# keyboard region, (3) the touch-raw flag existed while it ran and is gone afterwards. About 12 seconds; the screen shows the terminal.
# Usage: sh kbd-fb-check.sh   (on the phone, in the build directory)
TX=32; TY=64; TW=672; TH=896; KX=32; KY=976; KW=672; KH=600
IN=/tmp/kc.in
UI=""
for p in /proc/[0-9]*; do [ "$(cat "$p/comm" 2> /dev/null)" = serverdroid-ui ] && UI=${p#/proc/}; done
[ -n "$UI" ] && kill -STOP "$UI"
B=$(ls -d /sys/class/backlight/* | head -n 1); echo 0 > $B/bl_power; echo 120 > $B/brightness
cat /dev/zero > /dev/fb0 2> /dev/null
./fbsnap /tmp/kc.a
rm -f $IN /run/touch-raw; mkfifo $IN
./serverdroid-term --input $IN --touch-offset 0 --once > /tmp/kc.out 2>&1 &
TP=$!
sleep 2
flag_during=no; [ -e /run/touch-raw ] && flag_during=yes
send() { echo "$1" > $IN; sleep 0.15; }
tap() { set -- $(./kbdtest center "$1"); send "P $1 $2"; send "R $1 $2"; }
tap h; tap i; tap Enter
sleep 0.5
./fbsnap /tmp/kc.b
set -- $(./kbdtest center a); send "P $1 $2"; sleep 0.6
./fbsnap /tmp/kc.c
send "R $1 $2"
sleep 0.5
# a long press on the first word of the terminal: highlight, handles and the bar over the terminal's own cells
send "P 56 80"; sleep 0.8
./fbsnap /tmp/kc.d
send "R 56 80"
sleep 0.5
echo EXIT > $IN; wait $TP
flag_after=gone; [ -e /run/touch-raw ] && flag_after=still-there
[ -n "$UI" ] && kill -CONT "$UI"
outside() {   # changed bytes of $1 vs $2 outside the terminal and keyboard regions
    cmp -l "$1" "$2" 2> /dev/null | awk -v tx=$TX -v ty=$TY -v tw=$TW -v th=$TH -v kx=$KX -v ky=$KY -v kw=$KW -v kh=$KH '
        { o = $1 - 1; y = int(o / 2880); x = int((o % 2880) / 4);
          inT = (x >= tx && x < tx + tw && y >= ty && y < ty + th); inK = (x >= kx && x < kx + kw && y >= ky && y < ky + kh);
          if (!inT && !inK) n++ }
        END { print n + 0 }'
}
echo "keyboard drawn: $(cmp -l /tmp/kc.a /tmp/kc.b 2> /dev/null | awk -v ky=$KY '{ y = int(($1 - 1) / 2880); if (y >= ky) n++ } END { print n + 0 }') changed bytes in the keyboard rows"
echo "tray drawn while a was held: $(cmp -l /tmp/kc.b /tmp/kc.c 2> /dev/null | wc -l) bytes differ from the idle keyboard"
echo "changed outside both regions: idle $(outside /tmp/kc.a /tmp/kc.b), tray $(outside /tmp/kc.a /tmp/kc.c)"
echo "selection drawn: $(cmp -l /tmp/kc.b /tmp/kc.d 2> /dev/null | wc -l) bytes differ from the idle terminal; outside both regions: $(outside /tmp/kc.a /tmp/kc.d)"
echo "touch-raw flag: $flag_during while running, $flag_after after"
cat /tmp/kc.out
rm -f /tmp/kc.a /tmp/kc.b /tmp/kc.c /tmp/kc.d $IN
