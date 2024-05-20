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

void preinitialize()
{
#ifdef _WIN32
    // enable processing of ANSI escape sequences on Windows
    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleMode(hOutput, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

void init(std::optional<fs::path> filename, const settings::common_settings &settings)
{
    if (!settings.log.value()) {
        return;
    }

    if (settings.logfile.is_changed()) {
        filename = settings.logfile.value();
    }

    if (!filename.has_value()) {
        return;
    }

    fs::path p = fs::absolute(filename.value());

    if (logfile) {
        logfile.close();
    }

    logfile.open(p, settings.logappend.value() ? std::ios_base::app : std::ios_base::trunc);
    
    if (logfile) {
        print(flag::PROGRESS, "logging to {} ({})\n", p.string(), settings.logappend.value() ? "append" : "truncate");
        fmt::print(logfile, "---- {} / ericw-tools {} ----\n", settings.program_name, ERICWTOOLS_VERSION);
    } else {
        print(flag::PROGRESS, "WARNING: can't log to {}\n", p.string());
    }
}

void close()
{
    if (logfile) {
        fmt::print(logfile, "\n\n");
        logfile.close();
    }
}

static std::mutex print_mutex;
static print_callback_t active_print_callback;

void set_print_callback(print_callback_t cb)
{
    active_print_callback = cb;
}

void print(flag logflag, const char *str)
{
    if (!(mask & logflag)) {
        return;
    }

    if (active_print_callback) {
        active_print_callback(logflag, str);
    }

    fmt::text_style style;

    if (enable_color_codes) {
        if (string_icontains(str, "error")) {
            style = fmt::fg(fmt::color::red);
        } else if (string_icontains(str, "warning")) {
            style = fmt::fg(fmt::terminal_color::yellow);
        } else if (bitflags<flag>(logflag) & flag::PERCENT) {
            style = fmt::fg(fmt::terminal_color::bright_black);
        } else if (bitflags<flag>(logflag) & flag::STAT) {
            style = fmt::fg(fmt::terminal_color::cyan);
        }
    }

    print_mutex.lock();

    if (logflag != flag::PERCENT) {
        // log file, if open
        if (logfile && logflag != flag::PROGRESS) {
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

void vprint(flag logflag, fmt::string_view format, fmt::format_args args)
{
    // see https://fmt.dev/10.0.0/api.html#argument-lists

    print(logflag, fmt::vformat(format, args).c_str());
}

void print(const char *str)
{
    print(flag::DEFAULT, str);
}

void vprint(fmt::string_view format, fmt::format_args args)
{
    vprint(flag::DEFAULT, format, args);
}

static time_point start_time;
static bool is_timing = false;
static uint64_t last_count = -1;
static time_point last_indeterminate_time;
static std::atomic_bool locked = false;
static std::array<duration, 10> one_percent_times;
static size_t num_percent_times, percent_time_index;
static time_point last_percent_time;

static duration average_times_for_one_percent()
{
    duration pt {};

    for (size_t i = 0; i < num_percent_times; i++) {
        pt += one_percent_times[i];
    }

    pt /= num_percent_times;

    return pt;
}

static void register_average_time(duration dt)
{
    one_percent_times[percent_time_index] = dt;
    percent_time_index = (percent_time_index + 1) % one_percent_times.size();

    if (num_percent_times < one_percent_times.size()) {
        num_percent_times++;
    }
}

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

static percent_callback_t active_percent_callback;

void set_percent_callback(percent_callback_t cb)
{
    active_percent_callback = cb;
}

void percent(uint64_t count, uint64_t max, bool displayElapsed)
{
    bool expected = false;

    if (!(logging::mask & flag::CLOCK_ELAPSED)) {
        displayElapsed = false;
    }

    if (count == max) {
        while (!locked.compare_exchange_weak(expected, true))
            ; // wait until everybody else is done
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
        num_percent_times = 0;
        percent_time_index = 0;
        last_percent_time = I_FloatTime();
    }

    if (count == max) {
        auto elapsed = I_FloatTime() - start_time;
        is_timing = false;
        if (displayElapsed) {
            if (max == indeterminate) {
                if (active_percent_callback) {
                    active_percent_callback(std::nullopt, elapsed);
                } else {
                    print(flag::PERCENT, "[done] time elapsed: {:%H:%M:%S}\n", elapsed);
                }
            } else {
                if (active_percent_callback) {
                    active_percent_callback(100u, elapsed);
                } else {
                    print(flag::PERCENT, "[100%] time elapsed: {:%H:%M:%S}\n", elapsed);
                }
            }
        }
        last_count = -1;
    } else {
        if (max != indeterminate) {
            uint32_t pct = static_cast<uint32_t>((static_cast<float>(count) / max) * 100);
            if (last_count != pct) {
                if (active_percent_callback) {
                    active_percent_callback(pct, std::nullopt);
                } else {
                    if (pct) {
                        // we shifted from some other percent value to a non-zero value;
                        // calculate the time it took to get a 1% change
                        uint64_t diff = pct - last_count; // should always be >= 1
                        duration dt = I_FloatTime() - last_percent_time;
                        dt /= diff;
                        register_average_time(dt);
                        last_percent_time = I_FloatTime();
                        print(flag::PERCENT, "[{:>3}%]  est: {:%H:%M:%S}\r", pct, std::chrono::duration_cast<std::chrono::duration<long long>>(average_times_for_one_percent() * (100 - pct)));
                    } else {
                        print(flag::PERCENT, "[{:>3}%]  ...\r", pct);
                    }
                }
                last_count = pct;
            }
        } else {
            auto t = I_FloatTime();

            if (t - last_indeterminate_time > std::chrono::milliseconds(100)) {
                constexpr const char *spinners[] = {".   ", " .  ", "  . ", "   ."};
                if (active_percent_callback) {
                    active_percent_callback(std::nullopt, std::nullopt);
                } else {
                    last_count = (last_count + 1) >= std::size(spinners) ? 0 : (last_count + 1);
                    print(flag::PERCENT, "[{}]\r", spinners[last_count]);
                }
                last_indeterminate_time = t;
            }
        }
    }

    // unlock for next call
    locked = false;
}

// percent_clock

percent_clock::percent_clock(uint64_t i_max)
    : max(i_max)
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
    return n ? ((size_t)log10(n) + 1) : 1;
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
            print(flag::STAT, "{}{:{}} {}\n", stat.is_warning ? "WARNING: " : "", fmt::group_digits(stat.count.load()),
                stat.is_warning ? 0 : number_padding, stat.name);
        }
    }
}

stat_tracker_t::~stat_tracker_t()
{
    print_stats();
}
}; // namespace logging

ericwtools_error::ericwtools_error(const char *what)
    : std::runtime_error(what)
{
}

[[noreturn]] void exit_on_exception(const std::exception &e)
{
    logging::print("************ ERROR ************\n{}\n", e.what());
    logging::close();
    exit(1);
}

/*
 * =================
 * Error
 * For abnormal program terminations
 * =================
 */
[[noreturn]] void Error(const char *error)
{
#ifdef _DEBUG
    __debugbreak();
#endif
    throw ericwtools_error(error);
}

[[noreturn]] void VError(fmt::string_view format, fmt::format_args args)
{
    auto formatted = fmt::vformat(format, args);
    Error(formatted.c_str());
}
