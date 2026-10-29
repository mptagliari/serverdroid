# The ServerDroid welcome, for interactive login shells only (not for commands run over ssh, scp or sftp).
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Silence it with a ~/.hushlogin file or SERVERDROID_MOTD=0.
case $- in
*i*) [ -t 1 ] && [ "${SERVERDROID_MOTD:-1}" != 0 ] && [ ! -e "$HOME/.hushlogin" ] && [ -x /usr/local/bin/serverdroid-motd ] && /usr/local/bin/serverdroid-motd ;;
esac
