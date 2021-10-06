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
//#include <cstdio>
#include <filesystem>
#include <fmt/format.h>

// TODO: no wchar_t support in this version apparently
template<>
struct fmt::formatter<std::filesystem::path> : formatter<std::string>
{
    template<typename FormatContext>
    auto format(const std::filesystem::path &p, FormatContext &ctx)
    {
        return formatter<std::string>::format(p.string(), ctx);
    }
};

enum : int8_t
{
    LOG_DEFAULT,
    LOG_VERBOSE,
    LOG_PROGRESS,
    LOG_PERCENT,
    LOG_STAT
};

using log_flag_t = int8_t;

extern log_flag_t log_mask;

void InitLog(const std::filesystem::path &filename);

void CloseLog();

/* Print to screen and to log file */
void LogPrint(log_flag_t type, const char *str);

void LogPercent(int32_t value, int32_t max);

inline void LogPrint(const char *str)
{
    LogPrint(LOG_DEFAULT, str);
}

template<typename... Args>
inline void LogPrint(log_flag_t type, const char *fmt, const Args &...args)
{
    if (!type || (log_mask & (1 << type)))
        LogPrint(type, fmt::format(fmt, std::forward<const Args &>(args)...).c_str());
}

template<typename... Args>
inline void LogPrint(const char *fmt, const Args &...args)
{
    LogPrint(LOG_DEFAULT, fmt::format(fmt, std::forward<const Args &>(args)...).c_str());
}

#define FLogPrint(fmt, ...) LogPrint("{}: " fmt, __func__, ##__VA_ARGS__)

/* Print only into log file */
void LogPrintSilent(const char *str);

/* Only called from the threads code */
void LogPrintLocked(const char *str);

template<typename... Args>
inline void LogPrintLocked(const char *fmt, const Args &...args)
{
    LogPrintLocked(fmt::format(fmt, std::forward<const Args &>(args)...).c_str());
}