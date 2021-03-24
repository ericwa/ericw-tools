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
#include <map>
#include <string>
#include <utility>
#include <vector>

using keyvalue_t = std::pair<std::string, std::string>;
using keyvalues_t = std::vector<keyvalue_t>;

class entdict_t {
    keyvalues_t keyvalues;
public:
    entdict_t(std::initializer_list<keyvalue_t> l);
    entdict_t();

    const std::string& get(const std::string& key) const;
    void set(const std::string& key, const std::string& value);
    void remove(const std::string& key);

    keyvalues_t::iterator find(std::string_view key);
    keyvalues_t::const_iterator find(std::string_view key) const;

    keyvalues_t::const_iterator begin() const;
    keyvalues_t::const_iterator end() const;

    keyvalues_t::iterator begin();
    keyvalues_t::iterator end();
};

std::vector<entdict_t> EntData_Parse(const char *entdata);
std::string EntData_Write(const std::vector<entdict_t> &ents);
std::string EntDict_StringForKey(const entdict_t &dict, const std::string key);
float EntDict_FloatForKey(const entdict_t &dict, const std::string key);
void EntDict_RemoveValueForKey(entdict_t &dict, const std::string &key);
void EntDict_RenameKey(entdict_t &dict, const std::string &from, const std::string &to);
