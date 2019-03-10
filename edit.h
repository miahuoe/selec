#ifndef EDIT_H
#define EDIT_H

#include "utf8.h"
#include <string.h>

typedef struct edit {
	int cur_x, cur_y;
	size_t bufs;
	char *begin,
	     *cur,
	     *end;
} edit;

void edit_init(edit*, char*, size_t);

void edit_move(edit*, int);

void edit_insert(edit*, char*, size_t);

void edit_delete(edit*, int);

#endif
