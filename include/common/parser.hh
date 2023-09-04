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
#include "fs.hh"

enum : int32_t
{
    PARSE_NORMAL = 0,
    PARSE_SAMELINE = 1, /* Expect the next token the current line */
    PARSE_COMMENT = 2, /* If a // comment is next token, return it */
    PARSE_OPTIONAL = 4, /* Return next token on same line, or false if EOL */
    PARSE_PEEK = 8 /* Don't change parser state */
};

using parseflags = int32_t;

// kind of a parallel to std::source_location in C++20
// but this represents a location in a parser.
struct parser_source_location
{
    // the source name of this location; may be a .map file path,
    // or some other string that describes where this location came
    // to be. note that because the locations only live for the lifetime
    // of the object it is belonging to, whatever this string
    // points to must out-live the object.
    std::shared_ptr<std::string> source_name = nullptr;

    // the line number that this location is associated to, if any. Synthetic
    // locations may not necessarily have an associated line number.
    std::optional<size_t> line_number = std::nullopt;

    // reference to a location of the object that derived us. this is mainly
    // for synthetic locations; ie a bspbrush_t's sides aren't themselves generated
    // by a source or line, but they are derived from a mapbrush_t which does have
    // a location. The object it points to must outlive this object. this is mainly
    // for debugging.
    const parser_source_location *derivative = nullptr;

    parser_source_location();
    parser_source_location(const std::string &source);
    parser_source_location(const char *source);
    parser_source_location(const std::string &source, size_t line);
    parser_source_location(const char *source, size_t line);

    // check if this source location is valid
    explicit operator bool() const;

    // return a modified source location with only the line changed
    parser_source_location on_line(size_t new_line) const;

    // return a new, constructed source location derived from this one
    template<typename... Args>
    inline parser_source_location derive(Args &&...args)
    {
        parser_source_location loc(std::forward<Args>(args)...);
        loc.derivative = this;
        return loc;
    }

    // if we update to C++20 we could use this to track where location objects come from:
    // std::source_location created_location;
};

// FMT support
template<>
struct fmt::formatter<parser_source_location>
{
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(const parser_source_location &v, FormatContext &ctx) -> decltype(ctx.out())
    {
        if (v.source_name) {
            fmt::format_to(ctx.out(), "{}", *v.source_name.get());
        } else {
            fmt::format_to(ctx.out(), "unknown/unset location");
        }

        if (v.line_number.has_value()) {
            fmt::format_to(ctx.out(), "[line {}]", v.line_number.value());
        }

        return ctx.out();
    }
};

template<typename... T>
constexpr auto untie(const std::tuple<T...> &tuple)
{
    return std::tuple<typename std::remove_reference<T>::type...>(tuple);
}

template<typename T>
using untied_t = decltype(untie(std::declval<T>()));

struct parser_base_t
{
    std::string token; // the last token parsed by parse_token
    bool was_quoted = false; // whether the current token was from a quoted string or not
    parser_source_location location; // parse location, if any

    inline parser_base_t(parser_source_location base_location)
        : location(base_location)
    {
    }

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

    // base constructor; accept raw start & length
    parser_t(const void *start, size_t length, parser_source_location base_location);

    // pull from string_view; note that the string view must live for the entire
    // duration of the parser's life time
    parser_t(const std::string_view &view, parser_source_location base_location);

    // pull from fs::data; note that the data must live for the entire
    // duration of the parser's life time, and must has_value()
    parser_t(const fs::data &data, parser_source_location base_location);

    // pull from C string; made explicit because this is error-prone
    explicit parser_t(const char *str, parser_source_location base_location);

    bool parse_token(parseflags flags = PARSE_NORMAL) override;

    using state_type = decltype(std::tie(pos, location));

    state_type state();

    bool at_end() const override;

private:
    std::vector<untied_t<state_type>> _states;

public:
    void push_state() override;
    void pop_state() override;
};

// a parser that works on a list of tokens
struct token_parser_t : parser_base_t
{
    std::vector<std::string_view> tokens;
    size_t cur = 0;

    token_parser_t(int argc, const char **args, parser_source_location base_location);

    using state_type = decltype(std::tie(cur));

    state_type state();

    bool parse_token(parseflags flags = PARSE_NORMAL) override;
    bool at_end() const override;

private:
    std::vector<untied_t<state_type>> _states;

public:
    void push_state() override;
    void pop_state() override;
};