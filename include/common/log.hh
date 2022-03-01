/*  Copyright (C) 2000-2006  Kevin Shanahan

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

/*
 * common/log.h
 *
 * Stuff for logging selected output to a file
 * as well as the stdout.
 */

#pragma once

#include <cstdarg>
#include <filesystem>
#include <fmt/format.h>
#include <common/bitflags.hh>
#include <common/fs.hh>

// forward declaration
namespace settings
{
    class common_settings;
}

namespace logging
{
enum class flag : uint8_t
{
    NONE        = 0,      // none of the below (still prints though)
    DEFAULT     = 1 << 0, // prints everywhere
    VERBOSE     = 1 << 1, // prints everywhere, if enabled
    PROGRESS    = 1 << 2, // prints only to stdout
    PERCENT     = 1 << 3, // prints everywhere, if enabled
    STAT        = 1 << 4, // prints everywhere, if enabled
    ALL         = 0xFF
};

extern bitflags<flag> mask;

// initialize logging subsystem
void init(const fs::path &filename, const settings::common_settings &settings);

// shutdown logging subsystem
void close();

// print to respective targets based on log flag
void print(flag logflag, const char *str);

// print to default targets
inline void print(const char *str)
{
    print(flag::DEFAULT, str);
}

// format print to specified targets
template<typename... Args>
inline void print(flag type, const char *fmt, const Args &...args)
{
    if (mask & type) {
        print(type, fmt::format(fmt, std::forward<const Args &>(args)...).c_str());
    }
}

// format print to default targets
template<typename... Args>
inline void print(const char *fmt, const Args &...args)
{
    print(flag::DEFAULT, fmt::format(fmt, std::forward<const Args &>(args)...).c_str());
}

// TODO: C++20 source_location
#ifdef _MSC_VER
#define funcprint(fmt, ...) print(__FUNCTION__ ": " fmt, ##__VA_ARGS__)
#else
#define funcprint(fmt, ...) print("{}: " fmt, __func__, ##__VA_ARGS__)
#endif

inline void assert_(bool success, const char *expr, const char *file, int line)
{
    if (!success) {
        print("{}:{}: Q_assert({}) failed.\n", file, line, expr);
        // assert(0);
#ifdef _WIN32
        __debugbreak();
#endif
        exit(1);
    }
}

// Display a percent timer. This also keeps track of how long the
// current task is taking to execute. Note that only one of these
// can be active at a time. Once `count` == `max`, the progress
// bar will "finish" and be inoperable.
// Only use this by hand if you absolutely need to; otherwise,
// use <common/parallel.h>'s parallel_for(_each)
void percent(uint64_t count, uint64_t max);
};