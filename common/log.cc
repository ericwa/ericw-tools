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

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

#include <common/log.hh>
#include <common/threads.hh>
#include <common/cmdlib.hh>

#ifdef _WIN32
#include <windows.h> // for OutputDebugStringA
#endif

static FILE *logfile;
static bool log_ok;

void
init_log(const char *filename)
{
    log_ok = false;
    if ((logfile = fopen(filename, "w")))
        log_ok = true;
}

void
close_log()
{
    if (log_ok)
        fclose(logfile);
}

static void
logvprint_locked__(const char *fmt, va_list args)
{
    va_list log_args;
    char line[1024];

    va_copy(log_args, args);
    q_vsnprintf(line, sizeof(line), fmt, args);
    va_end(log_args);

    // print to log file
    if (log_ok) {
        fprintf(logfile, "%s", line);
        fflush(logfile);
    }
    
    // print to stdout
    printf("%s", line);
    fflush(stdout);

    // print to windows console
#ifdef _WIN32
    OutputDebugStringA(line);
#endif
}

void
logvprint(const char *fmt, va_list args)
{
    ThreadLock();
    InterruptThreadProgress__();
    logvprint_locked__(fmt, args);
    ThreadUnlock();
}

void
logprint_locked__(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    logvprint_locked__(fmt, args);
    va_end(args);
}

void logprint_silent(const char *fmt, ...)
{
    va_list args;

    ThreadLock();
    va_start(args, fmt);
    vfprintf(logfile, fmt, args);
    va_end(args);
    ThreadUnlock();
}

void
logprint(const char *fmt, ...)
{
    va_list args;

    ThreadLock();
    InterruptThreadProgress__();
    va_start(args, fmt);
    logvprint_locked__(fmt, args);
    va_end(args);
    ThreadUnlock();
}
