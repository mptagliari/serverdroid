#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# The welcome shown after an interactive SSH login (called from /etc/profile.d/serverdroid-motd.sh): the same look as the status page of the phone's
# own display, in the terminal: the title, the phone drawn in text and a few rows of live state. It only reads /proc, /sys and a few quick
# commands, prints at most about 25 lines, and is skipped for commands run over ssh (no login shell) and with ~/.hushlogin.
ESC=$(printf '\033')
if [ "${COLORTERM:-}" = truecolor ] || [ "${COLORTERM:-}" = 24bit ]; then
    G="${ESC}[1;38;2;95;194;76m"; O="${ESC}[1;38;2;232;130;26m"
else
    G="${ESC}[1;38;5;71m"; O="${ESC}[1;38;5;208m"
fi
N="${ESC}[0m"; D="${ESC}[2m"
COLS=$(stty size 2> /dev/null | cut -d' ' -f2); [ "${COLS:-0}" -ge 44 ] 2> /dev/null || COLS=80
W=46                                   # width of the block; it is centred
L=$(((COLS - W) / 2)); [ "$L" -ge 0 ] || L=0
M=$(printf '%*s' "$L" '')
lab() { printf '%s%s%-8s%s %s\n' "$M" "$O" "[$1]" "$N" "$2"; }

# the title and the phone (15 columns, as on the display)
T=$(( (W - 15) / 2 )); P=$(printf '%*s' $((L + T)) '')
echo
printf '%s%s[ SERVER%sDROID%s ]%s\n' "$(printf '%*s' $((L + T)) '')" "$G" "$O" "$G" "$N"
printf '%s%s%s - Linux %s%s\n\n' "$M" "$D" "$(hostname)" "$(uname -r)" "$N"
printf '%s%s.-------------.%s\n' "$P" "$N" "$N"
printf '%s|  %so%s  ===  %so%s  |\n' "$P" "$G" "$N" "$G" "$N"
printf '%s| %s.---------.%s |\n' "$P" "$G" "$N"
printf '%s| %s|%s  %s/\\/\\%s   %s|%s |\n' "$P" "$G" "$N" "$G" "$N" "$G" "$N"
printf '%s| %s|%s %s/    \\%s  %s|%s |\n' "$P" "$G" "$N" "$G" "$N" "$G" "$N"
printf "%s| %s'---------'%s |\n" "$P" "$G" "$N"
printf '%s|    %s(%s %sM%s %s)%s    |\n' "$P" "$G" "$N" "$O" "$N" "$G" "$N"
printf '%s| (_) (_) (_) |\n' "$P"
printf "%s'-------------'\n\n" "$P"

# live state (quick reads only)
lab TIME "$(date '+%Y-%m-%d %H:%M %Z')"
lab UP "$(awk '{d=int($1/86400); h=int(($1%86400)/3600); m=int(($1%3600)/60); printf "%dd %dh %dm", d, h, m}' /proc/uptime), load $(cut -d' ' -f1 /proc/loadavg)"
for i in wlan0 usb0 ncm0; do
    a=$(ip -4 -o addr show "$i" 2> /dev/null | awk '{print $4}' | head -n 1)
    [ -n "$a" ] && lab NET "$i $a"
done
# say what the daemon was really started with: -s means no password logins
ssh_mode="PASSWORD LOGIN MAY BE ENABLED"
for p in /proc/[0-9]*; do
    [ "$(cat "$p/comm" 2> /dev/null)" = dropbear ] && tr '\0' ' ' < "$p/cmdline" 2> /dev/null | grep -q -- ' -s ' && { ssh_mode="key login only"; break; }
done
lab SSH "$ssh_mode, $(netstat -tn 2> /dev/null | grep ':22 ' | grep -c ESTABLISHED) session(s)"
if [ -x /usr/local/bin/serverdroid-battery ]; then
    # the same line as the status page of the display: percent, state, current, temperature and, when it is known, the time left
    b=$(/usr/local/bin/serverdroid-battery 2> /dev/null)
    pc=$(echo "$b" | sed -n 's/^percent=//p'); st=$(echo "$b" | sed -n 's/^state=//p'); ma=$(echo "$b" | sed -n 's/^current_ma=//p')
    tc=$(echo "$b" | sed -n 's/^temp_c=//p'); eta=$(echo "$b" | sed -n 's/^eta_min=//p')
    if [ -n "$pc" ]; then
        line="$pc% $st, ${ma:-?} mA, ${tc:-?} C"
        [ "${eta:--1}" -gt 0 ] 2> /dev/null && line="$line, ~$((eta / 60))h$(printf %02d $((eta % 60)))m"
        lab BATT "$line"
    fi
fi
f0=$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq 2> /dev/null); f6=$(cat /sys/devices/system/cpu/cpufreq/policy6/scaling_cur_freq 2> /dev/null)
[ -n "$f0" ] && lab CPU "$((f0 / 1000)) / $((${f6:-0} / 1000)) MHz"
tz=""
for z in /sys/class/thermal/thermal_zone*; do
    [ -r "$z/temp" ] || continue
    n=$(sed 's/-thermal//; s/cpu-little/cpu/; s/cpu-big/big/' "$z/type")
    tz="$tz $n $(($(cat "$z/temp") / 1000))"
done
[ -n "$tz" ] && lab TEMP "${tz# } C"
lab MEM "$(awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{printf "%d of %d MB free", a/1024, t/1024}' /proc/meminfo)"
lab ROOT "$(df -h / | awk 'NR==2{print $3 " of " $2 " used"}')"
echo
