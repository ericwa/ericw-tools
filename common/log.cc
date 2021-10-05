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

#include <common/log.hh>
#include <common/threads.hh>
#include <common/cmdlib.hh>

log_flag_t log_mask =
    (std::numeric_limits<log_flag_t>::max()) & ~((1 << LOG_VERBOSE) | (1 << LOG_STAT) | (1 << LOG_PROGRESS));

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> // for OutputDebugStringA
#endif

static std::ofstream logfile;

void InitLog(const std::filesystem::path &filename)
{
    logfile.open(filename);
}

void CloseLog()
{
    if (logfile)
        logfile.close();
}

void LogPrintLocked(const char *str)
{
    // log file, if open
    if (logfile) {
        logfile << str;
        logfile.flush();
    }

    // stdout
    std::cout << str;

    // print to windows console
#ifdef _WIN32
    OutputDebugStringA(str);
#endif
}

static bool fInPercent = false;

void LogPrint(log_flag_t type, const char *str)
{
    if (type && !(log_mask & (1 << type)))
        return;

    if (fInPercent && type != LOG_PERCENT) {
        std::cout << "\r";
        fInPercent = false;
    }

    ThreadLock();
    InterruptThreadProgress__();
    LogPrintLocked(str);
    ThreadUnlock();
}

void LogPrintSilent(const char *str)
{
    ThreadLock();
    InterruptThreadProgress__();

    if (logfile) {
        logfile << str;
        logfile.flush();
    }

    ThreadUnlock();
}

void LogPercent(int32_t value, int32_t max)
{
    if (!(log_mask & (1 << LOG_PERCENT)))
        return;

    if (((value + 1) * 100) / max == (value * 100) / max)
        return;

    ThreadLock();
    InterruptThreadProgress__();

    // stdout
    fmt::print("\r{:3}%", ((value + 1) * 100) / max);
    fflush(stdout);

    fInPercent = true;

    ThreadUnlock();
}