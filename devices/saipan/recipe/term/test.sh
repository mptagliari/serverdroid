#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Headless tests of serverdroid-term with the text renderer: drive it through a FIFO, read the screen it dumps. Run on the phone:
#   cd /root/term && make && sh test.sh
# Each test prints PASS or FAIL with what it saw.
BIN=./serverdroid-term
DUMP=/tmp/term.dump
IN=/tmp/term.in
fail=0

start() {   # $1 = rows, $2 = cols, extra options after
    rows=$1; cols=$2; shift 2
    rm -f $DUMP $IN; mkfifo $IN
    $BIN --dump $DUMP --rows $rows --cols $cols --input $IN --scrollback 500 --once "$@" > /tmp/term.out 2>&1 &
    TPID=$!
    wait_for '# *\|$' 5 || { echo "no prompt at start"; cat $DUMP 2> /dev/null | head -5; }
}
send() { echo "$1" > $IN; }
wait_for() {   # $1 = regex over the dump, $2 = seconds
    n=0; max=$(( $2 * 10 ))
    while [ $n -lt $max ]; do grep -q -E "$1" $DUMP 2> /dev/null && return 0; sleep 0.1; n=$((n + 1)); done
    return 1
}
check() {   # $1 = name, $2 = regex, $3 = seconds
    if wait_for "$2" "${3:-3}"; then echo "PASS  $1"; else echo "FAIL  $1  (wanted: $2)"; fail=$((fail + 1)); grep -v "^|  *|$" $DUMP | head -14; fi
}
stop_term() { send EXIT; n=0; while kill -0 $TPID 2> /dev/null && [ $n -lt 30 ]; do sleep 0.1; n=$((n + 1)); done; kill -0 $TPID 2> /dev/null && { kill -9 $TPID; echo "FAIL  terminal did not exit on EXIT"; fail=$((fail + 1)); }; wait $TPID 2> /dev/null; }

echo "== 1. shell, UTF-8"
start 24 60
send 'T echo hello-term'; send 'K enter'; check "echo" '^\|hello-term'
send 'T echo ação ünï ç'; send 'K enter'; check "UTF-8 text" 'ação ünï ç'
stop_term

echo "== 2. colors and attributes"
start 24 60
send "T printf '\\033[31mRED\\033[0m \\033[1;32mGREENBOLD\\033[0m \\033[44mBLUEBG\\033[0m \\033[4mUND\\033[0m\\n'"; send 'K enter'
check "red foreground" '^@[0-9]+,[0-9]+ fg=[C-E][0-9A-F]0000 '
check "bold" 'attrs=1$'
check "underline" 'attrs=2$'
check "blue background" 'bg=0000[89A-F][0-9A-F]'
stop_term

echo "== 3. many lines and the scrollback"
start 24 60
send 'T seq 1 3000'; send 'K enter'; check "last line 3000 on screen" '^\|3000 *\|'
check "scrollback is full (limit 500)" 'scrollback=500'
send 'SCROLL 100'; check "scrolled back: offset 100" 'offset=100'
n=$(grep -E '^\|[0-9]+ *\|$' $DUMP | head -1 | tr -d '| '); [ -n "$n" ] && [ "$n" -lt 2900 ] && echo "PASS  older lines shown (top row $n)" || { echo "FAIL  older lines not shown (top row '$n')"; fail=$((fail + 1)); }
send 'T x'; check "typing returns to the live screen" 'offset=0'
stop_term

echo "== 4. vi (alternate screen)"
start 24 60
rm -f /tmp/vitest.txt
send 'T vi /tmp/vitest.txt'; send 'K enter'; check "alternate screen on" 'alt=1'
send 'T ihello from vi'; send 'K escape'; sleep 0.6; send 'T :wq'; send 'K enter'
check "back on the main screen" 'alt=0'
sleep 0.3; grep -q "hello from vi" /tmp/vitest.txt 2> /dev/null && echo "PASS  vi saved the file" || { echo "FAIL  vi did not save"; fail=$((fail + 1)); }
stop_term

echo "== 5. top"
start 24 60
send 'T top'; send 'K enter'; check "top shows Mem" 'Mem:' 5
send 'T q'; check "top leaves, the prompt is back" 'alive=1' 5; sleep 0.5; grep -c '# ' $DUMP > /dev/null
stop_term

echo "== 6. less"
start 24 60
send 'T less /etc/profile'; send 'K enter'; check "less shows the file" 'PATH' 3
send 'T q'; sleep 0.5; check "less leaves (prompt visible again)" 'serverdroid:~# *\|$' 3
stop_term

echo "== 7. Ctrl+C"
start 24 60
send 'T sleep 100'; send 'K enter'; sleep 0.5
send 'C c'; send 'T echo status=$?'; send 'K enter'; check "Ctrl+C stopped sleep (status 130)" 'status=130'
stop_term

echo "== 8. resize"
start 24 60
send 'RESIZE 12 40'; sleep 0.3; send 'T stty size'; send 'K enter'; check "TIOCSWINSZ reached the shell (12 40)" '^\|12 40'
check "the dump has 12 rows" 'rows=12 cols=40'
stop_term

echo "== 9. leaving: no orphans"
start 24 60
send 'T sleep 300 &'; send 'K enter'; send 'T sleep 301'; send 'K enter'; sleep 0.5
before=$(ps | grep -c "[s]leep 30[01]")
stop_term; sleep 0.5
after=$(ps | grep -c "[s]leep 30[01]")
[ "$before" -ge 2 ] && [ "$after" -eq 0 ] && echo "PASS  sleeps gone after leaving ($before -> $after)" || { echo "FAIL  orphans: $before -> $after"; fail=$((fail + 1)); ps | grep "[s]leep 30"; }

echo "== 10. the shell exits, then recovery"
rows=24; rm -f $DUMP $IN; mkfifo $IN
$BIN --dump $DUMP --rows 24 --cols 60 --input $IN > /tmp/term.out 2>&1 &
TPID=$!; wait_for '# *\|$' 5
send 'T exit'; send 'K enter'; check "exit message" 'the shell exited with status 0'
send 'OK'; check "OK starts a new shell" 'alive=1'
send 'T echo again-$$'; send 'K enter'; check "the new shell works" 'again-[0-9]+'
stop_term

echo "== 11. the on-screen keyboard (raw touches; screen pixels, no offset)"
KB="--kbd 32 976 672 600 --touch-offset 0"
tap() { set -- $(./kbdtest center "$1"); send "P $1 $2"; sleep 0.05; send "R $1 $2"; sleep 0.12; }
hold() { d=$2; set -- $(./kbdtest center "$1"); send "P $1 $2"; sleep $d; send "R $1 $2"; sleep 0.12; }
start 24 60 $KB
for c in e c h o " " h i; do tap "$c"; done; tap "Enter"
check "keys typed 'echo hi'" '^\|hi *\|'
for c in e c h o " "; do tap "$c"; done; hold a 0.6; tap "Enter"
check "long press a gives a-tilde" '^\|ã *\|'
send 'T echo ã'; tap "◀"; tap x; tap Enter
check "delete removed the whole UTF-8 character (ash line editing)" '^\|x *\|$'
send 'T sleep 100'; send 'K enter'; sleep 0.4; tap CTRL; tap c; send 'T echo status=$?'; send 'K enter'
check "CTRL then c stopped sleep (130)" 'status=130'
send 'T seq 1 300'; send 'K enter'; sleep 0.6
send 'P 300 500'; send 'M 300 540'; send 'M 300 580'; send 'R 300 580'
check "dragging down on the terminal scrolls back two lines" 'offset=2'
send 'P 300 580'; send 'M 300 540'; send 'R 300 540'
check "dragging up scrolls one line forward again" 'offset=1'
send 'T x'; check "typing returns to the live screen" 'offset=0'
set -- $(./kbdtest center q); send "P $1 $2"; send "M $1 $(($2 + 70))"; send "M $1 $(($2 + 140))"; send "R $1 $(($2 + 140))"; sleep 0.3
send 'SCROLL 0'
check "a drag that starts on the keyboard does not scroll the terminal" 'offset=0'
stop_term

echo "== 13. selection and copy (Android style)"
CB=/tmp/clip.txt
SELOPTS="--kbd 32 976 672 600 --touch-offset 0 --clipboard $CB"
statusline() { head -1 $DUMP; }
field() { statusline | tr ' ' '\n' | grep "^$1=" | head -1 | cut -d= -f2; }   # e.g. field copy -> x,y
px() { echo $((32 + $2 * 16 + 8)) $((64 + $1 * 32 + 16)); }   # row, col -> raw x y (region at 32,64; no offset)
longpress() { set -- $(px $1 $2); send "P $1 $2"; sleep 0.8; send "R $1 $2"; sleep 0.2; }
tapxy() { send "P $1 $2"; sleep 0.05; send "R $1 $2"; sleep 0.25; }
rm -f $CB
start 24 60 $SELOPTS
send 'T echo alpha-beta gamma'; send 'K enter'; check "output line" '^\|alpha-beta gamma'
row=$(grep -n '^|alpha-beta gamma' $DUMP | head -1 | cut -d: -f1); row=$((row - 2))   # line 1 is the status
longpress $row 3
check "long press selects the word" "sel=$row,0-$row,9 "
check "the bar offers Copy Paste All" 'copy=.* paste=.* all='
xy=$(field copy); tapxy ${xy%,*} ${xy#*,}
[ "$(cat $CB 2> /dev/null)" = "alpha-beta" ] && echo "PASS  Copy put the word in the clipboard file" || { echo "FAIL  clipboard is '$(cat $CB 2> /dev/null)'"; fail=$((fail + 1)); }
check "the selection is gone after Copy" 'sel=none'
longpress 20 40; check "a long press on a blank shows only Paste" ' paste=[0-9]*,[0-9]* *$'
xy=$(field paste); tapxy ${xy%,*} ${xy#*,}
check "Paste typed the text at the prompt" '^\|serverdroid:~# alpha-beta'
send 'C u'
longpress $row 3
h1=$(field h1); first=$(field h0)
check "the handles are there" 'h0=.* h1='
# drag the end handle to the end of 'gamma' (col 15): aim a handle's width below the text
set -- $(px $row 15); tx=$1; ty=$2
send "P ${h1%,*} ${h1#*,}"; sleep 0.05; send "M $tx $((ty + 28))"; sleep 0.1; send "R $tx $((ty + 28))"; sleep 0.3
check "dragging the end handle extends the selection" "sel=$row,0-$row,15 "
xy=$(field copy); tapxy ${xy%,*} ${xy#*,}
[ "$(cat $CB)" = "alpha-beta gamma" ] && echo "PASS  Copy of the extended selection" || { echo "FAIL  clipboard '$(cat $CB)'"; fail=$((fail + 1)); }
longpress $row 3
xy=$(field all); tapxy ${xy%,*} ${xy#*,}
check "All selects the whole screen" 'sel=0,0-23,59'
xy=$(field copy); tapxy ${xy%,*} ${xy#*,}
grep -q "alpha-beta gamma" $CB && [ "$(wc -l < $CB)" -ge 3 ] && echo "PASS  Copy after All has the lines joined" || { echo "FAIL  clipboard after All: $(head -c 200 $CB)"; fail=$((fail + 1)); }
longpress $row 3; tapxy 100 900
check "a touch elsewhere clears the selection" 'sel=none'
stop_term

echo "== 12. serverdroid-input (the line editor the interface calls)"
run_input() {   # options...; the text is in /tmp/inp.out, the exit status in /tmp/inp.rc
    rm -f $IN /tmp/inp.out /tmp/inp.rc; mkfifo $IN
    ( ./serverdroid-input --no-paint --input $IN --kbd 32 976 672 600 --touch-offset 0 "$@" > /tmp/inp.out; echo $? > /tmp/inp.rc ) &
    IPID=$!; sleep 0.3
}
finish_input() { n=0; while [ ! -s /tmp/inp.rc ] && [ $n -lt 30 ]; do sleep 0.1; n=$((n + 1)); done; wait $IPID 2> /dev/null; }
expect_input() {   # name, expected text, expected status
    if [ "$(cat /tmp/inp.out)" = "$2" ] && [ "$(cat /tmp/inp.rc 2> /dev/null)" = "$3" ]; then echo "PASS  $1"; else echo "FAIL  $1  (text '$(cat /tmp/inp.out)' status '$(cat /tmp/inp.rc 2> /dev/null)', wanted '$2' '$3')"; fail=$((fail + 1)); fi
}
run_input --initial ab --prompt Test; tap c; tap d; tap Enter; finish_input; expect_input "initial text plus keys, Enter confirms" abcd 0
run_input; hold a 0.6; tap Enter; finish_input; expect_input "long press gives a-tilde" ã 0
run_input --initial ação; tap "◀"; tap Enter; finish_input; expect_input "delete removes a whole UTF-8 character" açã 0
run_input --initial secret --password; tap x; tap Enter; finish_input; expect_input "a password is returned as typed, not masked" secretx 0
run_input --initial keep; send POWER; finish_input; expect_input "the power button cancels" "" 1
run_input --initial keep; tap ESC; finish_input; expect_input "ESC cancels" "" 1
run_input --initial keep; send OK; finish_input; expect_input "the assistant button confirms" keep 0
run_input --timeout 1; sleep 2; finish_input; expect_input "no input for the timeout cancels" "" 1

[ $fail -eq 0 ] && echo "ALL PASSED" || echo "$fail FAILED"
exit $fail
