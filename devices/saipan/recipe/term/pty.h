/* The link to Linux: a pseudo terminal with a shell on its slave side, in a session of its own. No terminal emulation and no drawing here. */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#ifndef TERM_PTY_H
#define TERM_PTY_H
#include <sys/types.h>

typedef struct {
	int master;		/* non-blocking */
	pid_t pid;		/* the session leader, 0 once it has been reaped */
	pid_t sid;		/* the session id (the leader's pid), kept after the leader is gone: its jobs may still run */
	int status;		/* wait status once it has been reaped */
} Pty;

int pty_spawn(Pty *p, const char *shell, int rows, int cols);	/* 0 on success */
void pty_resize(Pty *p, int rows, int cols, int xpixel, int ypixel);	/* TIOCSWINSZ; the kernel sends SIGWINCH to the foreground group */
int pty_reap(Pty *p);		/* 1 when the child has exited (status set), 0 while it runs */
void pty_terminate(Pty *p);	/* hang up the whole session, then kill it if needed, and reap it: no orphans */

#endif
