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

#ifndef __COMMON_LOG_H__
#define __COMMON_LOG_H__

#include <stdarg.h>
#include <stdio.h>

void init_log(const char *filename);
void close_log();

/* Print to screen and to log file */
void logprint(const char *fmt, ...)
    __attribute__((format(printf,1,2)));
void logvprint(const char *fmt, va_list args)
    __attribute__((format(printf,1,0)));

/* Print only into log file */
void logprint_silent(const char *fmt, ...)
    __attribute__((format(printf,1,2)));

/* Only called from the threads code */
void logprint_locked__(const char *fmt, ...)
    __attribute__((format(printf,1,2)));

#endif /* __COMMON_LOG_H__ */
