/*  Copyright (C) 1996-1997  Id Software, Inc.

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

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>
#include <string_view>

#include "qvec.hh"

struct parser_t;
struct parser_base_t;

using keyvalue_t = std::pair<std::string, std::string>;
using keyvalues_t = std::vector<keyvalue_t>;

class entdict_t
{
    keyvalues_t keyvalues;

public:
    entdict_t(std::initializer_list<keyvalue_t> l);
    entdict_t();
    entdict_t(parser_base_t &parser);

    const std::string &get(std::string_view key) const;
    double get_float(std::string_view key) const;
    int32_t get_int(std::string_view key) const;
    // returns number of vector components read
    int32_t get_vector(std::string_view key, qvec3f &out) const;
    void set(std::string_view key, std::string_view value);
    void remove(std::string_view key);
    void rename(std::string_view from, std::string_view to);

    keyvalues_t::iterator find(std::string_view key);
    keyvalues_t::const_iterator find(std::string_view key) const;

    bool has(std::string_view key) const;

    inline keyvalues_t::const_iterator begin() const { return keyvalues.begin(); }
    inline keyvalues_t::const_iterator end() const { return keyvalues.end(); }

    inline keyvalues_t::iterator begin() { return keyvalues.begin(); }
    inline keyvalues_t::iterator end() { return keyvalues.end(); }

    inline size_t size() { return keyvalues.size(); }

    // parse dictionary out of the input parser.
    // the parser must be at a position where { is
    // the next token parsed.
    void parse(parser_base_t &parser);

    // order-sensitive
    auto operator<=>(const entdict_t &other) const = default;
};

void EntData_ParseInto(parser_t &parser, std::vector<entdict_t> &vector);

/*
 * ==================
 * EntData_Parse
 * ==================
 */
std::vector<entdict_t> EntData_Parse(parser_t &parser);
struct mbsp_t;
std::vector<entdict_t> EntData_Parse(const mbsp_t &bsp);

std::string EntData_Write(const std::vector<entdict_t> &ents);
