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

#include "edit.h"
#include <stdio.h>

void edit_init(edit *E, char *buf, size_t bufs)
{
	E->cur_x = E->cur_y = 0;
	E->bufs = bufs;
	E->begin = E->cur = E->end = buf;
	*E->begin = 0;
}

static void update_cursor_pos(edit *E)
{
	int b, cp, w = 0;
	char *B;

	B = E->begin;
	while (B != E->cur && (b = utf8_dechar(&cp, B))) {
		w += utf8_cp2w(cp);
		B += b;
	}
	E->cur_x = w;
	E->cur_y = 1;
}

void edit_move(edit *E, int n)
{
	int b, cp;
	if (n > 0) {
		while (n-- && E->cur != E->end && (b = utf8_dechar(&cp, E->cur))) {
			E->cur += b;
		}
	}
	else if (n < 0) {
		while (n++ && E->cur != E->begin) {
			do {
				E->cur--;
			} while (E->cur != E->begin && !(b = utf8_b2len(E->cur)));
		}
	}
	update_cursor_pos(E);
}

void edit_insert(edit *E, char *s, size_t sl)
{
	char *src, *dst;
	int i;

	if ((E->end-E->begin)+sl >= E->bufs) {
		return;
	}

	src = E->end-1;
	dst = E->end+sl-1;
	for (i = 0; i < E->end-E->cur; i++) {
		*dst = *src;
		dst--;
		src--;
	}
	memcpy(E->cur, s, sl);
	E->cur += sl;
	E->end += sl;
	*E->end = 0;
	update_cursor_pos(E);
}

void edit_delete(edit *E, int n)
{
	char *A, *B;
	int b, cp;

	A = B = E->cur;
	if (n > 0) {
		if (B == E->end) return;
		while (n-- && B != E->end && (b = utf8_dechar(&cp, B))) {
			B += b;
		}
	}
	else if (n < 0) {
		if (A == E->begin) return;
		while (n++ && A != E->begin) {
			do {
				A--;
			} while (A != E->begin && !(b = utf8_b2len(A)));
		}
	}
	memmove(A, B, E->end-B);
	E->end -= B-A;
	E->cur = A;
	*E->end = 0;
	update_cursor_pos(E);
}
