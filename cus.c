/*
 * Copyright (c) 2015, 2017 Jason L. Wright (jason@thought.net)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <sysexits.h>

#ifndef TCSASOFT
#define TCSASOFT 0
#endif

static struct termios oldterm;

void restore_term(void);
void usage(const char *);

#define RSTATE_BEGIN	0
#define	RSTATE_CR	1
#define	RSTATE_TILDE	2

int
main(int argc, char *argv[]) {
	struct sockaddr_un sun;
	int sdi, sdo, fdi, fdo;
	struct termios t;
	int rstate;
	FILE *logfile = NULL;
	char *logfilename = NULL;
	char *sunpath = NULL;
	const char *progname = argv[0];

	while ((rstate = getopt(argc, argv, "l:")) != -1) {
		switch (rstate) {
		case 'l':
			logfilename = optarg;
			logfile = fopen(logfilename, "ab");
			if (logfile == NULL)
				err(EX_CANTCREAT, "open(%s)", optarg);
			break;
		default:
			usage(argv[0]);
			return (EX_USAGE);
		}
	}

	argv += optind;
	argc -= optind;

	if (argc != 1) {
		usage(progname);
		return (EX_USAGE);
	}
	sunpath = argv[0];
 
	if (tcgetattr(fileno(stdin), &t) == -1)
		err(EX_IOERR, "tcgetattr");
	memcpy(&oldterm, &t, sizeof(t));

	cfmakeraw(&t);
	if (tcsetattr(fileno(stdin), TCSANOW | TCSASOFT, &t) == -1)
		err(EX_IOERR, "tcsetattr");
	atexit(restore_term);

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
#ifndef __linux__
	sun.sun_len = strlen(sunpath) + 1;
#endif
	if (strlen(sunpath) > sizeof(sun.sun_path))
		errx(EX_DATAERR, "%s: name too long", sunpath);
	strncpy(&sun.sun_path[0], sunpath, sizeof(sun.sun_path));

	sdi = socket(AF_UNIX, SOCK_STREAM, PF_UNSPEC);
	if (sdi == -1)
		err(EX_OSERR, "socket");

	if (connect(sdi, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(EX_IOERR, "connect");

	sdo = dup(sdi);
	if (sdo == -1)
		err(EX_OSERR, "dup");

	fdi = fileno(stdin);
	fdo = fileno(stdout);

	rstate = RSTATE_BEGIN;

	for (;;) {
		struct pollfd fds[4];
		int i;

		fds[0].fd = fdi;
		fds[0].events = POLLIN;
		fds[0].revents = 0;

		fds[1].fd = fdo;
		fds[1].events = 0;
		fds[1].revents = 0;

		fds[2].fd = sdi;
		fds[2].events = POLLIN;
		fds[2].revents = 0;

		fds[3].fd = sdo;
		fds[3].events = 0;
		fds[3].revents = 0;

		i = poll(fds, sizeof(fds)/sizeof(fds[0]), -1);
		if (i == -1)
			err(EX_OSERR, "poll");

		/* read: stdin */
		if (fds[0].revents & POLLHUP) {
			printf("stdin: HUP\n");
			goto done;
		}
		if (fds[0].revents & POLLIN) {
			char buf[128], *p;
			size_t r;

			r = read(fds[0].fd, buf, sizeof(buf));
			if (r == -1)
				err(EX_IOERR, "read(stdin)");
			if (r == 0)
				break;

			for (p = buf; r > 0; r--, p++) {
				ssize_t rr;

				(void)rr;
				if (rstate == RSTATE_BEGIN) {
					if (p[0] == '\r') {
						rstate = RSTATE_CR;
					} else
						rstate = RSTATE_BEGIN;
					rr = write(fds[3].fd, p, 1);
				} else if (rstate == RSTATE_CR) {
					if (p[0] == '~') {
						rstate = RSTATE_TILDE;
					} else {
						if (p[0] == '\r')
							rstate = RSTATE_CR;
						else
							rstate = RSTATE_BEGIN;
					}
					rr = write(fds[3].fd, p, 1);
				} else if (rstate == RSTATE_TILDE) {
					/* other '~' commands? */
					if (p[0] == '.')
						goto done;
					rstate = RSTATE_BEGIN;
					rr = write(fds[3].fd, p, 1);
				}
			}
		}

		/* read: socket */
		if (fds[2].revents & POLLHUP) {
			printf("socket: HUP\n");
			goto done;
		}
		if (fds[2].revents & POLLIN) {
			char buf[128];
			size_t r, tx;

			r = read(fds[2].fd, buf, sizeof(buf));
			if (r == -1)
				err(EX_IOERR, "read(stdin)");
			if (r == 0)
				goto done;

			if (logfile)
				fwrite(buf, r, 1, logfile);

			tx = write(fds[1].fd, buf, r);
			if (tx == -1)
				err(EX_IOERR, "write(sock)");
		}
	}


done:
	return (EX_OK);
}

void
restore_term(void) {
	tcsetattr(fileno(stdin), TCSANOW | TCSASOFT, &oldterm);
}

void
usage(const char *progname) {
	fprintf(stderr, "%s socket [-l logfile]\n", progname);
}
