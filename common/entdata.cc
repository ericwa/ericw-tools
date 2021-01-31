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

/*
 * ==================
 * EntData_Parse
 * ==================
 */
std::vector<entdict_t>
EntData_Parse(const char *entdata)
{
    std::vector<entdict_t> result;
    const char *data = entdata;
    
    /* go through all the entities */
    while (1) {
        /* parse the opening brace */
        data = COM_Parse(data);
        if (!data)
            break;
        if (com_token[0] != '{')
            Error("%s: found %s when expecting {", __func__, com_token);
        
        /* Allocate a new entity */
        entdict_t entity;
        
        /* go through all the keys in this entity */
        while (1) {
            /* parse key */
            data = COM_Parse(data);
            if (!data)
                Error("%s: EOF without closing brace", __func__);
            
            std::string keystr { com_token };
            
            if (keystr == "}")
                break;
            if (keystr.length() > MAX_ENT_KEY - 1)
                Error("%s: Key length > %i: '%s'", __func__, MAX_ENT_KEY - 1, keystr.c_str());
            
            /* parse value */
            data = COM_Parse(data);
            if (!data)
                Error("%s: EOF without closing brace", __func__);
            
            std::string valstring { com_token };
            
            if (valstring[0] == '}')
                Error("%s: closing brace without data", __func__);
            if (valstring.length() > MAX_ENT_VALUE - 1)
                Error("%s: Value length > %i", __func__, MAX_ENT_VALUE - 1);
            
            entity[keystr] = valstring;
        }
        
        result.push_back(entity);
    }
    
    return result;
}

/*
 * ================
 * EntData_Write
 * ================
 */
std::string
EntData_Write(const std::vector<entdict_t> &ents)
{
    std::stringstream out;
    for (const auto &ent : ents) {
        out << "{\n";
        for (const auto &epair : ent) {
            out << "\"" << epair.first << "\" \"" << epair.second << "\"\n";
        }
        out << "}\n";
    }
    return out.str();
}

std::string
EntDict_StringForKey(const entdict_t &dict, const std::string key)
{
    auto it = dict.find(key);
    if (it != dict.end()) {
        return it->second;
    }
    return "";
}

float
EntDict_FloatForKey(const entdict_t &dict, const std::string key)
{
    auto s = EntDict_StringForKey(dict, key);
    if (s.empty())
        return 0;
    
    try {
        return std::stof(s);
    } catch (std::exception &) {
        return 0.0f;
    }
}

void
EntDict_RemoveValueForKey(entdict_t &dict, const std::string &key)
{
    const auto it = dict.find(key);
    if (it != dict.end()) {
        dict.erase(it);
    }
    Q_assert(dict.find(key) == dict.end());
}

void //mxd
EntDict_RenameKey(entdict_t &dict, const std::string &from, const std::string &to)
{
    const auto it = dict.find(from);
    if (it != dict.end()) {
        swap(dict[to], it->second);
        dict.erase(it);
    }
}
