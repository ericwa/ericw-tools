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

#include <common/log.h>
#include <common/cmdlib.h>
#include <stdio.h>

static FILE *logfile;
static qboolean log_ok;

void
init_log(char *filename)
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

void
logprint(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vprintf(fmt, args);
    if (log_ok) {
	vfprintf(logfile, fmt, args);
	fflush(logfile);
    }
}

void
logvprint(const char *fmt, va_list args)
{
    vprintf(fmt, args);
    if (log_ok) {
	vfprintf(logfile, fmt, args);
	fflush(logfile);
    }
}
