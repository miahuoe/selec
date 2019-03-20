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

#include "utf8.h"

int utf8_b2len(char* b)
{
	static const uint8_t t[32] = {
		1, 1, 1, 1, 1, 1, 1, 1, //00000xxx - 00111xxx
		1, 1, 1, 1, 1, 1, 1, 1, //01000xxx - 01111xxx
		0, 0, 0, 0, 0, 0, 0, 0, //10000xxx - 10111xxx
		2, 2, 2, 2, 3, 3, 4, 0  //11000xxx - 11111xxx
	};
	return t[(uint8_t)(*b) >> 3];
}

int utf8_cp2len(int cp)
{
	if (cp < 0x80) return 1;
	if (cp < 0x0800) return 2;
	if (cp < 0x010000) return 3;
	if (cp < 0x200000) return 4;
	return 0;
}

int utf8_dechar(int* cp, char* b)
{
	static const uint8_t p[5] = { 0x00, 0x7f, 0x1f, 0x0f, 0x07 };
	int nb, i;

	if (!*b || !(nb = utf8_b2len(b))) {
		return 0;
	}

	*cp = b[0] & p[nb];
	for (i = 1; i < nb; ++i) {
		*cp <<= 6;
		*cp |= b[i] & 0x3f;
	}
	return nb;
}

int utf8_enchar(int cp, char* b)
{
	static const uint8_t p[] = { 0x00, 0x7f, 0x1f, 0x0f, 0x07 };
	static const uint8_t o[] = { 0x00, 0x00, 0xc0, 0xe0, 0xf0 };
	int nb, i;

	nb = utf8_cp2len(cp);

	for (i = nb-1; i; --i, cp >>= 6) {
		b[i] = 0x80 | (cp & 0x3f);
	}
	b[0] = o[nb] | (cp & p[nb]);
	return nb;
}

static _Bool cp_in(const int r[][2], size_t Z, int cp)
{
	if (cp < r[0][0] || r[Z][1] < cp) return 0;
	size_t A = 0, I;
	while (A <= Z) {
		I = (A+Z)/2;
		if (cp < r[I][0]) {
			Z = I-1;
		}
		else if (cp > r[I][1]) {
			A = I+1;
		}
		else {
			return 1;
		}
	}
	return 0;
}

int utf8_cp2w(int cp)
{
	if (cp < 0x20 || cp == 0x7f) return 0;
	if (cp < 0x7f) return 1;
	if (cp_in(zero_width, zero_width_len-1, cp)) return 0;
	if (cp_in(double_width, double_width_len-1, cp)) return 2;
	return 1;
}

int utf8_strwidth(char *B)
{
	int w = 0, b, cp;
	while ((b = utf8_dechar(&cp, B))) {
		w += utf8_cp2w(cp);
		B += b;
	}
	return w;
}
