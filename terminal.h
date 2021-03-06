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

#ifndef TERMINAL_H
#define TERMINAL_H

#ifndef _DEFAULT_SOURCE
	#define _DEFAULT_SOURCE
#endif

#include <sys/ioctl.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>

#include "utf8.h"

#define CSI_CLEAR_ALL "\x1b[2J"
#define CSI_CLEAR_LINE "\x1b[K"
#define CSI_CURSOR_TOP_LEFT "\x1b[H"
#define CSI_CURSOR_SHOW "\x1b[?25h"
#define CSI_CURSOR_HIDE "\x1b[?25l"
#define CSI_SCREEN_ALTERNATIVE "\x1b[?47h"
#define CSI_SCREEN_NORMAL "\x1b[?47l"
#define CSI_CURSOR_HIDE_TOP_LEFT "\x1b[?25l\x1b[H"

#define SL(CSI) (CSI), (sizeof((CSI))-1)

typedef enum {
	IT_NONE = 0,
	IT_UTF8, // utf will contain bytes of the glyph
	IT_CTRL, // utf[0] will contain character
	IT_SPEC,
	IT_EOF
} input_type;

typedef enum {
	S_NONE = 0,
	S_ARROW_UP,
	S_ARROW_DOWN,
	S_ARROW_RIGHT,
	S_ARROW_LEFT,
	S_HOME,
	S_END,
	S_PAGE_UP,
	S_PAGE_DOWN,
	S_INSERT,
	S_BACKSPACE,
	S_DELETE,
	S_ESCAPE,
} special_type;

typedef struct {
	input_type t : 8;
	special_type s : 8;
	char utf[4];
} input;

typedef struct {
	char* seq;
	special_type t : 8;
} s2s;

void set_cur_pos(int, int, int);

void get_cur_pos(int, int*, int*);

void get_win_dims(int, int*, int*);

int move_cursor(int, int, int);

input get_input(int);

special_type which_key(char*);

int raw(struct termios*, int);

int unraw(struct termios*, int);

#endif
