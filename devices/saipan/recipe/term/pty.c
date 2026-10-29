#define _GNU_SOURCE
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "pty.h"

int pty_spawn(Pty *p, const char *shell, int rows, int cols)
{
	struct winsize ws = { .ws_row = rows, .ws_col = cols };
	char *slave;
	int m;
	pid_t pid;

	memset(p, 0, sizeof(*p));
	p->master = -1;
	m = posix_openpt(O_RDWR | O_NOCTTY);
	if (m < 0 || grantpt(m) < 0 || unlockpt(m) < 0 || !(slave = ptsname(m))) {
		if (m >= 0)
			close(m);
		return -1;
	}
	ioctl(m, TIOCSWINSZ, &ws);
	pid = fork();
	if (pid < 0) {
		close(m);
		return -1;
	}
	if (pid == 0) {
		int s;

		setsid();				/* a session of its own: the slave becomes its controlling terminal */
		s = open(slave, O_RDWR);
		if (s < 0)
			_exit(127);
		ioctl(s, TIOCSCTTY, 0);
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (s > 2)
			close(s);
		close(m);
		for (int fd = 3; fd < 64; fd++)
			close(fd);
		signal(SIGPIPE, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);
		signal(SIGHUP, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		setenv("TERM", "xterm-256color", 1);
		setenv("HOME", "/root", 1);
		setenv("USER", "root", 1);
		setenv("LOGNAME", "root", 1);
		setenv("SHELL", shell, 1);
		setenv("LANG", "C.UTF-8", 1);
		setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
		chdir("/root");
		execl(shell, "-ash", "-l", (char *)NULL);	/* a login shell: it reads /etc/profile */
		_exit(127);
	}
	fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
	fcntl(m, F_SETFD, FD_CLOEXEC);
	p->master = m;
	p->pid = pid;
	p->sid = pid;
	return 0;
}

void pty_resize(Pty *p, int rows, int cols, int xpixel, int ypixel)
{
	struct winsize ws = { .ws_row = rows, .ws_col = cols, .ws_xpixel = xpixel, .ws_ypixel = ypixel };

	if (p->master >= 0)
		ioctl(p->master, TIOCSWINSZ, &ws);
}

int pty_reap(Pty *p)
{
	int st;

	if (!p->pid)
		return 1;
	if (waitpid(p->pid, &st, WNOHANG) == p->pid) {
		p->status = st;
		p->pid = 0;
		return 1;
	}
	return 0;
}

static void sleep_ms(int ms)
{
	struct timespec ts = { 0, ms * 1000000L };
	nanosleep(&ts, NULL);
}

/* Every process of the session led by `sid` (jobs in the background are in groups of their own, so killing the shell's group is not enough). */
static void kill_session(pid_t sid, int sig)
{
	DIR *d = opendir("/proc");
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		char path[64], buf[512], *p;
		int fd, pid = atoi(e->d_name);
		long s;
		ssize_t n;

		if (pid <= 1)
			continue;
		snprintf(path, sizeof(path), "/proc/%d/stat", pid);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n <= 0)
			continue;
		buf[n] = 0;
		p = strrchr(buf, ')');			/* the name may contain spaces and parentheses: fields start after the last ")" */
		if (p && sscanf(p + 2, "%*c %*d %*d %ld", &s) == 1 && s == sid)
			kill(pid, sig);
	}
	closedir(d);
}

void pty_terminate(Pty *p)
{
	if (p->pid) {
		kill(-p->pid, SIGHUP);			/* the shell's own group first: it hangs up its jobs */
		kill(-p->pid, SIGCONT);
		for (int i = 0; i < 30 && !pty_reap(p); i++)
			sleep_ms(10);
	}
	if (p->sid) {
		kill_session(p->sid, SIGHUP);		/* whatever is left in the session, also after the shell is gone */
		kill_session(p->sid, SIGCONT);
		sleep_ms(50);
		kill_session(p->sid, SIGKILL);
	}
	for (int i = 0; p->pid && i < 100 && !pty_reap(p); i++)
		sleep_ms(10);
	p->sid = 0;
	if (p->master >= 0) {
		close(p->master);
		p->master = -1;
	}
}
