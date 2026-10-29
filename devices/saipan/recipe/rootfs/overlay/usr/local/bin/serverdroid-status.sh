#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Status screen for the phone's own display: one page on the framebuffer console (/dev/tty1), so a
# glance shows whether the server needs attention.
#
#   normal   white page (black text), panel at BL_ON for ON_SECS after boot or after a press of the Power key,
#            then the panel goes to sleep (standby, as the phone did); Power wakes it again.
#   ATTENTION the same white page with the reasons in a red banner, and the backlight flashes (on / off every second) until
#            someone presses Power. That acknowledges it: the flashing stops and the screen stays on, steady, until the cause is
#            gone (then the page returns to normal and standby). A new cause starts again with the flashing.
#
# It only reads state (sysfs, /proc, ps) and writes the console and the backlight level.
# Each backlight write is a DSI command to the panel, so it writes only on a change.
# Usage: serverdroid-status [ONCE]   (with an argument: draw one normal page and exit)

TTY=/dev/tty1
exec > "$TTY" 2> /dev/null < /dev/null
echo 1 > /proc/sys/kernel/printk 2> /dev/null     # keep kernel messages off the page

HOT_MC=80000          # millidegrees C: at or above this the page says ATTENTION
ON_SECS=20            # normal mode: seconds the screen stays on after boot / a Power press
BL_ON=60              # panel brightness 0-255 while on (dim: the page is for a glance)
BL_FLASH=255          # brightness of the flash's bright phase
BL_ACK=120            # brightness while an acknowledged ATTENTION stays on
WAKE=/run/serverdroid-wake
ATTN=/run/serverdroid-attn     # exists while an ATTENTION cause is present
ACK=/run/serverdroid-ack       # Power was pressed during this ATTENTION
PAGE_BASE=$(printf '\033[0;30;47m')   # black text on white, in both modes: the panel is an LCD

ESC=$(printf '\033')
GRN="${ESC}[1;30;42m"; RED="${ESC}[1;37;41m"; YEL="${ESC}[1;30;43m"
BLDEV=$(ls -d /sys/class/backlight/* 2> /dev/null | head -n 1)   # none on kernels without a backlight driver
LASTBL=""; LASTPWR=""; MODE=""

set_backlight() {
    [ -n "$BLDEV" ] && [ "$1" != "$LASTBL" ] && echo "$1" > "$BLDEV/brightness" 2> /dev/null && LASTBL=$1
}

# bl_power 4 = blanked: the driver puts the panel to sleep (its own commands); 0 wakes it.
set_power() {
    [ -n "$BLDEV" ] && [ "$1" != "$LASTPWR" ] && echo "$1" > "$BLDEV/bl_power" 2> /dev/null && LASTPWR=$1
}

uptime_s() { cut -d. -f1 /proc/uptime; }

# Power key: remember the time of the last press in $WAKE. The event device is found by name.
watch_keys() {
    EV=""
    for d in /sys/class/input/input*; do
        [ "$(cat "$d/name" 2> /dev/null)" = "mtk-pmic-keys" ] && EV=/dev/input/$(ls "$d" | grep '^event' | head -n 1)
    done
    [ -c "$EV" ] || return
    while :; do   # one 24-byte input_event per read: type, code, value (EV_KEY 1, KEY_POWER 116, press 1)
        set -- $(dd if="$EV" bs=24 count=1 2> /dev/null | od -An -tu2 -j16 -N6)
        if [ "$1" = 1 ] && [ "$2" = 116 ] && [ "$3" = 1 ]; then
            uptime_s > "$WAKE"
            [ -e "$ATTN" ] && : > "$ACK"
        fi
    done
}

collect() {   # fills the variables used by page(); REASON lists what needs attention
    REASON=""; MAXT=0; ZT=""
    for z in /sys/class/thermal/thermal_zone*; do
        [ -r "$z/temp" ] || continue
        t=$(cat "$z/temp"); n=$(cat "$z/type" | sed 's/-thermal//; s/cpu-//')
        ZT="$ZT $n $((t / 1000)).$(( (t % 1000) / 100 ))C"
        [ "$t" -gt "$MAXT" ] && MAXT=$t
    done
    if netstat -ltn 2> /dev/null | grep -q ':22 '; then SSH="listening"; else SSH="DOWN"; REASON="$REASON SSH-DOWN"; fi
    SESS=$(netstat -tn 2> /dev/null | grep ':22 ' | grep -c ESTABLISHED)
    if grep -q ' / ext4 rw' /proc/mounts; then ROOT="rw"; else ROOT="NOT-RW"; REASON="$REASON ROOT-NOT-RW"; fi
    [ "$MAXT" -ge "$HOT_MC" ] && REASON="$REASON HOT"
    KERR=$(dmesg 2> /dev/null | grep -cE 'EXT4-fs.* error|I/O error|Oops:|BUG:|rcu.*stall|Call trace')
    [ "$KERR" -gt 0 ] && REASON="$REASON KERNEL-ERRORS($KERR)"
    U=$(ls -d /sys/bus/platform/drivers/ufshcd*/*.ufshci/power_info 2> /dev/null | head -n 1)
    if [ -n "$U" ]; then UFS="$(cat $U/mode 2> /dev/null) $(cat $U/gear 2> /dev/null) x$(cat $U/lane 2> /dev/null) link=$(cat $U/link_state 2> /dev/null)"; else UFS="no UFS info"; fi
    IP=""; for i in usb0 ncm0; do a=$(ip -4 -o addr show "$i" 2> /dev/null | awk '{print $4}' | head -n 1); [ -n "$a" ] && IP="$IP $i=$a"; done
    [ -z "$IP" ] && IP=" no address"
    F0=$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq 2> /dev/null)
    F6=$(cat /sys/devices/system/cpu/cpufreq/policy6/scaling_cur_freq 2> /dev/null)
    [ -n "$F0" ] && FQ="little $((F0 / 1000)) MHz  big $((F6 / 1000)) MHz" || FQ="no cpufreq"
    MEM=$(awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{printf "%d of %d MB free", a/1024, t/1024}' /proc/meminfo)
    DISK=$(df -h / | awk 'NR==2{print $3 " used of " $2}')
    UP=$(awk '{d=int($1/86400); h=int(($1%86400)/3600); m=int(($1%3600)/60); printf "%dd %dh %dm", d, h, m}' /proc/uptime)
    NOW=$(date '+%Y-%m-%d %H:%M:%S %Z')   # local time: /etc/localtime is UTC-3 (Brasilia); the clock itself stays UTC
    case "$NOW" in 19*) NOW="clock not set";; esac
}

page() {      # $1 = base colours
    BASE=$1
    printf '%s%s[?25l%s[H\n\n' "$BASE" "$ESC" "$ESC"
    printf '    SERVERDROID  %s%s\n' "$(hostname)" "${ESC}[K"
    printf '    Linux %s%s\n\n' "$(uname -r)" "${ESC}[K"
    if [ -z "$REASON" ]; then
        printf '%s    STATUS: OK - no action needed  %s%s%s\n\n' "$GRN" "$BASE" "${ESC}[K" ""
    else
        printf '%s    ATTENTION:%s  %s%s\n\n' "$RED" "$BASE$YEL" "$REASON $BASE" "${ESC}[K"
    fi
    printf '    Time     %s%s\n' "$NOW" "${ESC}[K"
    printf '    Uptime   %s   load %s%s\n' "$UP" "$(cut -d' ' -f1-3 /proc/loadavg)" "${ESC}[K"
    printf '    Network %s%s\n' "$IP" "${ESC}[K"
    printf '    SSH      %s, %s session(s)%s\n' "$SSH" "$SESS" "${ESC}[K"
    printf '    CPU      %s%s\n' "$FQ" "${ESC}[K"
    printf '    Temp    %s%s\n' "$ZT" "${ESC}[K"
    printf '    Memory   %s%s\n' "$MEM" "${ESC}[K"
    printf '    Root     %s, %s%s\n' "$ROOT" "$DISK" "${ESC}[K"
    printf '    Storage  UFS %s%s\n' "$UFS" "${ESC}[K"
    printf '    Kernel   %s error line(s) since boot%s\n\n' "$KERR" "${ESC}[K"
    printf '    last kernel messages:%s\n' "${ESC}[K"
    dmesg 2> /dev/null | tail -n 5 | cut -c1-84 | while IFS= read -r l; do printf '    %s%s\n' "$l" "${ESC}[K"; done
    printf '%s[J' "$ESC"
}

if [ -n "$1" ]; then printf '%s%s[2J' "$PAGE_BASE" "$ESC"; collect; page "$PAGE_BASE"; exit 0; fi

trap 'exit 0' TERM
trap 'kill 0 2> /dev/null' EXIT
uptime_s > "$WAKE"
watch_keys &
TICK=0
while true; do
    NOWS=$(uptime_s)
    [ $((TICK % 5)) -eq 0 ] && collect
    if [ -n "$REASON" ]; then
        if [ "$MODE" != flash ]; then MODE=flash; printf '%s%s[2J' "$PAGE_BASE" "$ESC"; fi
        page "$PAGE_BASE"
        set_power 0
        : > "$ATTN"
        if [ -e "$ACK" ]; then set_backlight "$BL_ACK"
        elif [ $((TICK % 2)) -eq 0 ]; then set_backlight "$BL_FLASH"; else set_backlight 0; fi
    else
        rm -f "$ATTN" "$ACK"
        if [ "$MODE" != normal ]; then MODE=normal; printf '%s%s[2J' "$PAGE_BASE" "$ESC"; fi
        [ $((TICK % 5)) -eq 0 ] && page "$PAGE_BASE"
        LAST=$(cat "$WAKE" 2> /dev/null || echo 0)
        if [ $((NOWS - LAST)) -lt "$ON_SECS" ]; then
            set_power 0; set_backlight "$BL_ON"
        else
            set_power 4                                  # standby: the panel sleeps
        fi
    fi
    TICK=$((TICK + 1)); sleep 1
done
