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

#include <nlohmann/json.hpp>
#include <common/qvec.hh>

using nlohmann::json;

template<typename T, size_t N>
void to_json(json &j, const qvec<T, N> &p)
{
    j = json::array();

    for (auto &v : p) {
        j.push_back(v);
    }
}

template<typename T, size_t N>
void from_json(const json &j, qvec<T, N> &p)
{

    for (size_t i = 0; i < N; i++) {
        p[i] = j[i].get<T>();
    }
}