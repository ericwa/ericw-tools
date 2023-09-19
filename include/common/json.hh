/*  Copyright (C) 2017 Eric Wasylishen

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

// JSON & formatters for our types

#include <json/json.h>
#include <common/qvec.hh>

template<typename T, size_t N>
Json::Value to_json(const qvec<T, N> &p)
{
    auto j = Json::Value(Json::arrayValue);

    for (auto &v : p) {
        j.append(v);
    }

    return j;
}

template<typename T>
Json::Value to_json(const std::vector<T> &vec)
{
    auto j = Json::Value(Json::arrayValue);

    for (auto &v : vec) {
        j.append(v);
    }

    return j;
}

template<class T, size_t N>
Json::Value to_json(const std::array<T, N> &arr)
{
    auto j = Json::Value(Json::arrayValue);

    for (auto &v : arr) {
        j.append(v);
    }

    return j;
}

template<class T>
Json::Value json_array(std::initializer_list<T> args)
{
    auto j = Json::Value(Json::arrayValue);

    for (auto &v : args) {
        j.append(v);
    }

    return j;
}

template<typename T, size_t N>
qvec<T, N> from_json(const Json::Value &j)
{
    qvec<T, N> p;
    for (unsigned int i = 0; i < N; i++) {
        p[i] = j[i].as<T>();
    }
    return p;
}

static Json::Value parse_json(const uint8_t *begin, const uint8_t *end)
{
    Json::Value result;
    Json::CharReaderBuilder rbuilder;
    auto reader = std::unique_ptr<Json::CharReader>(rbuilder.newCharReader());
    reader->parse(reinterpret_cast<const char*>(begin),
        reinterpret_cast<const char*>(end), &result, nullptr);
    return result;
}
