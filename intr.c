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
 * - copy selection on rescan
 * - do pgup/pgdown differently
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

/* Returns the number of bytes that have maximum width W */
int utf8_limit_width(char *S, int W)
{
	int bytes = 0, b, cp, cpw;
	while ((b = utf8_dechar(&cp, S)) && W) {
		cpw = utf8_cp2w(cp);
		if (W < cpw) break;
		W -= cpw;
		bytes += b;
		S += b;
	}
	return bytes;
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

/* TODO test other line endings like \r\n */
int xgetline(int fd, char *buf, size_t bufs, char *b[3])
{
	char *src, *dst;
	size_t s;
	ssize_t r;
	int i, L = 0;

	src = b[1];
	dst = b[0];
	i = s = b[2] - b[1];
	while (i--) {
		*dst++ = *src++;
	}
	b[0] = buf;
	b[1] = buf;
	b[2] = buf + s;

	for (;;) {
		while (*b[1] != '\n' && *b[1] != '\r' && b[2] - b[1] > 0) {
			b[1]++;
		}
		if (b[2] - b[1] > 0 && (*b[1] == '\n' || *b[1] == '\r')) {
			L = b[1] - b[0];
			break;
		}
		else {
			r = read(fd, b[2], bufs-(b[2]-b[0]));
			if (r == -1) {
				return -2;
			}
			if (r == 0) {
				return -1;
			}
			b[2] += r;
		}
	}
	*b[1] = 0;
	b[1]++;
	if (b[2] - b[1] > 0 && (*b[1] == '\n' || *b[1] == '\r')) {
		*b[1] = 0;
		b[1]++;
	}
	return L;
}

void entry_free(entry *H)
{
	entry *F;
	while (H) {
		F = H;
		H = H->next;
		free(F);
	}
}

int read_entries(int fd, entry **head, entry **last)
{
	entry *q;
	int n = 0, L;
	char buf[BUFSIZ];
	char *b[3] = { buf, buf, buf };

	*head = 0;
	while (0 <= (L = xgetline(fd, buf, sizeof(buf), b))) {
		q = calloc(1, sizeof(entry)+L+1);
		q->prev = *last;
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

int str2num(char *s, int min, int max)
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
	if (n < min) {
		err("ERROR: Number too small. Minimum is %d\n", min);
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
static int list_height = -1;

static void sighandler(int sig)
{
	switch (sig) {
	case SIGWINCH:
		get_win_dims(&g_winw, &g_winh);
		if (list_height >= g_winh) {
			list_height = g_winh-1;
		}
		break;
	case SIGTERM:
	case SIGINT:
		exit(EXIT_SUCCESS);
	default:
		break;
	}
}

static void prepare_window(int fd, int *x, int *y)
{
	int i;

	*x = 1;
	if (list_height == -1) {
		list_height = g_winh-1;
	}
	if (g_winh < *y + list_height) {
		*y = g_winh - (list_height + 1) + 1;
		for (i = 0; i < list_height; i++) {
			write(fd, "\r\n", 2);
		}
	}
}

static void view_range_make(entry *view_range[2], int list_height, entry *L)
{
	view_range[0] = view_range[1] = L;
	list_height--; /* Already have the first one. It's view_range[0] */
	while (list_height-- && view_range[1]->next) {
		view_range[1] = view_range[1]->next;
	}
}

static void view_range_move(entry *view_range[2], entry **highlight, int n)
{
	/* TODO offsetof? */
	if (n > 0) {
		while (n-- && *highlight && (*highlight)->next) {
			if (*highlight == view_range[1]) {
				view_range[1] = view_range[1]->next;
				view_range[0] = view_range[0]->next;
			}
			*highlight = (*highlight)->next;
		}
	}
	else if (n < 0) {
		while (n++ && *highlight && (*highlight)->prev) {
			if (*highlight == view_range[0]) {
				view_range[1] = view_range[1]->prev;
				view_range[0] = view_range[0]->prev;
			}
			*highlight = (*highlight)->prev;
		}
	}
}

static void draw_view_range(entry *view_range[2], entry *highlight, int W, int H)
{
	entry *w;
	int fd = 2, b;

	w = view_range[0];
	while (w && H) {
		if (w == highlight) {
			dprintf(fd, "\x1b[%c%cm", '3', '0');
			dprintf(fd, "\x1b[%c%cm", '4', '7');
		}
		b = utf8_limit_width(w->str, W-2);
		dprintf(fd, "%s%c %.*s", CSI_CLEAR_LINE,
			w == highlight || w->selected ? '>' : ' ', b, w->str);
		if (w == highlight) {
			dprintf(fd, "\x1b[%cm", '0');
		}
		dprintf(fd, "\r\n");
		w = w->next;
		H--;
	}
	while (H) {
		dprintf(fd, "%s \r\n", CSI_CLEAR_LINE);
		H--;
	}
}

int main(int argc, char *argv[])
{
	char s[512]; /* TODO */
	char *delim = default_delim,
	     *subst = default_subst,
	     *argv0, **arg[ARG_MAX];
	int rw[2], wstatus, n = 0, x, y, utflen;
	int selected = 0;
	pid_t p[3];
	_Bool mid = 0, update = 1;
	struct termios old;
	struct sigaction sa;
	edit E;
	entry *H = 0, *L = 0, *highlight = 0;
	entry *view_range[2] = { 0, 0 };
	input I;

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
		case 'L':
			list_height = str2num(EARG(&argv), 1, 1000); // TODO
			break;
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

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sigaction(SIGWINCH, &sa, 0);
	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGINT, &sa, 0);

	raw(&old);
	get_cur_pos(&x, &y);
	get_win_dims(&g_winw, &g_winh);
	prepare_window(2, &x, &y);
	edit_init(&E, s, sizeof(s));

	for (;;) {
		set_cur_pos(x, y);
		draw_view_range(view_range, highlight, g_winw, list_height);
		dprintf(2, "%s> %.*s", CSI_CLEAR_LINE, (int)(E.end-E.begin), E.begin);
		set_cur_pos(1+E.cur_x+2, y+list_height);
		write(2, SL(CSI_CURSOR_SHOW));
		I = get_input();
		write(2, SL(CSI_CURSOR_HIDE));
		set_cur_pos(x, y);

		switch (I.t) {
		case IT_NONE:
			//printf("NONE\n");
		default:
			break;
		case IT_UTF8:
			update = 1;
			utflen = utf8_b2len(I.utf);
			edit_insert(&E, I.utf, utflen);
			//printf("UTF8::'%.*s'\n", utflen, I.utf);
			break;
		case IT_SPEC:
			//printf("SPEC::'%s'\n", special_type_str[I.s]);
			switch (I.s) {
			default:
				break;
			case S_ESCAPE:
				goto end;
			case S_BACKSPACE:
				update = 1;
				edit_delete(&E, -1);
				break;
			case S_DELETE:
				update = 1;
				edit_delete(&E, 1);
				break;
			case S_PAGE_UP:
				view_range_move(view_range, &highlight, -list_height);
				break;
			case S_PAGE_DOWN:
				view_range_move(view_range, &highlight, list_height);
				break;
			case S_ARROW_UP:
				view_range_move(view_range, &highlight, -1);
				break;
			case S_ARROW_DOWN:
				view_range_move(view_range, &highlight, 1);
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
			//printf("CTRL::'%c'\n", I.utf[0]);
			switch (I.utf[0]) {
			case 'M':
			case 'J': /* ENTER */
				goto end;
			case 'I': /* TAB */
				if (highlight) {
					highlight->selected = !highlight->selected;
					selected += highlight->selected ? 1 : -1;
				}
				break;
			}
			break;
		}

		if (update) {
			update = 0;
			selected = 0;
			highlight = 0;
			view_range[0] = view_range[1] = 0;
			entry_free(H);
			H = L = highlight = 0;
			if (*E.begin) {
				spawn(rw, p, arg);
				close(rw[1]); /* TODO */

				read_entries(rw[0], &H, &L);
				if (H) {
					view_range_make(view_range, list_height, H);
					highlight = view_range[0];
					wait(&wstatus);
				}
			}
		}

	}
end:
	set_cur_pos(x, y);
	for (int i = 0; i < list_height+1; i++) {
		dprintf(2, "%s", CSI_CLEAR_LINE);
		if (i != list_height) {
			dprintf(2, "\r\n");
		}
	}
	set_cur_pos(x, y);
	unraw(&old);

	if (!selected && highlight) {
		printf("%s\n", highlight->str);
	}
	entry *T = H;
	while (T) {
		if (T->selected) {
			printf("%s\n", T->str);
		}
		T = T->next;
	}
	entry_free(H);
	return 0;
}
