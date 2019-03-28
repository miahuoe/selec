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

#define NO_ARG do { ++*argv; if (!(mid = **argv)) argv++; } while (0)

/* TODO
 * - VEEEERY SLOW
 * - exits on itself (?) when terminal changes size
 * - long input must scroll
 * - match fragment highlight
 */

typedef struct entry {
	struct entry *next;
	_Bool selected;
	unsigned short L;
	char str[];
} entry;

static void err(const char*, ...);
static int digits(int);
static int utf8_limit_width(char*, int);
static int xgetline(int, char*, size_t, char *[2]);
static void entry_free(entry*);
static void entry_print_matching_and_free(entry**, int);
static int entry_match(entry*, entry**, char*, int);
static int read_entries(int, entry**, entry**);
static int str2num(char*, int, int);
static char* EARG(char***);
static char *ARG(char***);
static char* basename(char*);
static void usage(char*);
static void setup_signals(void);
static void sighandler(int);
static void prepare_window(int, int*, int*);
static void view_range_draw(int, entry**, int[2], int, int, int);
static void view_range_move(entry**, int[2], int*, int);
static void fill_matching(entry*, entry**);

/* Global, because sighandler must be able to resize window */
static int winw = 0;
static int winh = 0;
static int list_height = -1;
static struct termios old;

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

	while (n) {
		n /= 10;
		d++;
	}
	return d ? d : 1;
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

static void entry_free(entry *H)
{
	entry *F;
	while (H) {
		F = H;
		H = H->next;
		free(F);
	}
}

static void entry_print_matching_and_free(entry **M, int fd)
{
	while (*M) {
		if ((*M)->selected) {
			dprintf(fd, "%s\n", (*M)->str);
		}
		free(*M);
		++M;
	}
}

static int entry_match(entry *H, entry **L, char *reg, int cflags)
{
	regex_t R;
	int n = 0, e, eflags = 0;
	regmatch_t pmatch;

	e = regcomp(&R, reg, cflags);
	if (e) return 0;

	*L = 0;
	L++;
	while (H) {
		if (0 == regexec(&R, H->str, 1, &pmatch, eflags)) {
			*L = H;
			L++;
			*L = 0;
			n++;
		}
		H = H->next;
	}
	regfree(&R);
	return n;
}

static int read_entries(int fd, entry **head, entry **last)
{
	entry *q;
	int n = 0, L;
	char buf[BUFSIZ];
	char *b[2] = { buf, buf };

	*head = 0;
	while (0 <= (L = xgetline(fd, buf, sizeof(buf), b))) {
		q = calloc(1, sizeof(entry)+L+1);
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
	"    -E     Use extended regex.\n"
	"    -i     Case sensitive.\n"
	"    -h     Display this help message and exit.\n");
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
		get_win_dims(2, &winw, &winh);
		list_height = winh-1;
		break;
	case SIGTERM:
	case SIGINT:
		// TODO cleanup
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
		list_height = winh-1;
	}
	if (winh < *y + list_height) {
		*y = winh - (list_height + 1) + 1;
		for (i = 0; i < list_height; i++) {
			write(fd, "\r\n", 2);
		}
	}
}

static void view_range_draw(int fd, entry **L, int view[2], int hl, int W, int H)
{
	int c, b, ind;

	c = view[0];
	while (L[c] && H) {
		b = utf8_limit_width(L[c]->str, W-2);
		ind = c == hl || L[c]->selected ? '>' : ' ';
		if (c == hl) {
			dprintf(fd,
				"\x1b[%c%cm" /* CSI */
				"\x1b[%c%cm" /* CSI */
				"%s%c %.*s" /* entry */
				"\x1b[%cm" /* CSI */
				"\r\n", /* newline */
				'3', '0',
				'4', '7',
				CSI_CLEAR_LINE, ind, b, L[c]->str,
				'0');
		}
		else {
			dprintf(fd,
				"%s%c %.*s" "\r\n",
				CSI_CLEAR_LINE, ind, b, L[c]->str);
		}
		c++;
		H--;
	}
	while (H) {
		dprintf(fd, "%s\r\n", CSI_CLEAR_LINE);
		H--;
	}
}

static void view_range_move(entry **L, int view[2], int *hl, int y)
{
	if (y > 0) {
		while (y--) {
			if (L[1+*hl] && view[1] == 1+*hl) {
				++view[0];
				++view[1];
			}
			if (L[1+*hl]) {
				++*hl;
			}
		}
	}
	else if (y < 0) {
		while (y++) {
			if (L[-1+*hl] && view[0] == *hl) {
				--view[0];
				--view[1];
			}
			if (L[-1+*hl]) {
				--*hl;
			}
		}
	}
}

static void fill_matching(entry *H, entry **L)
{
	L++;
	while (H) {
		*L = H;
		L++;
		*L = 0;
		H = H->next;
	}
}

int main(int argc, char *argv[])
{
	char s[4*1024], *argv0;
	int x, y, d, i;
	int selected = 0, num = 0, num_matching;
	int inputfd = 0, outfd = 1, drawfd = 2;
	int cflags = REG_ICASE | REG_NEWLINE;
	_Bool mid = 0, update = 1;
	edit E;
	int view[2];
	int highlight; // TODO find after change
	entry **matching; // TODO simplify
	entry *list[2] = { 0, 0 };
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
		case 'i':
			cflags &= ~REG_ICASE;
			NO_ARG;
			break;
		case 'E':
			cflags |= REG_EXTENDED;
			NO_ARG;
			break;
		case 'L':
			list_height = str2num(EARG(&argv), 1, 1000); // TODO
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
	 * User input will be available in /dev/tty
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

		matching = malloc((num+2) * sizeof(entry*));
		view[0] = 1;
		view[1] = 1+(num < list_height ? num : list_height);
		highlight = 1;
		fill_matching(list[0], matching);

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
	get_win_dims(drawfd, &winw, &winh);
	prepare_window(drawfd, &x, &y);
	edit_init(&E, s, sizeof(s));

	for (;;) {
		if (update) {
			update = 0;
			num_matching = entry_match(list[0], matching, E.begin, cflags);
			view[0] = 1;
			view[1] = 1+(num_matching < list_height ? num_matching : list_height);
			highlight = 1;
		}

		set_cur_pos(drawfd, x, y);
		view_range_draw(drawfd, matching, view, highlight, winw, list_height);
		write(drawfd, SL(CSI_CLEAR_LINE));

		d = digits(num);
		i = dprintf(drawfd, "%*d/%*d/%d > ", d, selected, d, num_matching, num);
		dprintf(drawfd, "%.*s", utf8_limit_width(E.begin, winw-i), E.begin);

		set_cur_pos(drawfd, 1+E.cur_x+i, y+list_height);

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
			edit_insert(&E, I.utf, utf8_b2len(I.utf));
			break;
		case IT_SPEC:
			switch (I.s) {
			default:
				break;
			case S_ESCAPE:
				selected = 0;
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
				view_range_move(matching, view, &highlight, -list_height);
				break;
			case S_PAGE_DOWN:
				view_range_move(matching, view, &highlight, list_height);
				break;
			case S_ARROW_UP:
				view_range_move(matching, view, &highlight, -1);
				break;
			case S_ARROW_DOWN:
				view_range_move(matching, view, &highlight, 1);
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
				if (!selected) {
					matching[highlight]->selected = 1;
					selected = 1;
				}
				goto end;
			case 'I': /* TAB */
				matching[highlight]->selected = !matching[highlight]->selected;
				selected += matching[highlight]->selected ? 1 : -1;
				view_range_move(matching, view, &highlight, 1);
				break;
			}
			break;
		}
	}
end:
	set_cur_pos(drawfd, x, y);
	for (i = 0; i < list_height+1; i++) {
		dprintf(drawfd, "%s", CSI_CLEAR_LINE);
		if (i != list_height) {
			dprintf(drawfd, "\r\n");
		}
	}
	set_cur_pos(drawfd, x, y);
	unraw(&old, inputfd);
	write(drawfd, SL(CSI_CURSOR_SHOW));

	if (!selected) {
		entry_free(list[0]);
	}
	else {
		entry_print_matching_and_free(matching+1, outfd);
	}
	return 0;
}
