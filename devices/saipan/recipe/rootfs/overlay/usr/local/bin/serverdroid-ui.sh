#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# On-screen interface for the phone's own display (console /dev/tty1) and its buttons: a status page that is also the home screen,
# and a menu driven by the volume keys and the assistant button, like a TV menu.
#
#   Volume up / down   move the selection (hold to repeat)
#   Assistant button   open the menu from the status page; select the highlighted item
#   Power              go back / close; on the status page it wakes the screen, and puts it in standby when it is already on
#
#   status page   white page with the state of the server; the panel sleeps after ON_SECS without a key (standby), any key wakes it.
#   ATTENTION     the same page with a red banner and the backlight flashing until a button is pressed (acknowledged);
#                 then the screen stays on, steady, until the cause is gone.
#
# It reads state (sysfs, /proc, ps) and writes the console and the backlight. The buttons come from /usr/local/bin/keys (tokens UP, DOWN,
# OK, POWER on a FIFO). The menu actions that change anything (Wi-Fi reconnect, brightness, charge mode, the terminal) are listed in menu_run().

TTY=/dev/tty1
exec > "$TTY" 2> /dev/null < /dev/null
# The kernel turns the volume and power buttons into keyboard input for this console, and the console would echo it onto the screen
# ("^[[26~" at the cursor, scrambling the page): nobody reads that input, so switch the echo off (and keep the tty open, which keeps the setting).
stty -F "$TTY" -echo -icanon 2> /dev/null
echo 1 > /proc/sys/kernel/printk 2> /dev/null     # keep kernel messages off the page

HOT_MC=80000          # millidegrees C: at or above this the page says ATTENTION
ON_SECS=20            # seconds the screen stays on after boot or a key, status page
MENU_SECS=60          # the menu closes itself after this many idle seconds
BL_ON=60              # panel brightness 0-255 while on
BL_FLASH=255          # brightness of the flash's bright phase
BL_ACK=120            # brightness while an acknowledged ATTENTION stays on
FIFO=/run/keys.fifo
WAKE=/run/serverdroid-wake
ATTN=/run/serverdroid-attn     # exists while an ATTENTION cause is present
ACK=/run/serverdroid-ack       # a button was pressed during this ATTENTION
BLFILE=/etc/serverdroid-brightness   # saved brightness for the status page (one number)
PAGE_BASE=$(printf '\033[0;30;47m')  # black text on white (an LCD: no burn-in concern)
ESC=$(printf '\033')
# Brand colours: the console palette has green and yellow slots (normal and bright); put the logo's green and orange in them.
palette() { printf '%s]P2%s%s]PA%s%s]P3%s%s]PB%s' "$ESC" 5fc24c "$ESC" 5fc24c "$ESC" e8821a "$ESC" e8821a; }
FG_G="${ESC}[1;32m"; FG_O="${ESC}[1;33m"
GRN="${ESC}[1;30;42m"; RED="${ESC}[1;37;41m"; YEL="${ESC}[1;30;43m"; REV="${ESC}[0;37;40m"
BLDEV=$(ls -d /sys/class/backlight/* 2> /dev/null | head -n 1)   # none on kernels without a backlight driver
LASTBL=""; LASTPWR=""; MODE=""; SCREEN=home
[ -r "$BLFILE" ] && BL_ON=$(cat "$BLFILE")
set -- $(stty -F "$TTY" size 2> /dev/null); ROWS=${1:-50}; COLS=${2:-45}
# The panel crops about 32 px at the top and the left edge, and the front camera cut-out reaches about 64 px down in the middle: keep text out
# of them (8x16 font: 4 rows, 4 columns; 16x32 font: 2 rows, 2 columns).
if [ "$ROWS" -ge 80 ]; then TM=4; LM=4; else TM=2; LM=2; fi
IND=$(printf '%*s' "$LM" '')
TEXTW=$((COLS - LM - 1))

set_backlight() { [ -n "$BLDEV" ] && [ "$1" != "$LASTBL" ] && echo "$1" > "$BLDEV/brightness" 2> /dev/null && LASTBL=$1; }
set_power() { [ -n "$BLDEV" ] && [ "$1" != "$LASTPWR" ] && echo "$1" > "$BLDEV/bl_power" 2> /dev/null && LASTPWR=$1; }
uptime_s() { cut -d. -f1 /proc/uptime; }
fit() { cut -c1-"$TEXTW" | sed "s/^/$IND/"; }
cls() { palette; printf '%s%s[2J%s[H' "$PAGE_BASE" "$ESC" "$ESC"; }
at() { printf '%s[%s;1H' "$ESC" "$1"; }
atrc() { printf '%s[%s;%sH' "$ESC" "$1" "$2"; }     # row, column
# reverse-video line, text after the margin. The text is cut to TEXTW characters: a line that reaches the last column of the last row makes
# the console scroll by one row, which shifts everything drawn before it (the menu's partial redraws then land one row off).
bar() { printf '%s%s%s%s%s' "$REV" "$IND" "$(printf '%s' "$1" | cut -c1-"$TEXTW")" "${ESC}[K" "$PAGE_BASE"; }
top() { at 1; i=1; while [ "$i" -le "$TM" ]; do printf '%s[K\n' "$ESC"; i=$((i + 1)); done; }

# ---- buttons -------------------------------------------------------------------------------------------------------------------
[ -p "$FIFO" ] || mkfifo "$FIFO"
exec 3<> "$FIFO"
if [ -x /usr/local/bin/keys ]; then
    /usr/local/bin/keys /dev/input/event* >&3 2> /dev/null &
    KEYSPID=$!
    # the button reader must not outlive this script: an orphan next to the new one's reader makes every press arrive twice (the menu skips items)
    trap 'kill $KEYSPID 2> /dev/null' EXIT
    trap 'exit 0' TERM INT HUP
fi
KEY=""
getkey() {      # waits up to $1 seconds for a button token; sets KEY ("" on timeout)
    KEY=""
    read -t "$1" KEY <&3 || KEY=""
}
drain() { while read -t 0.05 junk <&3; do :; done 2> /dev/null; }

# Touch: the touch service (ctsd) writes UP and DOWN for a drag (one per console row) and TAP x y / LONG x y with raw panel coordinates
# (0..719, 0..1599). The panel hides the first 32 pixels of the console at the top and the left (TM, LM above), so a point on the glass
# is the console pixel (x + OFFX, y + OFFY); /etc/serverdroid-touch-cal can override the two offsets.
CELLW=$((720 / COLS)); CELLH=$((1600 / ROWS)); OFFX=32; OFFY=32
[ -r /etc/serverdroid-touch-cal ] && . /etc/serverdroid-touch-cal
tap_pos() {   # KEY = "TAP x y": sets TCOL and TROW, the 1-based console cell that was touched
    set -- $KEY; TCOL=$(( ($2 + OFFX) / CELLW + 1 )); TROW=$(( ($3 + OFFY) / CELLH + 1 ))
}
have_touch() { [ -e /run/touch-present ]; }

# ---- state -----------------------------------------------------------------------------------------------------------------------
collect() {   # fills the variables used by the pages; REASON lists what needs attention
    REASON=""; MAXT=0; ZT=""
    for z in /sys/class/thermal/thermal_zone*; do
        [ -r "$z/temp" ] || continue
        t=$(cat "$z/temp"); n=$(cat "$z/type" | sed 's/-thermal//; s/cpu-little/cpu/; s/cpu-big/big/; s/soc/soc/')
        ZT="$ZT $n $((t / 1000))"
        [ "$t" -gt "$MAXT" ] && MAXT=$t
    done
    if netstat -ltn 2> /dev/null | grep -q ':22 '; then SSH="listening"; else SSH="DOWN"; REASON="$REASON SSH-DOWN"; fi
    SESS=$(netstat -tn 2> /dev/null | grep ':22 ' | grep -c ESTABLISHED)
    if grep -q ' / ext4 rw' /proc/mounts; then ROOT="rw"; else ROOT="NOT-RW"; REASON="$REASON ROOT-NOT-RW"; fi
    [ "$MAXT" -ge "$HOT_MC" ] && REASON="$REASON HOT"
    KERR=$(dmesg 2> /dev/null | grep -cE 'EXT4-fs.* error|I/O error|Oops:|BUG:|rcu.*stall|Call trace')
    [ "$KERR" -gt 0 ] && REASON="$REASON KERNEL-ERRORS($KERR)"
    if [ -x /usr/local/bin/serverdroid-battery ]; then
        eval "$(/usr/local/bin/serverdroid-battery 2> /dev/null | sed 's/^/B_/')"
        BATL="${B_percent}% ${B_state}, ${B_current_ma} mA, ${B_temp_c} C"
        [ "${B_eta_min:--1}" -gt 0 ] 2> /dev/null && BATL="$BATL, ~$((B_eta_min / 60))h$(printf %02d $((B_eta_min % 60)))m"
        [ "$B_state" = discharging ] && [ "${B_percent:-100}" -le 15 ] && REASON="$REASON BATTERY-LOW(${B_percent}%)"
    else BATL="no battery data"; fi
    F0=$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq 2> /dev/null)
    F6=$(cat /sys/devices/system/cpu/cpufreq/policy6/scaling_cur_freq 2> /dev/null)
    [ -n "$F0" ] && FQ="$((F0 / 1000)) / $((F6 / 1000)) MHz" || FQ="no cpufreq"
    MEM=$(awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{printf "%d of %d MB free", a/1024, t/1024}' /proc/meminfo)
    DISK=$(df -h / | awk 'NR==2{print $3 " of " $2}')
    UP=$(awk '{d=int($1/86400); h=int(($1%86400)/3600); m=int(($1%3600)/60); printf "%dd %dh %dm", d, h, m}' /proc/uptime)
    NETL=$(netlines)
    NOW=$(date '+%Y-%m-%d %H:%M %Z')
    case "$NOW" in 19*) NOW="clock not set";; esac
}

netlines() {  # one line per interface with an address, plus the Wi-Fi link
    for i in wlan0 usb0 ncm0; do
        a=$(ip -4 -o addr show "$i" 2> /dev/null | awk '{print $4}' | head -n 1)
        [ -n "$a" ] && echo "$i $a"
    done
    if [ -d /sys/class/net/wlan0 ] && wpa_cli -i wlan0 status 2> /dev/null | grep -q 'wpa_state=COMPLETED'; then
        sig=$(iw dev wlan0 link 2> /dev/null | awk '/signal/ {print $2" "$3}')
        fr=$(wpa_cli -i wlan0 status 2> /dev/null | sed -n 's/^freq=//p')
        echo "wifi up, $fr MHz, $sig"
    elif [ -d /sys/class/net/wlan0 ]; then
        echo "wifi not connected"
    fi
}

# The phone drawn in text, like the boot picture: $1 = base colours. 15 columns, 9 rows, centred.
phone_art() {
    B=$1; P=$(printf '%*s' $(((COLS - 15) / 2)) '')
    printf '%s%s.-------------.%s\n' "$B" "$P" "${ESC}[K"
    printf '%s%s|  %so%s  ===  %so%s  |%s\n' "$B" "$P" "$FG_G" "$B" "$FG_G" "$B" "${ESC}[K"
    printf '%s%s| %s.---------.%s |%s\n' "$B" "$P" "$FG_G" "$B" "${ESC}[K"
    printf '%s%s| %s|%s  %s/\\/\\%s   %s|%s |%s\n' "$B" "$P" "$FG_G" "$B" "$FG_G" "$B" "$FG_G" "$B" "${ESC}[K"
    printf '%s%s| %s|%s %s/    \\%s  %s|%s |%s\n' "$B" "$P" "$FG_G" "$B" "$FG_G" "$B" "$FG_G" "$B" "${ESC}[K"
    printf '%s%s| %s'"'"'---------'"'"'%s |%s\n' "$B" "$P" "$FG_G" "$B" "${ESC}[K"
    printf '%s%s|    %s(%s %sM%s %s)%s    |%s\n' "$B" "$P" "$FG_G" "$B" "$FG_O" "$B" "$FG_G" "$B" "${ESC}[K"
    printf '%s%s| (_) (_) (_) |%s\n' "$B" "$P" "${ESC}[K"
    printf '%s%s'"'"'-------------'"'"'%s\n' "$B" "$P" "${ESC}[K"
}

row() {   # label (orange, in brackets), value
    printf '%s %s%-8s%s %s%s\n' "$IND" "$FG_O" "[$1]" "$BASE" "$2" "${ESC}[K"
}

page() {      # the status page; $1 = base colours
    BASE=$1
    printf '%s%s[?25l' "$BASE" "$ESC"; top
    printf '%*s%s[ SERVER%sDROID%s ]%s%s\n' $(((COLS - 15) / 2)) '' "$FG_G" "$FG_O" "$FG_G" "$BASE" "${ESC}[K"
    printf '%s %s - Linux %s%s\n\n' "$IND" "$(hostname)" "$(uname -r)" "${ESC}[K"
    phone_art "$BASE"
    printf '%s\n' "${ESC}[K"
    if [ -z "$REASON" ]; then
        printf '%s%s STATUS: OK - no action needed %s%s\n\n' "$GRN" "$IND" "$BASE" "${ESC}[K"
    else
        printf '%s%s ATTENTION:%s %s%s\n\n' "$RED" "$IND" "$BASE$YEL" "$REASON $BASE" "${ESC}[K"
    fi
    row TIME "$NOW"
    row UP "$UP, load $(cut -d' ' -f1 /proc/loadavg)"
    printf '%s\n' "$NETL" | while IFS= read -r l; do row NET "$l"; done
    row SSH "$SSH, $SESS session(s)"
    row BATT "$BATL"
    row CPU "$FQ"
    row TEMP "${ZT# } C"
    row MEM "$MEM"
    row ROOT "$ROOT, $DISK"
    row KERNEL "$KERR error line(s)"
    printf '%s\n' "${ESC}[K"
    printf '%s %slast kernel messages:%s%s\n' "$IND" "$FG_O" "$BASE" "${ESC}[K"
    # the Wi-Fi driver's own chatter is left out here (the Kernel log screen shows everything); long lines wrap instead of being cut
    KM=$(/usr/local/bin/klog 2> /dev/null | grep -v -E '\[wlan\]|asicFill|callbacks suppressed' | tail -n 5 | fold -s -w $((TEXTW - 2)) | tail -n 8)
    [ -n "$KM" ] || KM="(only Wi-Fi driver output; see Kernel log)"
    printf '%s\n' "$KM" | while IFS= read -r l; do printf '%s %s%s\n' "$IND" "$l" "${ESC}[K"; done
    printf '%s[J' "$ESC"
    at "$ROWS"; bar " tap or ASSISTANT: menu"
}

# ---- menu ----------------------------------------------------------------------------------------------------------------------
# No Reboot and no Power off here: a warm reboot leaves the panel in a broken state and a software power off does not come back by itself
# (docs/touch-input-investigation.md, "Reboot and power off"); use the Power key, or ssh.
ITEMS="Status|Network|System|Battery|Charge mode|Wi-Fi networks|Run command|Terminal (preview)|Kernel log|Brightness|Wi-Fi reconnect|Touch test"
NITEMS=12
item_name() { echo "$ITEMS" | cut -d'|' -f"$1"; }

menu_row() { echo $((TM + 3 + 2 * ($1 - 1))); }     # screen row of item $1

menu_item() {  # $1 = index, $2 = selected index: draws that one row
    at "$(menu_row "$1")"
    if [ "$1" -eq "$2" ]; then bar " > $(item_name "$1")"; else printf '%s    %s%s' "$IND" "$(item_name "$1")" "${ESC}[K"; fi
}

menu_draw() {  # $1 = selected index: the whole screen
    cls; printf '%s[?25l' "$ESC"
    at $((TM + 1)); bar " SERVERDROID MENU"
    i=1; while [ "$i" -le "$NITEMS" ]; do menu_item "$i" "$1"; i=$((i + 1)); done
    at "$ROWS"; bar " drag/VOL: move  tap: select  tap here: close"
}

show_text() {  # $1 = title, text on stdin; scroll with UP/DOWN, OK or POWER returns
    T=$(cat); TOTAL=$(printf '%s\n' "$T" | wc -l); TOP=1; PER=$((ROWS - TM - 4)); IDLE=0; DRAW=1
    while :; do
        if [ "$DRAW" -eq 1 ]; then
            cls; printf '%s[?25l' "$ESC"
            at $((TM + 1)); bar " $1"
            at $((TM + 3)); printf '%s\n' "$T" | sed -n "${TOP},$((TOP + PER - 1))p" | fit
            at "$ROWS"; bar " drag/VOL: scroll  tap here: back"
            DRAW=0
        fi
        getkey 1
        case "$KEY" in
            TAP*) tap_pos; [ "$TROW" -ge "$ROWS" ] && return ;;
            UP) [ "$TOP" -gt 1 ] && { TOP=$((TOP - 1)); DRAW=1; } ;;
            DOWN) [ $((TOP + PER - 1)) -lt "$TOTAL" ] && { TOP=$((TOP + 1)); DRAW=1; } ;;
            OK|POWER) return ;;
            "") IDLE=$((IDLE + 1)); [ "$IDLE" -gt "$MENU_SECS" ] && return ;;
        esac
        [ -n "$KEY" ] && IDLE=0
    done
}

confirm() {   # $1 = question; OK = yes, POWER (or timeout) = no
    cls; printf '%s[?25l' "$ESC"
    at $((TM + 1)); bar " $1"
    at $((TM + 3)); printf '%s ASSISTANT or tap here = yes\n\n\n\n%s POWER or tap here = no\n' "$IND" "$IND"
    n=0
    while [ "$n" -lt 20 ]; do
        getkey 1
        case "$KEY" in
            OK) return 0 ;; POWER) return 1 ;;
            TAP*) tap_pos; [ "$TROW" -le $((TM + 5)) ] && return 0; [ "$TROW" -ge $((TM + 7)) ] && return 1 ;;
        esac
        n=$((n + 1))
    done
    return 1
}

brightness_menu() {
    cls; printf '%s[?25l' "$ESC"
    LV="20 40 60 90 120 180 255"; cur=$BL_ON; shown=""
    while :; do
        if [ "$shown" != "$cur" ]; then
            at $((TM + 1)); bar " Brightness"
            at $((TM + 3)); printf '%s level %s of 255%s\n\n%s VOL: change\n%s ASSIST: save\n%s PWR: cancel%s' "$IND" "$cur" "${ESC}[K" "$IND" "$IND" "$IND" "${ESC}[K"
            set_backlight "$cur"; shown=$cur
        fi
        getkey 30
        case "$KEY" in
            UP) for l in $LV; do [ "$l" -gt "$cur" ] && { cur=$l; break; }; done ;;
            DOWN) prev=$cur; for l in $LV; do [ "$l" -lt "$cur" ] && prev=$l; done; cur=$prev ;;
            OK) BL_ON=$cur; echo "$cur" > "$BLFILE"; return ;;
            TAP*) tap_pos
                if [ "$TROW" -ge "$ROWS" ]; then set_backlight "$BL_ON"; return; fi
                BL_ON=$cur; echo "$cur" > "$BLFILE"; return ;;
            POWER|"") set_backlight "$BL_ON"; return ;;
        esac
    done
}

choose() {   # $1 = title, the items on stdin (one per line); sets CH to the chosen number, 0 if cancelled
    T=$(cat); TOTAL=$(printf '%s\n' "$T" | wc -l); SEL=1; TOP=1; PER=$((ROWS - TM - 5)); CH=0; IDLE=0; DRAW=1; FIRST=1
    [ "$TOTAL" -gt 0 ] || return
    while :; do
        if [ "$DRAW" -eq 1 ]; then
            [ "$FIRST" -eq 1 ] && { cls; printf '%s[?25l' "$ESC"; at $((TM + 1)); bar " $1"; at "$ROWS"; bar " drag/VOL: move  tap: choose  tap here: back"; FIRST=0; }
            r=$((TM + 3)); i=$TOP
            while [ "$i" -lt $((TOP + PER)) ] && [ "$i" -le "$TOTAL" ]; do
                at $r; l=$(printf '%s\n' "$T" | sed -n "${i}p" | cut -c1-$((TEXTW - 4)))
                if [ "$i" -eq "$SEL" ]; then bar " > $l"; else printf '%s   %s%s' "$IND" "$l" "${ESC}[K"; fi
                r=$((r + 1)); i=$((i + 1))
            done
            DRAW=0
        fi
        getkey 1
        case "$KEY" in
            UP) [ "$SEL" -gt 1 ] && { SEL=$((SEL - 1)); [ "$SEL" -lt "$TOP" ] && TOP=$SEL; DRAW=1; } ;;
            DOWN) [ "$SEL" -lt "$TOTAL" ] && { SEL=$((SEL + 1)); [ "$SEL" -ge $((TOP + PER)) ] && TOP=$((SEL - PER + 1)); DRAW=1; } ;;
            OK) CH=$SEL; return ;;
            POWER) CH=0; return ;;
            TAP*)
                tap_pos
                if [ "$TROW" -ge "$ROWS" ]; then CH=0; return; fi
                i=$((TOP + TROW - (TM + 3)))
                if [ "$TROW" -ge $((TM + 3)) ] && [ "$i" -ge 1 ] && [ "$i" -le "$TOTAL" ]; then CH=$i; return; fi ;;
            "") IDLE=$((IDLE + 1)); [ "$IDLE" -gt "$MENU_SECS" ] && { CH=0; return; } ;;
        esac
        [ -n "$KEY" ] && IDLE=0
    done
}

# Text entry with three buttons: VOL scrolls through the characters (hold to repeat), ASSISTANT adds the shown one, the last two entries
# of the ring delete the last character and finish. Sets TXT and TXTOK (0 when cancelled with POWER).
CHARSET='abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ !"#$%&'"'"'()*+,-./:;<=>?@[\]^_`{|}~'
ring_entry() {   # $1 = what is asked
    TXT=""; TXTOK=0; N=${#CHARSET}; IDX=1; IDLE=0; cls; printf '%s[?25l' "$ESC"
    at $((TM + 1)); bar " $1"
    at $((TM + 3)); printf '%s VOL: choose a character%s' "$IND" "${ESC}[K"
    at $((TM + 4)); printf '%s ASSIST: add it%s' "$IND" "${ESC}[K"
    at $((TM + 5)); printf '%s ring ends: [DEL] [DONE]%s' "$IND" "${ESC}[K"
    at "$ROWS"; bar " PWR: cancel"
    while :; do
        if [ "$IDX" -le "$N" ]; then cc=$(printf '%s' "$CHARSET" | cut -c"$IDX"); [ "$cc" = " " ] && cc="space"
        elif [ "$IDX" -eq $((N + 1)) ]; then cc="[DEL]"; else cc="[DONE]"; fi
        at $((TM + 8)); printf '%s text: %s%s' "$IND" "$TXT" "${ESC}[K"
        at $((TM + 10)); printf '%s now:  < %s >%s' "$IND" "$cc" "${ESC}[K"
        getkey 30
        case "$KEY" in
            UP) IDX=$((IDX + 1)); [ "$IDX" -gt $((N + 2)) ] && IDX=1 ;;
            DOWN) IDX=$((IDX - 1)); [ "$IDX" -lt 1 ] && IDX=$((N + 2)) ;;
            OK)
                if [ "$IDX" -le "$N" ]; then [ "${#TXT}" -lt 63 ] && { [ "$cc" = space ] && cc=" "; TXT="$TXT$cc"; }
                elif [ "$IDX" -eq $((N + 1)) ]; then TXT=${TXT%?}
                else TXTOK=1; return; fi ;;
            POWER|"") return ;;
        esac
    done
}

# Text entry on the touch screen is a program of its own, serverdroid-input (term/input_app.c): a prompt, a text field and the keyboard of the
# terminal (QWERTY with gaps, accents on long press, symbol pages). It draws on the framebuffer itself and writes the text to standard output;
# the exit status says whether it was confirmed. PW=1 masks the text (tapping the field shows it).
kbd_entry() {   # $1 = what is asked, $2 = initial text; sets TXT and TXTOK (1 after enter, 0 when cancelled)
    TXT=$(/usr/local/bin/serverdroid-input --prompt "$1" --initial "$2" ${PW:+--password}); [ $? -eq 0 ] && TXTOK=1 || { TXTOK=0; TXT=""; }
    rm -f /run/touch-raw                      # the program removes it itself; this covers a crash
    drain
}

text_entry() {   # $1 = what is asked, $2 = initial text; the keyboard when the touch screen is there, else the ring of characters
    if have_touch && [ -x /usr/local/bin/serverdroid-input ]; then kbd_entry "$1" "$2"; else ring_entry "$1"; fi
}

# A command runner, not an interactive terminal: type a command with the keyboard, it runs (as root, 20 s at most, no input) and its output is
# shown in the scrolling text view; programs that need a terminal (vi, top) do not work. Anyone who can touch the screen can run commands.
terminal() {
    last=""
    while :; do
        text_entry "Command (empty or cancel: leave)" "$last"
        [ "$TXTOK" -eq 1 ] && [ -n "$TXT" ] || return
        last=$TXT
        cls; at $((TM + 1)); bar " running..."
        { printf '$ %s\n\n' "$TXT"; timeout 20 sh -c "$TXT" < /dev/null 2>&1 | head -c 30000; } | fold -s -w $((TEXTW - 2)) | show_text "Terminal"
    done
}

# Where a tap lands, to check the offsets OFFX and OFFY: it prints the raw coordinates and the console cell and draws a mark there.
touch_test() {
    cls; printf '%s[?25l' "$ESC"; at $((TM + 1)); bar " Touch test"
    at $((TM + 3)); printf '%s tap anywhere; the mark should%s\n%s appear under your finger%s' "$IND" "${ESC}[K" "$IND" "${ESC}[K"
    at "$ROWS"; bar " tap here: leave"; IDLE=0
    while :; do
        getkey 1
        case "$KEY" in
            TAP*|LONG*)
                set -- $KEY; rx=$2; ry=$3; tap_pos
                [ "$TROW" -ge "$ROWS" ] && return
                at $((TM + 6)); printf '%s x %s y %s -> column %s row %s%s' "$IND" "$rx" "$ry" "$TCOL" "$TROW" "${ESC}[K"
                atrc "$TROW" "$TCOL"; printf '%s+%s' "$REV" "$PAGE_BASE" ;;
            POWER) return ;;
            "") IDLE=$((IDLE + 1)); [ "$IDLE" -gt 120 ] && return ;;
        esac
        [ -n "$KEY" ] && IDLE=0
    done
}

# The terminal proper is a separate program (term/): it draws in a region of its own, reads the same tokens from the FIFO while it runs (this
# script waits), and ends when POWER is pressed or the shell is left. The menu is redrawn when it returns.
term_preview() {
    if [ ! -x /usr/local/bin/serverdroid-term ]; then
        echo "serverdroid-term is not installed." | show_text "Terminal"
        return
    fi
    cat /dev/zero > /dev/fb0 2> /dev/null   # a black screen behind it (the console clear leaves the edge columns alone); the terminal only draws inside its own region
    /usr/local/bin/serverdroid-term
    rm -f /run/touch-raw                    # the terminal removes it itself; this covers a crash (the touch service would keep sending raw events)
    drain
}

WPAC="wpa_cli -i wlan0"
wifi_state() { $WPAC status 2> /dev/null | sed -n "s/^$1=//p"; }

wifi_wait() {   # $1 = network id: wait up to 30 s for that network to be associated; 0 when it is
    n=0
    while [ "$n" -lt 15 ]; do
        [ "$(wifi_state wpa_state)" = COMPLETED ] && [ "$(wifi_state id)" = "$1" ] && return 0
        sleep 2; n=$((n + 1))
    done
    return 1
}

wifi_dhcp() {   # fresh lease after a network change
    [ -r /run/udhcpc.wlan0.pid ] && kill "$(cat /run/udhcpc.wlan0.pid)" 2> /dev/null
    sleep 1
    udhcpc -i wlan0 -b -R -x hostname:serverdroid -p /run/udhcpc.wlan0.pid > /dev/null 2>&1
    sleep 4
    a=$(ip -4 -o addr show wlan0 | awk '{print $4}' | cut -d/ -f1 | head -n 1); [ -n "$a" ] && arping -U -c 3 -I wlan0 "$a" > /dev/null 2>&1   # mesh nodes cache addresses
}

wifi_networks() {
    cls; at $((TM + 1)); bar " Wi-Fi networks"; at $((TM + 3)); printf '%s scanning...%s' "$IND" "${ESC}[K"
    $WPAC scan > /dev/null 2>&1; sleep 6
    LIST=$($WPAC scan_results 2> /dev/null | awk -F'\t' 'NR > 1 && $5 != "" { s = $3 + 0; k = ($4 ~ /WPA|RSN|WEP/) ? "*" : " "
        if (!($5 in best) || s > best[$5]) { best[$5] = s; sec[$5] = k } } END { for (n in best) printf "%d\t%s\t%s\n", best[n], sec[n], n }' | sort -rn)
    [ -n "$LIST" ] || { echo "No networks found." | show_text "Wi-Fi networks"; return; }
    printf '%s\n' "$LIST" | awk -F'\t' '{ printf "%s %s (%d)\n", $2, $3, $1 }' > /run/wifi.list
    choose "Wi-Fi networks (* = password)" < /run/wifi.list; rm -f /run/wifi.list
    [ "$CH" -gt 0 ] || return
    row=$(printf '%s\n' "$LIST" | sed -n "${CH}p"); sec=$(printf '%s' "$row" | cut -f2); ssid=$(printf '%s' "$row" | cut -f3-)
    confirm "Connect to $ssid?" || return
    grep -q '^update_config=1' /etc/wpa_supplicant.conf || sed -i '1i update_config=1' /etc/wpa_supplicant.conf
    old=$(wifi_state id)
    id=$($WPAC list_networks 2> /dev/null | awk -F'\t' -v s="$ssid" '$2 == s { print $1; exit }'); new=0
    if [ -z "$id" ]; then
        if [ "$sec" = "*" ]; then
            PW=1 text_entry "Password for $ssid"; [ "$TXTOK" -eq 1 ] && [ -n "$TXT" ] || return
            psk=$(wpa_passphrase "$ssid" "$TXT" 2> /dev/null | sed -n 's/^[[:space:]]*psk=//p'); TXT=""
            [ -n "$psk" ] || { echo "Could not use that password." | show_text "Wi-Fi"; return; }
        fi
        id=$($WPAC add_network 2> /dev/null | tail -n 1); new=1
        $WPAC set_network "$id" ssid "\"$ssid\"" > /dev/null
        if [ "$sec" = "*" ]; then $WPAC set_network "$id" psk "$psk" > /dev/null; else $WPAC set_network "$id" key_mgmt NONE > /dev/null; fi
        psk=""
    fi
    cls; at $((TM + 1)); bar " Wi-Fi"; at $((TM + 3)); printf '%s connecting to %s...%s' "$IND" "$ssid" "${ESC}[K"
    $WPAC enable_network "$id" > /dev/null; $WPAC select_network "$id" > /dev/null
    if wifi_wait "$id"; then
        $WPAC set_network "$id" priority 10 > /dev/null
        [ -n "$old" ] && [ "$old" != "$id" ] && $WPAC set_network "$old" priority 0 > /dev/null
        $WPAC save_config > /dev/null; chmod 600 /etc/wpa_supplicant.conf
        wifi_dhcp
        { echo "Connected to $ssid."; echo; ip -4 -o addr show wlan0 | awk '{print $4}'; echo; echo "The SSH address may have changed."; } | show_text "Wi-Fi"
    else
        [ "$new" -eq 1 ] && $WPAC remove_network "$id" > /dev/null
        $WPAC enable_network all > /dev/null
        [ -n "$old" ] && $WPAC select_network "$old" > /dev/null
        { echo "Could not connect to $ssid."; echo "(wrong password or out of range)"; echo; echo "The previous network was restored."; } | show_text "Wi-Fi"
    fi
}

menu_run() {  # $1 = index of the chosen item
    case "$(item_name "$1")" in
        Status) SCREEN=home ;;
        Network)
            collect
            { netlines; echo; ip -4 route 2> /dev/null; echo; wpa_cli -i wlan0 status 2> /dev/null | grep -E '^(wpa_state|freq|key_mgmt|pairwise_cipher)'; } | show_text "Network" ;;
        System)
            collect
            { echo "Up      $UP"; echo "Load    $(cut -d' ' -f1-3 /proc/loadavg)"; echo "CPU     $FQ"; echo "Temp   $ZT"; echo "Mem     $MEM"; echo "Root    $DISK"
              echo "Kernel  $(uname -r)"; echo "Errors  $KERR"; echo; echo "Services:"; ps | grep -E 'dropbear|wpa_supplicant|udhcpc' | grep -v grep | awk '{print " " $5}' | sort -u; } | show_text "System" ;;
        Battery)
            collect
            { echo "Level    ${B_percent}% (estimate)"; echo "State    $B_state"; echo "Voltage  ${B_voltage_mv} mV"; echo "Current  ${B_current_ma} mA"
              echo "Temp     ${B_temp_c} C"; echo "Mode     $B_charge_mode"
              [ "${B_eta_min:--1}" -gt 0 ] 2> /dev/null && echo "Time     about $((B_eta_min / 60))h $((B_eta_min % 60))m"
              echo; echo "The level is estimated from the voltage; the"; echo "time assumes a steady current."; } | show_text "Battery" ;;
        "Charge mode")
            cur=$(cat /etc/serverdroid-charge-mode 2> /dev/null); [ -n "$cur" ] || cur=limit
            case "$cur" in limit) new=full ;; full) new=hold ;; hold) new=battery ;; *) new=limit ;; esac
            echo "$new" > /etc/serverdroid-charge-mode; sync
            { echo "Was:  $cur"; echo "Now:  $new"; echo
              echo "limit    keep 70-80% on the cable"; echo "full     charge to 100%"; echo "hold     run from the cable,"; echo "         battery rests"
              echo "battery  run from the battery"; echo; echo "Applied within 10 seconds."; } | show_text "Charge mode" ;;
        "Wi-Fi networks") wifi_networks ;;
        "Run command") terminal ;;
        "Terminal (preview)") term_preview ;;
        "Touch test") touch_test ;;
        "Kernel log") /usr/local/bin/klog 2> /dev/null | tail -n 200 | fold -s -w $((TEXTW - 2)) | show_text "Kernel log" ;;
        Brightness) brightness_menu ;;
        "Wi-Fi reconnect")
            ( /opt/serverdroid-conn/wifi-up.sh 2>&1 | tail -n 3 ) | show_text "Wi-Fi reconnect" ;;
    esac
}

menu() {
    SEL=1; IDLE=0
    drain
    menu_draw "$SEL"
    while :; do
        getkey 1
        OLD=$SEL
        case "$KEY" in
            TAP*)
                tap_pos
                if [ "$TROW" -ge "$ROWS" ]; then KEY=POWER
                elif [ "$TROW" -ge $((TM + 3)) ] && [ $(((TROW - TM - 3) / 2 + 1)) -le "$NITEMS" ]; then SEL=$(((TROW - TM - 3) / 2 + 1)); KEY=OK
                else KEY=""; fi ;;
        esac
        case "$KEY" in
            UP) SEL=$((SEL - 1)); [ "$SEL" -lt 1 ] && SEL=$NITEMS ;;
            DOWN) SEL=$((SEL + 1)); [ "$SEL" -gt "$NITEMS" ] && SEL=1 ;;
            OK) menu_run "$SEL"; [ "$SCREEN" = home ] && return; drain; menu_draw "$SEL" ;;
            POWER) return ;;
        esac
        [ "$SEL" != "$OLD" ] && { menu_item "$OLD" "$SEL"; menu_item "$SEL" "$SEL"; }
        if [ -z "$KEY" ]; then IDLE=$((IDLE + 1)); [ "$IDLE" -gt "$MENU_SECS" ] && return; else IDLE=0; fi
    done
}

# ---- main loop ------------------------------------------------------------------------------------------------------------------
# After a warm reboot the panel has shown a broken picture (the lower half doubled, or black with the backlight on) until it went through
# a sleep and wake once (docs/touch-input-investigation.md, "black screen"): do that once per boot, before drawing, and send the brightness.
kick_panel() {
    [ -n "$BLDEV" ] || return
    [ -e /run/serverdroid-panel-kicked ] && return
    : > /run/serverdroid-panel-kicked
    echo 4 > "$BLDEV/bl_power" 2> /dev/null; sleep 0.5
    echo 0 > "$BLDEV/bl_power" 2> /dev/null; sleep 0.3
    echo "$BL_ON" > "$BLDEV/brightness" 2> /dev/null
    LASTPWR=0; LASTBL=$BL_ON
    echo "serverdroid-ui: panel sleep and wake done at boot" > /dev/kmsg 2> /dev/null
}
kick_panel
# Boot picture: once per boot the first screen is the splash (a raw framebuffer image written straight to /dev/fb0), kept for SPLASH_SECS
# while the first status is collected; it replaces the kernel's eight penguins as the first thing in view.
SPLASH=/usr/share/serverdroid/splash.raw.gz
SPLASH_SECS=5
splash() {
    [ -e /run/serverdroid-splash-done ] && return
    : > /run/serverdroid-splash-done
    [ -r "$SPLASH" ] && [ -w /dev/fb0 ] || return
    printf '%s[?25l' "$ESC"; set_power 0; set_backlight "$BL_ON"
    zcat "$SPLASH" > /dev/fb0 2> /dev/null
    collect
    sleep "$SPLASH_SECS"
}
splash
uptime_s > "$WAKE"
TICK=0
while true; do
    NOWS=$(uptime_s)
    [ $((TICK % 5)) -eq 0 ] && collect
    if [ -n "$REASON" ]; then
        if [ "$MODE" != flash ]; then MODE=flash; cls; fi
        [ $((TICK % 5)) -eq 0 ] && page "$PAGE_BASE"
        set_power 0
        : > "$ATTN"
        if [ -e "$ACK" ]; then set_backlight "$BL_ACK"
        elif [ $((TICK % 2)) -eq 0 ]; then set_backlight "$BL_FLASH"; else set_backlight 0; fi
    else
        rm -f "$ATTN" "$ACK"
        if [ "$MODE" != normal ]; then MODE=normal; cls; fi
        [ $((TICK % 5)) -eq 0 ] && page "$PAGE_BASE"
        LAST=$(cat "$WAKE" 2> /dev/null || echo 0)
        if [ $((NOWS - LAST)) -lt "$ON_SECS" ]; then set_power 0; set_backlight "$BL_ON"
        else set_power 4; fi                                  # standby: the panel sleeps
    fi
    getkey 1
    if [ -n "$KEY" ]; then
        WASOFF=$LASTPWR
        if [ "$KEY" = POWER ] && [ "$WASOFF" != 4 ] && [ -z "$REASON" ]; then   # Power with the screen on and nothing to acknowledge: standby now
            echo 0 > "$WAKE"; set_power 4; TICK=-1; continue
        fi
        uptime_s > "$WAKE"
        [ -e "$ATTN" ] && : > "$ACK"
        set_power 0
        case "$KEY" in TAP*|OK) KEY=OK ;; esac
        if [ "$KEY" = OK ] && [ "$WASOFF" != 4 ]; then       # a key that only wakes the screen does not open the menu
            SCREEN=menu; menu; SCREEN=home; MODE=""; drain; uptime_s > "$WAKE"
        fi
        TICK=-1
    fi
    TICK=$((TICK + 1))
done
