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

#include <common/entdata.h>

#include <cstring>
#include <sstream>
#include <common/cmdlib.hh>

#include <light/light.hh>
#include <light/entities.hh>
#include <light/ltface.hh>
#include <common/bsputils.hh>
#include <common/parser.hh>

#include <fmt/ostream.h>

entdict_t::entdict_t(std::initializer_list<keyvalue_t> l) : keyvalues(l) { }

entdict_t::entdict_t() = default;

const std::string &entdict_t::get(const std::string &key) const
{
    if (auto it = find(key); it != keyvalues.end()) {
        return it->second;
    }

    static std::string empty;
    return empty;
}

void entdict_t::set(const std::string &key, const std::string &value)
{
    // search for existing key to update
    if (auto it = find(key); it != keyvalues.end()) {
        // found existing key
        it->second = value;
        return;
    }

    // no existing key; add new
    keyvalues.emplace_back(key, value);
}

void entdict_t::remove(const std::string &key)
{
    if (auto it = find(key); it != keyvalues.end()) {
        keyvalues.erase(it);
    }
}

keyvalues_t::iterator entdict_t::find(std::string_view key)
{
    auto existingIt = keyvalues.end();
    for (auto it = keyvalues.begin(); it != keyvalues.end(); ++it) {
        if (key == it->first) {
            existingIt = it;
            break;
        }
    }
    return existingIt;
}

keyvalues_t::const_iterator entdict_t::find(std::string_view key) const
{
    auto existingIt = keyvalues.end();
    for (auto it = keyvalues.begin(); it != keyvalues.end(); ++it) {
        if (key == it->first) {
            existingIt = it;
            break;
        }
    }
    return existingIt;
}

keyvalues_t::const_iterator entdict_t::begin() const
{
    return keyvalues.begin();
}

keyvalues_t::const_iterator entdict_t::end() const
{
    return keyvalues.end();
}

keyvalues_t::iterator entdict_t::begin()
{
    return keyvalues.begin();
}

keyvalues_t::iterator entdict_t::end()
{
    return keyvalues.end();
}

/*
 * ==================
 * EntData_Parse
 * ==================
 */
std::vector<entdict_t> EntData_Parse(const std::string &entdata)
{
    std::vector<entdict_t> result;
    parser_t parser(entdata.data());

    /* go through all the entities */
    while (1) {
        /* parse the opening brace */
        if (!parser.parse_token())
            break;
        if (parser.token != "{")
            FError("found {} when expecting {", parser.token);

        /* Allocate a new entity */
        entdict_t &entity = result.emplace_back();

        /* go through all the keys in this entity */
        while (1) {
            /* parse key */
            if (!parser.parse_token())
                FError("EOF without closing brace");

            if (parser.token == "}")
                break;
            if (parser.token.length() > MAX_ENT_KEY - 1)
                FError("Key length > {}: '{}'", MAX_ENT_KEY - 1, parser.token);

            std::string keystr = parser.token;

            /* parse value */
            if (!parser.parse_token())
                FError("EOF without closing brace");

            if (parser.token == "}")
                FError("closing brace without data");
            if (parser.token.length() > MAX_ENT_VALUE - 1)
                FError("Value length > {}", MAX_ENT_VALUE - 1);

            entity.set(keystr, parser.token);
        }
    }

    return result;
}

/*
 * ================
 * EntData_Write
 * ================
 */
std::string EntData_Write(const std::vector<entdict_t> &ents)
{
    std::string out;
    for (const auto &ent : ents) {
        out += "{\n";
        for (const auto &epair : ent) {
            fmt::format_to(std::back_inserter(out), "\"{}\" \"{}\"\n", epair.first, epair.second);
        }
        out += "}\n";
    }
    return out;
}

const std::string &EntDict_StringForKey(const entdict_t &dict, const std::string key)
{
    return dict.get(key);
}

float EntDict_FloatForKey(const entdict_t &dict, const std::string key)
{
    auto s = dict.get(key);
    if (s.empty())
        return 0;

    try {
        return std::stof(s);
    }
    catch (std::exception &) {
        return 0.0f;
    }
}

void EntDict_RemoveValueForKey(entdict_t &dict, const std::string &key)
{
    dict.remove(key);
}

void // mxd
EntDict_RenameKey(entdict_t &dict, const std::string &from, const std::string &to)
{
    const auto it = dict.find(from);
    if (it != dict.end()) {
        const auto oldValue = it->second;

        dict.remove(from);
        dict.set(to, oldValue);
    }
}
