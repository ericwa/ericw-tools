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

#define MAXTOKEN 1024

typedef enum parseflags {
    PARSE_NORMAL   = 0,
    PARSE_SAMELINE = 1, /* Expect the next token the current line */
    PARSE_COMMENT  = 2, /* If a // comment is next token, return it */
    PARSE_OPTIONAL = 4, /* Return next token on same line, or false if EOL */
} parseflags_t;

typedef struct parser {
    bool unget;
    const char *pos;
    int linenum;
    char token[MAXTOKEN];
} parser_t;

bool ParseToken(parser_t *p, parseflags_t flags);
void ParserInit(parser_t *p, const char *data);

#endif /* PARSER_H */
