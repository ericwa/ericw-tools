/*  Copyright (C) 2000-2001  Kevin Shanahan

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
 * common/log.c
 */

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <fmt/ostream.h>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <string>

#include <common/log.hh>
#include <common/settings.hh>
#include <common/cmdlib.hh>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> // for OutputDebugStringA

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

static std::ofstream logfile;

namespace logging
{
bitflags<flag> mask = bitflags<flag>(flag::ALL) & ~bitflags<flag>(flag::VERBOSE);
bool enable_color_codes = true;

void init(const fs::path &filename, const settings::common_settings &settings)
{
    if (settings.log.value()) {
        logfile.open(filename);
        fmt::print(logfile, "---- {} / ericw-tools {} ----\n", settings.programName, ERICWTOOLS_VERSION);
    }
}

void close()
{
    if (logfile) {
        logfile.close();
    }
}

static std::mutex print_mutex;

void print(flag logflag, const char *str)
{
    if (!(mask & logflag)) {
        return;
    }

    fmt::text_style style;

    if (enable_color_codes) {
        if (string_icontains(str, "error")) {
            style = fmt::fg(fmt::color::red);
        } else if (string_icontains(str, "warning")) {
            style = fmt::fg(fmt::terminal_color::yellow);
        } else if (bitflags<flag>(logflag) & flag::PERCENT) {
            style = fmt::fg(fmt::terminal_color::blue);
        } else if (bitflags<flag>(logflag) & flag::STAT) {
            style = fmt::fg(fmt::terminal_color::cyan);
        }
    }

    print_mutex.lock();

    if (logflag != flag::PERCENT) {
        // log file, if open
        if (logfile) {
            logfile << str;
            logfile.flush();
        }

#ifdef _WIN32
        // print to windows console.
        // if VS's Output window gets support for ANSI colors, we can change this to ansi_str.c_str()
        OutputDebugStringA(str);
#endif
    }

    if (enable_color_codes) {
        // stdout (assume the terminal can render ANSI colors)
        fmt::print(style, "{}", str);
    } else {
        std::cout << str;
    }

    // for TB, etc...
    fflush(stdout);

    print_mutex.unlock();
}

void print(const char *str)
{
    print(flag::DEFAULT, str);
}

static time_point start_time;
static bool is_timing = false;
static uint64_t last_count = -1;
static time_point last_indeterminate_time;
static std::atomic_bool locked = false;

void header(const char *name)
{
    print(flag::PROGRESS, "---- {} ----\n", name);
}

void assert_(bool success, const char *expr, const char *file, int line)
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

void percent(uint64_t count, uint64_t max, bool displayElapsed)
{
    bool expected = false;

    if (!(logging::mask & flag::CLOCK_ELAPSED)) {
        displayElapsed = false;
    }

    if (count == max) {
        while (!locked.compare_exchange_weak(expected, true)) ; // wait until everybody else is done
    } else {
        if (!locked.compare_exchange_weak(expected, true)) {
            return; // somebody else is doing this already
        }
    }

    // we got the lock

    if (!is_timing) {
        start_time = I_FloatTime();
        is_timing = true;
        last_count = -1;
        last_indeterminate_time = {};
    }

    if (count == max) {
        auto elapsed = I_FloatTime() - start_time;
        is_timing = false;
        if (displayElapsed) {
            if (max == indeterminate) {
                print(flag::PERCENT, "[done] time elapsed: {:.3}\n", elapsed);
            } else {
                print(flag::PERCENT, "[100%] time elapsed: {:.3}\n", elapsed);
            }
        }
        last_count = -1;
    } else {
        if (max != indeterminate) {
            uint32_t pct = static_cast<uint32_t>((static_cast<float>(count) / max) * 100);
            if (last_count != pct) {
                print(flag::PERCENT, "[{:>3}%]\r", pct);
                last_count = pct;
            }
        } else {
            auto t = I_FloatTime();

            if (t - last_indeterminate_time > std::chrono::milliseconds(100)) {
                constexpr const char *spinners[] = {
                    ".   ",
                    " .  ",
                    "  . ",
                    "   ."
                };
                last_count = (last_count + 1) >= std::size(spinners) ? 0 : (last_count + 1);
                print(flag::PERCENT, "[{}]\r", spinners[last_count]);
                last_indeterminate_time = t;
            }
        }
    }

    // unlock for next call
    locked = false;
}

// percent_clock

percent_clock::percent_clock(uint64_t i_max) : max(i_max)
{
    if (max != 0) {
        percent(0, max, displayElapsed);
    }
}

void percent_clock::increase()
{
#ifdef _DEBUG
    if (count == max) {
        logging::print("ERROR TO FIX LATER: clock counter increased to end, but not finished yet\n");
    }
#endif

    percent(count++, max, displayElapsed);
}

void percent_clock::operator()()
{
    increase();
}

void percent_clock::operator++(int)
{
    increase();
}

void percent_clock::print()
{
    if (!ready) {
        return;
    }

    ready = false;
    
#ifdef _DEBUG
    if (max != indeterminate) {
        if (count != max) {
            logging::print("ERROR TO FIX LATER: clock counter ended too early\n");
        }
    }
#endif

    percent(max, max, displayElapsed);
}

percent_clock::~percent_clock()
{
    print();
}

// stat_tracker_t

stat_tracker_t::stat &stat_tracker_t::register_stat(const std::string &name, bool show_even_if_zero, bool is_warning)
{
    return stats.emplace_back(name, show_even_if_zero, is_warning);
}

size_t stat_tracker_t::number_of_digits(size_t n)
{
    return n ? ((size_t) log10(n) + 1) : 1;
}

size_t stat_tracker_t::number_of_digit_padding()
{
    size_t number_padding = 0;

    // calculate padding for number
    for (auto &stat : stats) {
        if (!stat.is_warning && (stat.show_even_if_zero || stat.count)) {
            number_padding = std::max(number_of_digits(stat.count.load()), number_padding);
        }
    }

    if (!number_padding) {
        return number_padding;
    }

    return number_padding + ((number_padding - 1) / 3);
}

void stat_tracker_t::print_stats()
{
    if (stats_printed) {
        return;
    }

    stats_printed = true;

    // add 8 char padding just to keep it away from the left side
    size_t number_padding = number_of_digit_padding() + 4;

    for (auto &stat : stats) {
        if (stat.show_even_if_zero || stat.count) {
            print(flag::STAT, "{}{:{}} {}\n", stat.is_warning ? "WARNING: " : "", fmt::group_digits(stat.count.load()), stat.is_warning ? 0 : number_padding, stat.name);
        }
    }
}

stat_tracker_t::~stat_tracker_t()
{
    print_stats();
}
}; // namespace logging

/*
 * =================
 * Error
 * For abnormal program terminations
 * =================
 */
[[noreturn]] void Error(const char *error)
{
    logging::print("************ ERROR ************\n{}\n", error);
    logging::close();
#ifdef _DEBUG
    __debugbreak();
#endif
    exit(1);
}
