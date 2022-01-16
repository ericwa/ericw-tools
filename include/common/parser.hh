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

#pragma once

#include <utility>

enum : int32_t
{
    PARSE_NORMAL = 0,
    PARSE_SAMELINE = 1, /* Expect the next token the current line */
    PARSE_COMMENT = 2, /* If a // comment is next token, return it */
    PARSE_OPTIONAL = 4, /* Return next token on same line, or false if EOL */
    PARSE_PEEK = 8 /* Don't change parser state */
};

using parseflags = int32_t;

template<typename... T>
inline auto untie(const std::tuple<T...> &tuple)
{
    return std::tuple<typename std::remove_reference<T>::type...>(tuple);
}

struct parser_t
{
    const char *pos;
    const char *end;
    uint32_t linenum = 1;
    std::string token;

    // base constructor; accept raw start & length
    parser_t(const void *start, size_t length) : pos(reinterpret_cast<const char *>(start)), end(reinterpret_cast<const char *>(start) + length) { }

    // pull from string_view; note that the string view must live for the entire
    // duration of the parser's life time
    parser_t(const std::string_view &view) : parser_t(&view.front(), view.size()) { }

    // pull from C string; made explicit because this is error-prone
    explicit parser_t(const char *str) : parser_t(str, strlen(str)) { }

    bool parse_token(parseflags flags = PARSE_NORMAL);

    auto state() { return std::tie(pos, linenum); }

    bool at_end() const { return pos >= end; };
};
