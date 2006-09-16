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

#include "qbsp.h"
#include "parser.h"

int linenum;
char token[MAXTOKEN];

static bool unget;
static char *script;


void
ParserInit(char *data)
{
    linenum = 1;
    script = data;
    unget = false;
}


bool
ParseToken(int flags)
{
    char *token_p;

    /* is a token already waiting? */
    if (unget) {
	unget = false;
	return true;
    }

 skipspace:
    /* skip space */
    while (*script <= 32) {
	if (!*script) {
	    if (flags & PARSE_SAMELINE)
		Message(msgError, errLineIncomplete, linenum);
	    return false;
	}
	if (*script++ == '\n') {
	    if (flags & PARSE_SAMELINE)
		Message(msgError, errLineIncomplete, linenum);
	    linenum++;
	}
    }

    /* comment field */
    if (script[0] == '/' && script[1] == '/') {
	if (flags & PARSE_COMMENT) {
	    token_p = token;
	    while (*script && *script != '\n') {
		*token_p++ = *script++;
		if (token_p > &token[MAXTOKEN - 1])
		    Message(msgError, errTokenTooLarge, linenum);
	    }
	    goto out;
	}
	if (flags & PARSE_SAMELINE)
	    Message(msgError, errLineIncomplete, linenum);
	while (*script++ != '\n')
	    if (!*script) {
		if (flags & PARSE_SAMELINE)
		    Message(msgError, errLineIncomplete, linenum);
		return false;
	    }
	goto skipspace;
    }
    if (flags & PARSE_COMMENT)
	return false;

    /* copy token */
    token_p = token;

    if (*script == '"') {
	script++;
	while (*script != '"') {
	    if (!*script)
		Message(msgError, errEOFInQuotes, linenum);
	    *token_p++ = *script++;
	    if (token_p > &token[MAXTOKEN - 1])
		Message(msgError, errTokenTooLarge, linenum);
	}
	script++;
    } else
	while (*script > 32) {
	    *token_p++ = *script++;
	    if (token_p > &token[MAXTOKEN - 1])
		Message(msgError, errTokenTooLarge, linenum);
	}
 out:
    *token_p = 0;

    return true;
}
