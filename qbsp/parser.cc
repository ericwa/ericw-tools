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

#include <qbsp/qbsp.hh>
#include <qbsp/parser.hh>

void
ParserInit(parser_t *p, const char *data)
{
    p->linenum = 1;
    p->pos = data;
    p->unget = false;
}


bool
ParseToken(parser_t *p, int flags)
{
    char *token_p;

    /* is a token already waiting? */
    if (p->unget) {
        p->unget = false;
        return true;
    }

 skipspace:
    /* skip space */
    while (*p->pos <= 32) {
        if (!*p->pos) {
            if (flags & PARSE_OPTIONAL)
                return false;
            if (flags & PARSE_SAMELINE)
                Error("line %d: Line is incomplete", p->linenum);
            return false;
        }
        if (*p->pos == '\n') {
            if (flags & PARSE_OPTIONAL)
                return false;
            if (flags & PARSE_SAMELINE)
                Error("line %d: Line is incomplete", p->linenum);
            p->linenum++;
        }
        p->pos++;
   }

    /* comment field */
    if (p->pos[0] == '/' && p->pos[1] == '/') {
        if (flags & PARSE_COMMENT) {
            token_p = p->token;
            while (*p->pos && *p->pos != '\n') {
                *token_p++ = *p->pos++;
                if (token_p > &p->token[MAXTOKEN - 1])
                    Error("line %d: Token too large", p->linenum);
            }
            goto out;
        }
        if (flags & PARSE_OPTIONAL)
            return false;
        if (flags & PARSE_SAMELINE)
            Error("line %d: Line is incomplete", p->linenum);
        while (*p->pos++ != '\n') {
            if (!*p->pos) {
                if (flags & PARSE_SAMELINE)
                    Error("line %d: Line is incomplete", p->linenum);
                return false;
            }
        }
        p->linenum++; // count the \n the preceding while() loop just consumed
        goto skipspace;
    }
    if (flags & PARSE_COMMENT)
        return false;

    /* copy token */
    token_p = p->token;

    if (*p->pos == '"') {
        p->pos++;
        while (*p->pos != '"') {
            if (!*p->pos)
                Error("line %d: EOF inside quoted token", p->linenum);
            if (*p->pos == '\\')
            {
                //small note. the vanilla quake engine just parses the "foo" stuff then goes and looks for \n explicitly within strings.
                //this means ONLY \n works, and double-quotes cannot be used either in maps _NOR SAVED GAMES_.
                //certain editors can write "wad" "c:\foo\" which is completely fucked.
                //so lets try to prevent more brokenness and encourage map editors to switch to using sane wad keys.
                switch(p->pos[1])
                {
                case 'n':
                case '\'':
                case 'r':
                case 't':
                case '\\':
                case 'b': // ericw-tools extension, parsed by light, used to toggle bold text
                        //regular two-char escapes
                    *token_p++ = *p->pos++;
                    if (token_p > &p->token[MAXTOKEN - 1])
                                Error("line %d: Token too large", p->linenum);          
                        break;
                case 'x':
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':       //too lazy to validate these. doesn't break stuff.
                        break;
                case '\"':
                            *token_p++ = *p->pos++;
                            if (token_p > &p->token[MAXTOKEN - 1])
                                Error("line %d: Token too large", p->linenum);          
                        if (p->pos[1] == '\r' || p->pos[1] == '\n')
                                Error("line %d: escaped double-quote at end of string", p->linenum);            
                        break;
                default:
                        Message(msgLiteral, "line %d: Unrecognised string escape - \\%c\n", p->linenum, p->pos[1]);
                        break;
                }
            }
            *token_p++ = *p->pos++;
            if (token_p > &p->token[MAXTOKEN - 1])
                Error("line %d: Token too large", p->linenum);
        }
        p->pos++;
    } else {
        while (*p->pos > 32) {
            *token_p++ = *p->pos++;
            if (token_p > &p->token[MAXTOKEN - 1])
                Error("line %d: Token too large", p->linenum);
        }
    }
 out:
    *token_p = 0;

    return true;
}
