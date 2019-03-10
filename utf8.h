#ifndef UTF8_H
#define UTF8_H

#include <stdint.h>
#include <stddef.h>
#include "widechars.h"

int utf8_b2len(char*);
int utf8_cp2len(int);
int utf8_dechar(int*, char*);
int utf8_enchar(int, char*);
int utf8_cp2w(int cp);
int utf8_strwidth(char*);

#endif
