#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# A scripted demonstration of serverdroid-term on the real screen, plus a check that nothing outside its region was written.
# The interface is frozen meanwhile. Sentinel text is put on the console below the terminal (where the keyboard will be); the framebuffer is
# read before and after (through the same mmap the terminal draws with: read() of this fbdev does not show those writes) and every byte outside the terminal's rectangle must be identical.
# Usage: sh demo-fb.sh      (on the phone, in the directory of the build; about 50 seconds)
X=32; Y=64; W=672; H=896
IN=/run/term.in
UI=""
for p in /proc/[0-9]*; do [ "$(cat "$p/comm" 2> /dev/null)" = serverdroid-ui ] && UI=${p#/proc/}; done
[ -n "$UI" ] && kill -STOP "$UI"
B=$(ls -d /sys/class/backlight/* | head -n 1); echo 0 > $B/bl_power; echo 120 > $B/brightness
mark() { echo "term demo: $1 at $(date +%T)" > /dev/kmsg; }
send() { echo "$1" > $IN; }

# sentinel text under the terminal's region (console rows 37 to 46 of the 16x32 font)
{ printf '\033[0;30;47m\033[2J'; printf '\033[37;3H'; for i in 1 2 3 4 5 6 7 8 9 10; do printf '\033[%d;3H%s' $((36 + i)) "KEYBOARD AREA - must stay untouched $i"; done; } > /dev/tty1
sleep 1
./fbsnap /tmp/fb.before

mark "start"
rm -f $IN; mkfifo $IN
./serverdroid-term --no-kbd --region $X $Y $W $H --input $IN --once > /tmp/term.fb.out 2>&1 &
TP=$!
sleep 2
mark "1 accents"; send 'T echo "Terminal do ServerDroid: ação, ünï, ç, é"'; send 'K enter'; sleep 2
mark "2 ls --color"; send 'T ls --color=always /'; send 'K enter'; sleep 2
mark "3 colors"; send "T for i in 0 1 2 3 4 5 6 7; do printf \"\\033[3\${i}m####\\033[4\${i}m  \\033[0m \"; done; echo"; send 'K enter'; sleep 2
mark "4 many lines"; send 'T seq 1 90'; send 'K enter'; sleep 3
mark "5 scrollback 40 lines back"; send 'SCROLL 40'; sleep 4
mark "6 live again"; send 'SCROLL -100'; sleep 1
mark "7 vi"; send 'T vi /etc/profile'; send 'K enter'; sleep 4; send 'T :q'; send 'K enter'; sleep 2
mark "8 top"; send 'T top'; send 'K enter'; sleep 5; send 'T q'; sleep 1
mark "9 ctrl+c"; send 'T sleep 100'; send 'K enter'; sleep 1; send 'C c'; sleep 1
mark "10 resize to 20 rows x 30 columns"; send 'RESIZE 20 30'; sleep 1; send 'T stty size'; send 'K enter'; sleep 3
./fbsnap /tmp/fb.during
mark "11 leave"; send 'EXIT'
wait $TP; echo "terminal exit status $?"
sleep 1
./fbsnap /tmp/fb.after

# during the run every differing byte (cmp -l prints 1-based offsets) must lie inside the region; after leaving, the console is repainted from
# what the interface had, so the screen must be identical to the one before
outside=$(cmp -l /tmp/fb.before /tmp/fb.during 2> /dev/null | awk -v x0=$X -v y0=$Y -v w=$W -v h=$H '
    { o = $1 - 1; y = int(o / 2880); x = int((o % 2880) / 4);
      if (x < x0 || x >= x0 + w || y < y0 || y >= y0 + h) n++ }
    END { print n + 0 }')
during=$(cmp -l /tmp/fb.before /tmp/fb.during 2> /dev/null | wc -l)
after=$(cmp -l /tmp/fb.before /tmp/fb.after 2> /dev/null | wc -l)
echo "while the terminal ran: $during bytes changed, $outside outside its region"
echo "after leaving: $after bytes differ from before leaving the screen"
mark "done: $during changed, $outside outside the region, $after differ after leaving"
rm -f /tmp/fb.before /tmp/fb.during /tmp/fb.after
[ -n "$UI" ] && kill -CONT "$UI"
