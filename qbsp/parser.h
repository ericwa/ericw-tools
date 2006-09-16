/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>

#define MAXTOKEN 256

extern int linenum;
extern char token[MAXTOKEN];

enum parseflags {
    PARSE_NORMAL   = 0,
    PARSE_SAMELINE = 1, /* The next token must be on the current line */
    PARSE_COMMENT  = 2  /* Return a // comment as the next token */
};

bool ParseToken(int flags);
void ParserInit(char *data);

#endif /* PARSER_H */
