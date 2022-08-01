/*  Copyright (C) 2022 Eric Wasylishen

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

#include <algorithm>
#include <iterator>
#include <vector>
#include <list>

template<class T>
void sort_and_remove_duplicates(T &v)
{
    std::sort(v.begin(), v.end());

    auto last = std::unique(v.begin(), v.end());
    v.erase(last, v.end());
}

template<class E>
std::vector<E> concat(const std::vector<E> &a, const std::vector<E> &b)
{
    std::vector<E> result;
    result.reserve(a.size() + b.size());

    std::copy(a.begin(), a.end(), std::back_inserter(result));
    std::copy(b.begin(), b.end(), std::back_inserter(result));
    return result;
}

template<class E>
std::vector<E> make_vector(E e)
{
    std::vector<E> result;
    result.push_back(std::move(e));
    return result;
}

template<class E>
std::list<E> make_list(E e)
{
    std::list<E> result;
    result.push_back(std::move(e));
    return result;
}
