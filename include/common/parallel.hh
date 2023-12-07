/*  Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 2017 Eric Wasylishen

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

#include "common/log.hh"
#include <tbb/parallel_for.h>
#include <tbb/parallel_for_each.h>

#include <atomic>

// parallel extensions to logging
namespace logging
{
template<typename TS, typename TE, typename Body>
void parallel_for(const TS &start, const TE &end, const Body &func)
{
    auto length = end - start;
    std::atomic<uint64_t> progress = 0;

    tbb::parallel_for(start, end, [&](const auto &it) {
        percent(progress++, length);
        func(it);
    });

    percent(progress, length);
}

template<typename Container, typename Body>
void parallel_for_each(Container &container, const Body &func)
{
    auto length = std::size(container);
    std::atomic<uint64_t> progress = 0;

    tbb::parallel_for_each(container, [&](auto &f) {
        percent(progress++, length);
        func(f);
    });

    percent(progress, length);
}

template<typename Container, typename Body>
void parallel_for_each(const Container &container, const Body &func)
{
    auto length = std::size(container);
    std::atomic<uint64_t> progress = 0;

    tbb::parallel_for_each(container, [&](const auto &f) {
        percent(progress++, length);
        func(f);
    });

    percent(progress, length);
}
} // namespace logging