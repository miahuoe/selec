#ifndef UTF8_H
#define UTF8_H

#include <stdint.h>

int utf8_b2len(char*);
int utf8_cp2len(int);
int utf8_dechar(int*, char*);
int utf8_enchar(int, char*);

#endif
