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
#include <signal.h>

#include "terminal.h"
#include "edit.h"

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
	_Bool selected;
	unsigned short L;
	char str[];
} entry;

void err(const char *fmt, ...)
{
	va_list a;

	va_start(a, fmt);
	vfprintf(stderr, fmt, a);
	va_end(a);
	fflush(stderr);
	fflush(stdout);
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

static int g_winw = 0;
static int g_winh = 0;

void sighandler(int sig)
{
	switch (sig) {
	case SIGWINCH:
		get_win_dims(&g_winw, &g_winh);
		break;
	case SIGTERM:
	case SIGINT:
		exit(EXIT_SUCCESS);
	default:
		break;
	}
}

void fill_line(int linelen, const char *fmt, ...)
{
	va_list a;

	(void)linelen;

	va_start(a, fmt);
	dprintf(1, SL(CSI_CLEAR_LINE));
	vdprintf(1, fmt, a);
	va_end(a);
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
	struct sigaction sa;
	edit E;
	edit_init(&E, s, sizeof(s));

	(void)argc;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGWINCH, &sa, 0);

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

	if (!n) {
		usage(argv0);
		return 0;
	}

	s[0] = 0;

	entry *H = 0, *L = 0;
	input I;
	int x, y;
	int list_height = 5;

	raw(&old);
	get_cur_pos(&x, &y);
	fprintf(stderr, "cur_pos(%d, %d)\r\n", x, y);
	get_win_dims(&g_winw, &g_winh);
	fprintf(stderr, "win_dims(%d, %d)\r\n", g_winw, g_winh);
	fprintf(stderr, "list_height = %d\r\n", list_height);
	if (g_winh < y+list_height) {
		y -= list_height;
		for (int i = 0; i < list_height; i++) {
			dprintf(1, "~\r\n");
		}
	}
	for (;;) {
		fprintf(stderr, "cur_pos(%d, %d)\r\n", x, y);
		fprintf(stderr, "edit(%d, %d)\r\n", E.cur_x, E.cur_y);
		set_cur_pos(x, y);
		for (int i = 0; i < list_height; i++) {
			if (L) {
				fill_line(g_winw, "%c %s\r\n",
					L->selected ? '>' : ' ', L->str);
				L = L->prev;
			}
			else {
				fill_line(g_winw, "\r\n");
			}
		}
		fill_line(g_winw, ">%.*s|", E.end-E.begin, E.begin);
		write(1, SL(CSI_CURSOR_SHOW));
		set_cur_pos(1+E.cur_x+1, g_winh);
		I = get_input();
		write(1, SL(CSI_CURSOR_HIDE));
		set_cur_pos(x, y);
		switch (I.t) {
		case IT_NONE:
		default:
			break;
		case IT_UTF8:
			edit_insert(&E, I.utf, utf8_b2len(I.utf));
			break;
		case IT_SPEC:
			fprintf(stderr, "IT_SPEC::%s\n", special_type_str[I.s]);
			switch (I.s) {
			default:
				break;
			case S_ESCAPE:
				goto end;
			case S_BACKSPACE:
				edit_delete(&E, -1);
				break;
			case S_DELETE:
				edit_delete(&E, 1);
				break;
			case S_ARROW_LEFT:
				edit_move(&E, -1);
				break;
			case S_ARROW_RIGHT:
				edit_move(&E, 1);
				break;
			case S_HOME:
				edit_move(&E, -999); // TODO
				break;
			case S_END:
				edit_move(&E, 999); // TODO
				break;
			}
			break;
		case IT_CTRL:
			if (I.utf[0] == 'M' || I.utf[0] == 'J') goto end;
		}

		spawn(rw, p, arg);
		close(rw[1]); /* TODO */

		read_entries(rw[0], &H, &L);
		wait(&wstatus);
	}
	end:
	set_cur_pos(x, y);
	for (int i = 0; i < list_height+1; i++) {
		fprintf(stderr, "line!\n");
		fill_line(g_winw, "");
		if (i != list_height) {
			dprintf(1, "\r\n", 2);
		}
	}
	set_cur_pos(x, y);
	unraw(&old);
	fprintf(stderr, "'%.*s'\n", E.end-E.begin, E.begin);

	return 0;
}
