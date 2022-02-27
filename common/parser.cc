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
#include <common/parser.hh>

bool parser_t::parse_token(parseflags flags)
{
    /* for peek, we'll do a backup/restore. */
    if (flags & PARSE_PEEK) {
        auto restore = untie(state());
        bool result = parse_token(flags & ~PARSE_PEEK);
        state() = restore;
        return result;
    }

    was_quoted = false;
    token.clear();
    auto token_p = std::back_inserter(token);

skipspace:
    /* skip space */
    while (at_end() || *pos <= 32) {
        if (at_end() || !*pos) {
            if (flags & PARSE_OPTIONAL)
                return false;
            if (flags & PARSE_SAMELINE)
                FError("line {}: Line is incomplete", linenum);
            return false;
        }
        if (*pos == '\n') {
            if (flags & PARSE_OPTIONAL)
                return false;
            if (flags & PARSE_SAMELINE)
                FError("line {}: Line is incomplete", linenum);
            linenum++;
        }
        pos++;
    }

    /* comment field */
    if (pos[0] == '/' && pos[1] == '/') {
        if (flags & PARSE_COMMENT) {
            while (*pos && *pos != '\n') {
                *token_p++ = *pos++;
            }
            goto out;
        }
        if (flags & PARSE_OPTIONAL)
            return false;
        if (flags & PARSE_SAMELINE)
            FError("line {}: Line is incomplete", linenum);
        while (*pos++ != '\n') {
            if (!*pos) {
                if (flags & PARSE_SAMELINE)
                    FError("line {}: Line is incomplete", linenum);
                return false;
            }
        }
        linenum++; // count the \n the preceding while() loop just consumed
        goto skipspace;
    }
    if (flags & PARSE_COMMENT)
        return false;

    /* copy token */

    if (*pos == '"') {
        was_quoted = true;
        pos++;
        while (*pos != '"') {
            if (!*pos)
                FError("line {}: EOF inside quoted token", linenum);
            if (*pos == '\\') {
                // small note. the vanilla quake engine just parses the "foo" stuff then goes and looks for \n
                // explicitly within strings. this means ONLY \n works, and double-quotes cannot be used either in maps
                // _NOR SAVED GAMES_. certain editors can write "wad" "c:\foo\" which is completely fucked. so lets try
                // to prevent more brokenness and encourage map editors to switch to using sane wad keys.
                switch (pos[1]) {
                    case 'n':
                    case '\'':
                    case 'r':
                    case 't':
                    case '\\':
                    case 'b': // ericw-tools extension, parsed by light, used to toggle bold text
                              // regular two-char escapes
                        *token_p++ = *pos++;
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
                    case '9': // too lazy to validate these. doesn't break stuff.
                        break;
                    case '\"':
                        if (pos[2] == '\r' || pos[2] == '\n') {
                            LogPrint("WARNING: line {}: escaped double-quote at end of string\n", linenum);
                        } else {
                            *token_p++ = *pos++;
                        }
                        break;
                    default: LogPrint("WARNING: line {}: Unrecognised string escape - \\{}\n", linenum, pos[1]); break;
                }
            }
            *token_p++ = *pos++;
        }
        pos++;
    } else {
        while (*pos > 32) {
            *token_p++ = *pos++;
        }
    }

out:
    return true;
}
