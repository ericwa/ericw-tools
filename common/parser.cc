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

#include <common/log.hh>
#include <common/parser.hh>

// parser_source_location

parser_source_location::parser_source_location() = default;
parser_source_location::parser_source_location(const std::string &source)
    : source_name(std::make_unique<std::string>(source))
{
}
parser_source_location::parser_source_location(const char *source)
    : source_name(std::make_unique<std::string>(source))
{
}
parser_source_location::parser_source_location(const std::string &source, size_t line)
    : source_name(std::make_unique<std::string>(source)),
      line_number(line)
{
}
parser_source_location::parser_source_location(const char *source, size_t line)
    : source_name(std::make_unique<std::string>(source)),
      line_number(line)
{
}

parser_source_location::operator bool() const
{
    return source_name != nullptr;
}

parser_source_location parser_source_location::on_line(size_t new_line) const
{
    parser_source_location loc(*this);
    loc.line_number = new_line;
    return loc;
}

// parser_t

parser_t::parser_t(const void *start, size_t length, parser_source_location base_location)
    : parser_base_t(base_location.on_line(1)),
      pos(reinterpret_cast<const char *>(start)),
      end(reinterpret_cast<const char *>(start) + length)
{
}

parser_t::parser_t(std::string_view view, parser_source_location base_location)
    : parser_t(view.data(), view.size(), base_location)
{
}

parser_t::parser_t(const fs::data &data, parser_source_location base_location)
    : parser_t(data.value().data(), data.value().size(), base_location)
{
}

parser_t::parser_t(const char *str, parser_source_location base_location)
    : parser_t(str, strlen(str), base_location)
{
}

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
                FError("{}: Line is incomplete", location);
            return false;
        }
        if (*pos == '\n') {
            if (flags & PARSE_OPTIONAL)
                return false;
            if (flags & PARSE_SAMELINE)
                FError("{}: Line is incomplete", location);
            location.line_number.value()++;
        }
        pos++;
    }

    /* comment field */
    if ((pos[0] == '/' && pos[1] == '/') || pos[0] == ';') { // quark writes ; comments in q2 maps
        if (flags & PARSE_COMMENT) {
            while (*pos && *pos != '\n') {
                *token_p++ = *pos++;
            }
            goto out;
        }
        if (flags & PARSE_OPTIONAL)
            return false;
        if (flags & PARSE_SAMELINE)
            FError("{}: Line is incomplete", location);
        while (*pos++ != '\n') {
            if (!*pos) {
                if (flags & PARSE_SAMELINE)
                    FError("{}: Line is incomplete", location);
                return false;
            }
        }
        location.line_number.value()++; // count the \n the preceding while() loop just consumed
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
                FError("{}: EOF inside quoted token", location);
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
                            logging::print("WARNING: {}: escaped double-quote at end of string\n", location);
                        } else {
                            *token_p++ = *pos++;
                        }
                        break;
                    default:
                        logging::print("WARNING: {}: Unrecognised string escape - \\{}\n", location, pos[1]);
                        break;
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

parser_t::state_type parser_t::state()
{
    return state_type(pos, location);
}

bool parser_t::at_end() const
{
    return pos >= end;
}

void parser_t::push_state()
{
    _states.push_back(state());
}

void parser_t::pop_state()
{
    state() = _states.back();
    _states.pop_back();
}

// token_parser_t

token_parser_t::token_parser_t(int argc, const char **args, parser_source_location base_location)
    : parser_base_t(base_location),
      tokens(args, args + argc)
{
}

token_parser_t::state_type token_parser_t::state()
{
    return state_type(cur);
}

bool token_parser_t::parse_token(parseflags flags)
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

bool token_parser_t::at_end() const
{
    return cur >= tokens.size();
}

void token_parser_t::push_state()
{
    _states.push_back(state());
}

void token_parser_t::pop_state()
{
    state() = _states.back();
    _states.pop_back();
}
