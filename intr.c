#ifndef _DEFAULT_SOURCE
	#define _DEFAULT_SOURCE
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>

#include "terminal.h"

#define ARG_MAX 512

/* TODO
 * - test & rename spawn()
 * - getline from fd
 * - raw mode and print only last N
 * - raw mode and SIGINT, SIGTERM...
 */

typedef struct entry {
	struct entry *next;
	struct entry *prev;
	size_t L;
	char str[];
} entry;

void err(const char *fmt, ...)
{
	va_list a;

	va_start(a, fmt);
	vfprintf(stderr, fmt, a);
	va_end(a);
	fflush(stderr);
	exit(EXIT_FAILURE);
}

int spawn(int rw[2], pid_t *p, char **argv[])
{
	int pair[2], l;

	/* pipe: [0] = read, [1] = write */
	if (pipe(pair)) {
		return -1;
	}
	l = pair[0];
	rw[1] = pair[1];
	while (*argv) {
		if (pipe(pair) || (*p = fork()) == -1) { /* TODO vfork() ? */
			return -1;
		}
		if (*p == 0) {
			close(rw[0]);
			close(rw[1]);

			dup2(l, 0);

			dup2(pair[1], 1);
			close(pair[0]);

			execvp((*argv)[0], *argv);
			exit(EXIT_FAILURE);
		}
		close(l);
		close(pair[1]);
		l = pair[0];
		argv++;
		p++;
	}
	rw[0] = l;
	return 0;
}

int xgetline(int fd, char* buf, size_t bufsize, size_t* linelen)
{
	/* TODO */
	int r;
	*linelen = 0;

	(void)bufsize;

	while ((r = read(fd, buf, 1))) {
		if (*buf == '\n' || *buf == '\r') {
			*buf = 0;
			return 0;
		}
		++*linelen;
		buf++;
	}
	return 1;
}

int read_entries(int fd, entry **head, entry **last)
{
	entry *q;
	int n = 0, r;
	char buf[BUFSIZ];
	size_t L = 0;

	*head = 0;
	while (!(r = xgetline(fd, buf, sizeof(buf), &L))) {
		q = malloc(sizeof(entry)+L+1);
		q->prev = *last;
		q->next = 0;
		q->L = L;
		memcpy(q->str, buf, L+1);
		if (!*head) {
			*head = q;
		}
		else {
			(*last)->next = q;
		}
		*last = q;
		n++;
	}
	return n;
}

int str2num(char *s, int max)
{
	int n = 0;

	while (*s && '0' <= *s && *s <= '9') {
		n *= 10;
		n += *s - '0';
		s++;
	}
	if (*s) {
		err("ERROR: Not a number.\n");
	}
	if (n > max) {
		err("ERROR: Number too big. Maximum is %d\n", max);
	}
	return n;
}

#define NO_ARG do { ++*argv; if (!(mid = **argv)) argv++; } while (0)

static char *ARG(char ***argv)
{
	char *r = 0;

	(**argv)++;
	if (***argv) { /* -oARG */
		r = **argv;
		(*argv)++;
	}
	else { /* -o ARG */
		(*argv)++;
		if (**argv && ***argv != '-') {
			r = **argv;
			(*argv)++;
		}
	}
	return r;
}

static char *EARG(char ***argv)
{
	char *a;
	a = ARG(argv);
	if (!a) {
		err("ERROR: Expected argument.\n");
	}
	return a;
}

char* basename(char *S)
{
	char *s = S;
	while (*s) s++;
	while (S != s && *s != '/') s--;
	if (*s == '/') s++;
	return s;
}

static char *default_delim = "|";
static char *default_subst = "{}";

void usage(char *argv0)
{
	fprintf(stderr, "Usage: %s [options]\n", basename(argv0));
	fprintf(stderr,
	"Options:\n"
	"    -h     Display this help message and exit.\n"
	"    -d C   Set delimiter. Default: %s\n"
	"    -s C   Set substitution. Default: %s\n",
	default_delim,
	default_subst
	);
}

int main(int argc, char *argv[])
{
	char s[512];
	char *delim = default_delim,
	     *subst = default_subst,
	     *argv0,
	     **arg[ARG_MAX];
	int rw[2], wstatus, n = 0;
	pid_t p[3];
	_Bool mid = 0;
	struct termios old;

	(void)argc;

	argv0 = *argv;
	++argv;
	while ((mid && **argv) || (*argv && **argv == '-')) {
		if (!mid) ++*argv;
		mid = 0;
		if ((*argv)[0] == '-' && (*argv)[1] == 0) {
			argv++;
			break;
		}
		switch (**argv) {
		case 'd':
			delim = EARG(&argv);
			break;
		case 's':
			subst = EARG(&argv);
			break;
		case 'h':
			usage(argv0);
			NO_ARG;
			return 0;
		default:
			usage(argv0);
			return 1;
		}
	}
	arg[n] = argv;
	arg[n+1] = 0;
	while (*argv) {
		if (!strcmp(*argv, delim)) {
			*argv = 0;
			n++;
			if (n >= ARG_MAX) {
				err("error: too many commands\n");
			}
			argv++;
			arg[n] = argv;
			arg[n+1] = 0;
		}
		else if (!strcmp(*argv, subst)) {
			*argv = s;
		}
		argv++;
	}

	s[0] = 0;

	char *curr = s;
	entry *H = 0, *L = 0;
	raw(&old);
	input I;
	int utflen;
	for (;;) {
		I = get_input();
		switch (I.t) {
		case IT_NONE:
		default:
			break;
		case IT_UTF8:
			utflen = utf8_b2len(I.utf);
			memcpy(curr, I.utf, utflen);
			curr += utflen;
			*curr = 0;
			break;
		case IT_SPEC:
			break;
		case IT_CTRL:
			if (I.utf[0] == 'M' || I.utf[0] == 'J') goto end;
			break;
		}

		spawn(rw, p, arg);
		close(rw[1]); /* TODO */
		read_entries(rw[0], &H, &L);
		printf("\r%s                      ", s);
		fflush(stdout);
		wait(&wstatus);
	}
	end:
	unraw(&old);
	printf("\r");
	fflush(stdout);

	return 0;
}
