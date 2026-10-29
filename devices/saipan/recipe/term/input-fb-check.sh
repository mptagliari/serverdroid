#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Runs serverdroid-input on the real framebuffer with the interface frozen: types two letters through the test FIFO, confirms, and checks from
# snapshots that the screen changed only inside its rectangle (x 32, y 64, 672 x 1512), that the answer is what was typed, and that the
# raw-touch flag is gone afterwards. About 8 seconds; the screen shows the entry page meanwhile. Usage: sh input-fb-check.sh (on the phone)
IN=/tmp/ic.in
UI=""
for p in /proc/[0-9]*; do [ "$(cat "$p/comm" 2> /dev/null)" = serverdroid-ui ] && UI=${p#/proc/}; done
[ -n "$UI" ] && kill -STOP "$UI"
B=$(ls -d /sys/class/backlight/* | head -n 1); echo 0 > $B/bl_power; echo 120 > $B/brightness
cat /dev/zero > /dev/fb0 2> /dev/null
./fbsnap /tmp/ic.a
rm -f $IN /run/touch-raw; mkfifo $IN
./serverdroid-input --input $IN --touch-offset 0 --prompt "Password for test" --initial "ab" --password > /tmp/ic.out &
IP=$!
sleep 1.5
send() { echo "$1" > $IN; sleep 0.2; }
tap() { set -- $(./kbdtest center "$1"); send "P $1 $2"; send "R $1 $2"; }
flag=no; [ -e /run/touch-raw ] && flag=yes
tap o; tap k
sleep 0.6
./fbsnap /tmp/ic.b
tap Enter
wait $IP; rc=$?
[ -n "$UI" ] && kill -CONT "$UI"
echo "answer: '$(cat /tmp/ic.out)' status $rc; touch-raw flag $flag while running, $([ -e /run/touch-raw ] && echo still-there || echo gone) after"
echo "drawn: $(cmp -l /tmp/ic.a /tmp/ic.b 2> /dev/null | wc -l) changed bytes; outside the rectangle: $(cmp -l /tmp/ic.a /tmp/ic.b 2> /dev/null | awk '{ o = $1 - 1; y = int(o / 2880); x = int((o % 2880) / 4); if (x < 32 || x >= 704 || y < 64 || y >= 1576) n++ } END { print n + 0 }')"
rm -f /tmp/ic.a /tmp/ic.b $IN
