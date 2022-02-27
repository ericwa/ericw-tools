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
#include <vector>
#include <string_view>

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
constexpr auto untie(const std::tuple<T...> &tuple)
{
    return std::tuple<typename std::remove_reference<T>::type...>(tuple);
}

template<typename T>
using untied_t = decltype(untie(std::declval<T>()));

struct parser_base_t
{
    std::string token;
    bool was_quoted = false; // whether the current token was from a quoted string or not

    virtual bool parse_token(parseflags flags = PARSE_NORMAL) = 0;

    virtual bool at_end() const = 0;

    virtual void push_state() = 0;

    virtual void pop_state() = 0;
};

#include <utility>

// a parser that works on a single, contiguous string
struct parser_t : parser_base_t
{
    const char *pos;
    const char *end;
    uint32_t linenum = 1;

    // base constructor; accept raw start & length
    inline parser_t(const void *start, size_t length)
        : pos(reinterpret_cast<const char *>(start)), end(reinterpret_cast<const char *>(start) + length)
    {
    }

    // pull from string_view; note that the string view must live for the entire
    // duration of the parser's life time
    inline parser_t(const std::string_view &view) : parser_t(&view.front(), view.size()) { }

    // pull from C string; made explicit because this is error-prone
    explicit parser_t(const char *str) : parser_t(str, strlen(str)) { }

    bool parse_token(parseflags flags = PARSE_NORMAL) override;

    using state_type = decltype(std::tie(pos, linenum));

    constexpr state_type state() { return state_type(pos, linenum); }

    bool at_end() const override { return pos >= end; }

private:
    std::vector<untied_t<state_type>> _states;

public:
    virtual void push_state() override { _states.push_back(state()); }

    virtual void pop_state() override
    {
        state() = _states.back();
        _states.pop_back();
    }
};

// a parser that works on a list of tokens
struct token_parser_t : parser_base_t
{
    std::vector<std::string_view> tokens;
    size_t cur = 0;

    inline token_parser_t(int argc, const char **args) : tokens(args, args + argc) { }

    using state_type = decltype(std::tie(cur));

    constexpr state_type state() { return state_type(cur); }

    inline bool parse_token(parseflags flags = PARSE_NORMAL) override
    {
        /* for peek, we'll do a backup/restore. */
        if (flags & PARSE_PEEK) {
            auto restore = untie(state());
            bool result = parse_token(flags & ~PARSE_PEEK);
            state() = restore;
            return result;
        }

        token.clear();
        was_quoted = false;

        if (at_end()) {
            return false;
        }

        token = tokens[cur++];

        was_quoted = std::any_of(token.begin(), token.end(), isspace);

        return true;
    }

    bool at_end() const override { return cur >= tokens.size(); }

private:
    std::vector<untied_t<state_type>> _states;

public:
    virtual void push_state() override { _states.push_back(state()); }

    virtual void pop_state() override
    {
        state() = _states.back();
        _states.pop_back();
    }
};