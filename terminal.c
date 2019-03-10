#include "terminal.h"

void set_cur_pos(int x, int y)
{
	dprintf(1, "\x1b[%d;%dH", y, x);
}

void get_cur_pos(int *x, int *y)
{
	// TODO
	dprintf(1, "\x1b[6n", 4);
	scanf("\x1b[%d;%dR", y, x);
}

void get_win_dims(int *C, int *R)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
		*R = *C = 0;
	}
	*R = ws.ws_row;
	*C = ws.ws_col;
}

int move_cursor(int R, int C) {
	char buf[1+1+4+1+4+1+1];
	size_t n;
	n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", R, C);
	return (write(STDOUT_FILENO, buf, n) == -1 ? errno : 0);
}

int raw(struct termios* before)
{
	struct termios raw;

	if (tcgetattr(0, before)) return errno;
	raw = *before;
	cfmakeraw(&raw);
	//raw.c_cc[VINTR] = 0x03;
	//raw.c_cc[VSUSP] = 0x1a;
	raw.c_iflag &= ~(BRKINT);
	raw.c_lflag &= ~(ISIG); /* Ignore Ctrl-C and Ctrl-Z */
	write(1, SL(CSI_CURSOR_HIDE));
	return (tcsetattr(0, TCSAFLUSH, &raw) ? errno : 0);
}

int unraw(struct termios* before)
{
	if (tcsetattr(0, TCSAFLUSH, before)) return errno;
	write(1, SL(CSI_CURSOR_SHOW));
	return 0;
}

ssize_t tread(int fd, void *buf, size_t num, suseconds_t to)
{
	fd_set s;
	ssize_t n;
	int e;
	struct timeval t;

	FD_ZERO(&s);
	FD_SET(fd, &s);

	do {
		errno = 0;
		t.tv_sec = 0;
		t.tv_usec = to;
		n = select(fd+1, &s, 0, 0, &t);
	} while (n == -1 && ((e = errno), e == EINTR));
	if (n == -1 || n == 0) {
		goto exit;
	}
	do {
		errno = 0;
		n = read(fd, buf, num);
	} while (n == -1 && ((e = errno), e == EAGAIN || e == EINTR));
exit:
	FD_ZERO(&s);
	return n;
}

static _Bool in(char c, char *S)
{
	while (*S && c != *S) S++;
	return *S;
}

static char Tread1(char *c)
{
	if (1 == tread(0, c, 1, 125000)) return *c;
	return 0;
}

static char read1(char *c)
{
	if (1 == read(0, c, 1)) return *c;
	return 0;
}

special_type which_key(char *seq)
{
	int i = 0;
	while (seq2special[i].seq && seq2special[i].t != S_NONE) {
		if (!strcmp(seq2special[i].seq, seq)) {
			return seq2special[i].t;
		}
		i++;
	}
	return IT_NONE;
}

/* TODO timeout */
input get_input(void)
{
	input i;
	int utflen, b;
	char seq[8] = { 0 };

	memset(&i, 0, sizeof(i));
	if (read1(seq) == '\x1b') {
		if (Tread1(seq+1) && in(seq[1], "[O")) {
			if (read1(seq+2) && in(seq[2], "0123456789")) {
				read1(seq+3);
			}
		}
		i.t = IT_SPEC;
		i.s = which_key(seq);
	}
	else if (seq[0] == 0x7f) {
		i.t = IT_SPEC;
#if defined(__linux__) || defined(__linux) || defined(linux)
		i.s = S_BACKSPACE;
#else
		i.s = S_DELETE;
#endif
	}
	else if (!(seq[0] & 0x60)) {
		i.t = IT_CTRL;
		i.utf[0] = seq[0] | 0x40;
	}
	else if ((utflen = utf8_b2len(seq))) {
		i.t = IT_UTF8;
		i.utf[0] = seq[0];
		for (b = 1; b < utflen; ++b) {
			if (!read1(i.utf+b)) {
				i.t = IT_NONE;
				memset(i.utf, 0, 5);
				return i;
			}
		}
		for (; b < 5; ++b) {
			i.utf[b] = 0;
		}
	}
	return i;
}
