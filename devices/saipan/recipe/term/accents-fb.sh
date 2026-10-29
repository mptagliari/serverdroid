#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# A short visual check of the fonts on the real screen (about 14 seconds): Portuguese accents, bold, underline, colors, and the larger region.
# The interface is frozen meanwhile and comes back at the end. Usage: sh accents-fb.sh   (on the phone, in the build directory)
IN=/run/term.in
UI=""
for p in /proc/[0-9]*; do [ "$(cat "$p/comm" 2> /dev/null)" = serverdroid-ui ] && UI=${p#/proc/}; done
[ -n "$UI" ] && kill -STOP "$UI"
B=$(ls -d /sys/class/backlight/* | head -n 1); echo 0 > $B/bl_power; echo 120 > $B/brightness
cat /dev/zero > /dev/fb0 2> /dev/null
rm -f $IN; mkfifo $IN
./serverdroid-term --no-kbd --input $IN --once > /tmp/term.acc.out 2>&1 &
TP=$!
sleep 2
send() { echo "$1" > $IN; sleep 0.8; }
send "T echo 'minúsculas: ação coração órfão pé avó âmbar ê ô õ à ü ç ñ'"; send 'K enter'
send "T echo 'MAIÚSCULAS: AÇÃO CORAÇÃO ÓRFÃO PÉ AVÓ ÂMBAR Ê Ô Õ À Ü Ç Ñ'"; send 'K enter'
send "T printf 'normal \\033[1mNEGRITO\\033[0m \\033[4msublinhado\\033[0m \\033[7mreverso\\033[0m \\033[31mvermelho\\033[0m \\033[1;34mazul-negrito\\033[0m\\n'"; send 'K enter'
send "T seq 1 31 | tr '\\n' ' '; echo"; send 'K enter'
send "T echo; echo 'sem teclado: a regiao vai ate a linha 28'"; send 'K enter'
sleep 6
send 'EXIT'
wait $TP
[ -n "$UI" ] && kill -CONT "$UI"
echo "done"
