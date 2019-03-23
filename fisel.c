/*
Copyright (c) 2019 Michał Czarnecki

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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
#include <fcntl.h>
#include <regex.h>

#include "terminal.h"
#include "edit.h"

#define ARG_MAX 512

#define NO_ARG do { ++*argv; if (!(mid = **argv)) argv++; } while (0)

/* TODO
 * - match fragment highlight
 * - do pgup/pgdown differently
 * - on small number of entries use stack, not heap
 */

typedef struct entry {
	struct entry *next;
	struct entry *prev;
	_Bool selected;
	_Bool match;
	unsigned short L;
	char str[];
} entry;

static void err(const char*, ...);
static int digits(int);
static int utf8_limit_width(char*, int);
static int xgetline(int, char*, size_t, char *[2]);
//static void entry_free(entry*);
static void entry_print_and_free(entry*, int);
static int entry_match(entry*, char*, int);
static int read_entries(int, entry**, entry**);
static int str2num(char*, int, int);
static char* EARG(char***);
static char *ARG(char***);
static char* basename(char*);
static void usage(char*);
static void setup_signals(void);
static void sighandler(int);
static void prepare_window(int, int*, int*);
static void view_range_make(entry*[2], int, entry*);
static void view_range_move(entry*[2], entry **, int);
static void draw_view_range(int, entry*[2], entry*, int, int);

static char *default_delim = "|";
static char *default_subst = "{}";

/* Global, because sighandler must be able to resize window */
static int g_winw = 0;
static int g_winh = 0;
static int g_list_height = -1;

static void err(const char *fmt, ...)
{
	va_list a;

	va_start(a, fmt);
	vdprintf(2, fmt, a);
	va_end(a);
	exit(EXIT_FAILURE);
}

static int digits(int n)
{
	int d = 0;
	if (n == 0) return 1;
	while (n) {
		n /= 10;
		d++;
	}
	return d;
}

/* Returns the number of bytes that have maximum width W */
static int utf8_limit_width(char *S, int W)
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

/* TODO test other line endings like \r\n */
static int xgetline(int fd, char *buf, size_t bufs, char *b[2])
{
	char *src, *dst;
	size_t s;
	ssize_t r;
	int i, L = 0;

	src = b[0];
	dst = buf;
	i = s = b[1] - b[0];
	while (i--) {
		*dst++ = *src++;
	}
	b[0] = buf;
	b[1] = buf + s;

	for (;;) {
		while (*b[0] != '\n' && *b[0] != '\r' && b[1] - b[0] > 0) {
			b[0]++;
		}
		if (b[1] - b[0] > 0 && (*b[0] == '\n' || *b[0] == '\r')) {
			L = b[0] - buf;
			break;
		}
		else {
			r = read(fd, b[1], bufs-(b[1]-buf));
			if (r == -1) {
				return -2;
			}
			if (r == 0) {
				return -1;
			}
			b[1] += r;
		}
	}
	*b[0] = 0;
	b[0]++;
	if (b[1] - b[0] > 0 && (*b[0] == '\n' || *b[0] == '\r')) {
		*b[0] = 0;
		b[0]++;
	}
	return L;
}
#if 0
static void entry_free(entry *H)
{
	entry *F;
	while (H) {
		F = H;
		H = H->next;
		free(F);
	}
}
#endif
static void entry_print_and_free(entry *H, int fd)
{
	entry *T;
	while (H) {
		if (fd != -1 && H->selected) {
			dprintf(fd, "%s\n", H->str);
		}
		T = H;
		H = H->next;
		free(T);
	}
}

static int entry_match(entry *H, char *reg, int cflags)
{
	regex_t R;
	int n = 0, e, eflags = 0;
	regmatch_t pmatch;

	e = regcomp(&R, reg, cflags);
	if (e) return 0;

	while (H) {
		H->match = 0 == regexec(&R, H->str, 1, &pmatch, eflags);
		n += H->match;
		H = H->next;
	}
	regfree(&R);
	return n;
}

static int read_entries(int fd, entry **head, entry **last)
{
	entry *q;
	int n = 0, L;
	char buf[128];
	char *b[2] = { buf, buf };

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
	return L == -2 ? -1 : n;
}

static int str2num(char *s, int min, int max)
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

static char* EARG(char ***argv)
{
	char *a;
	a = ARG(argv);
	if (!a) {
		err("ERROR: Expected argument.\n");
	}
	return a;
}

static char* basename(char *S)
{
	char *s = S;
	while (*s) s++;
	while (S != s && *s != '/') s--;
	if (*s == '/') s++;
	return s;
}

static void usage(char *argv0)
{
	dprintf(2, "Usage: %s [options]\n", basename(argv0));
	dprintf(2,
	"Options:\n"
	"    -h     Display this help message and exit.\n"
	"    -d C   Set delimiter. Default: %s\n"
	"    -s C   Set substitution. Default: %s\n",
	default_delim,
	default_subst
	);
}

static void setup_signals(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sigaction(SIGWINCH, &sa, 0);
	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGINT, &sa, 0);
}

static void sighandler(int sig)
{
	switch (sig) {
	case SIGWINCH:
		get_win_dims(2, &g_winw, &g_winh);
		if (g_list_height >= g_winh) {
			g_list_height = g_winh-1;
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
	if (g_list_height == -1) {
		g_list_height = g_winh-1;
	}
	if (g_winh < *y + g_list_height) {
		*y = g_winh - (g_list_height + 1) + 1;
		for (i = 0; i < g_list_height; i++) {
			write(fd, "\r\n", 2);
		}
	}
}

static void view_range_make(entry *vr[2], int list_height, entry *L)
{
	vr[0] = vr[1] = L;
	while (!vr[0]->match) {
		vr[0] = vr[0]->next;
	}
	list_height--;
	while (list_height && vr[1]->next) {
		if (vr[1]->match) {
			list_height--;
		}
		vr[1] = vr[1]->next;
	}
}

static void view_range_move(entry *vr[2], entry **hl, int n)
{
	/* TODO offsetof? */
	entry *last_visible = 0;
	if (n > 0) {
		while (n-- && *hl && (*hl)->next) {
			if ((*hl)->match) {
				last_visible = *hl;
			}
			if (*hl == vr[1]) {
				vr[1] = vr[1]->next;
				vr[0] = vr[0]->next;
			}
			*hl = (*hl)->next;
		}
	}
	else if (n < 0) {
		while (n++ && *hl && (*hl)->prev) {
			if ((*hl)->match) {
				last_visible = *hl;
			}
			if (*hl == vr[0]) {
				vr[1] = vr[1]->prev;
				vr[0] = vr[0]->prev;
			}
			*hl = (*hl)->prev;
		}
	}
	if (!(*hl)->match) {
		*hl = last_visible;
	}
}

static void draw_view_range(int fd, entry *vr[2], entry *hl, int W, int H)
{
	entry *w;
	int b;

	w = vr[0];
	while (w && H) {
		if (!w->match) {
			w = w->next;
			continue;
		}
		if (w == hl) {
			dprintf(fd, "\x1b[%c%cm", '3', '0');
			dprintf(fd, "\x1b[%c%cm", '4', '7');
		}
		b = utf8_limit_width(w->str, W-2);
		dprintf(fd, "%s%c %.*s", CSI_CLEAR_LINE,
			w == hl || w->selected ? '>' : ' ', b, w->str);
		if (w == hl) {
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
	char s[4*1024];
	char *argv0;
	int x, y, utflen;
	int selected = 0, num = 0, matching;
	int inputfd = 0, drawfd = 2;
	int cflags = REG_EXTENDED | REG_ICASE | REG_NEWLINE;
	_Bool mid = 0, update = 1;
	struct termios old;
	edit E;
	entry *highlight = 0;
	entry *list[2] = { 0, 0 };
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
			g_list_height = str2num(EARG(&argv), 1, 1000); // TODO
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

	setup_signals();

	/*
	 * fisel is passed data via stdin.
	 * Like so: some-command | fisel
	 * In such case user input will be available in /dev/tty
	 */
	if (isatty(0)) {
		usage(argv0);
		return 0;
	}
	else {
		inputfd = open("/dev/tty", O_RDONLY);
		if (inputfd == -1) {
			err("Failed to open /dev/tty.\n");
		}
		num = read_entries(0, &list[0], &list[1]);
		highlight = list[0];
		if (num == 0) {
			usage(argv0);
			return 0;
		}
	}

	if (-1 == raw(&old, inputfd)) {
		err("Couldn't initialize terminal.\n");
	}
	write(drawfd, SL(CSI_CURSOR_HIDE));
	get_cur_pos(drawfd, &x, &y);
	get_win_dims(drawfd, &g_winw, &g_winh);
	prepare_window(drawfd, &x, &y);
	edit_init(&E, s, sizeof(s));

	for (;;) {
		if (update) {
			update = 0;
			view_range[0] = view_range[1] = 0;

			matching = entry_match(list[0], E.begin, cflags);
			if (matching) {
				view_range_make(view_range, g_list_height, list[0]);
				if (highlight && !highlight->match) {
					highlight = view_range[0];
				}
			}
		}

		set_cur_pos(drawfd, x, y);
		draw_view_range(drawfd, view_range, highlight, g_winw, g_list_height);
		write(drawfd, SL(CSI_CLEAR_LINE));

		int d = digits(num);
		int i = dprintf(drawfd, "%*d/%*d/%d > ", d, selected, d, matching, num);
		dprintf(drawfd, "%.*s", (int)(E.end-E.begin), E.begin);

		set_cur_pos(drawfd, 1+E.cur_x+i, y+g_list_height);
		write(drawfd, SL(CSI_CURSOR_SHOW));
		I = get_input(inputfd);
		write(drawfd, SL(CSI_CURSOR_HIDE));
		set_cur_pos(drawfd, x, y);

		switch (I.t) {
		case IT_NONE:
		default:
			break;
		case IT_EOF:
			goto end;
		case IT_UTF8:
			update = 1;
			utflen = utf8_b2len(I.utf);
			edit_insert(&E, I.utf, utflen);
			break;
		case IT_SPEC:
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
				view_range_move(view_range, &highlight, -g_list_height);
				break;
			case S_PAGE_DOWN:
				view_range_move(view_range, &highlight, g_list_height);
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
	}
end:
	set_cur_pos(drawfd, x, y);
	for (int i = 0; i < g_list_height+1; i++) {
		dprintf(drawfd, "%s", CSI_CLEAR_LINE);
		if (i != g_list_height) {
			dprintf(drawfd, "\r\n");
		}
	}
	set_cur_pos(drawfd, x, y);
	unraw(&old, inputfd);
	write(drawfd, SL(CSI_CURSOR_SHOW));

	if (!selected && highlight) {
		dprintf(1, "%s\n", highlight->str);
	}
	else {
		entry_print_and_free(list[0], 1);
	}
	return 0;
}
