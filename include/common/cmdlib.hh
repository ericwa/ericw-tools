/*  Copyright (C) 1996-1997  Id Software, Inc.

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

#include <cassert>
//#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <cctype>
#include <ctime>
#include <cstdarg>
#include <string>
#include <filesystem>
#include <memory>
#include <fmt/format.h>
#include <common/log.hh>
#include <common/qvec.hh> // FIXME: For qmax/qmin

#define stringify__(x) #x
#define stringify(x) stringify__(x)

#ifndef __GNUC__
#define __attribute__(x)
#endif

#ifdef _MSC_VER
#define unlink _unlink
#endif

/* set these before calling CheckParm */
extern int myargc;
extern char **myargv;

char *Q_strupr(char *start);
char *Q_strlower(char *start);
int Q_strncasecmp(const char *s1, const char *s2, int n);
int Q_strcasecmp(const char *s1, const char *s2);

extern std::filesystem::path    qdir, // c:/Quake/, c:/Hexen II/ etc.
                                gamedir, // c:/Quake/mymod/
                                basedir; // c:/Quake/ID1/, c:/Quake 2/BASEQ2/ etc.

bool string_iequals(const std::string &a, const std::string &b); // mxd

void SetQdirFromPath(const std::string &basedirname, std::filesystem::path path);

// Returns the path itself if it has an extension already, otherwise
// returns the path with extension replaced with `extension`.
inline std::filesystem::path DefaultExtension(const std::filesystem::path &path, const std::filesystem::path &extension)
{
    if (path.has_extension())
        return path;

    return std::filesystem::path(path).replace_extension(extension);
}

#include <chrono>

using qclock = std::chrono::high_resolution_clock;
using duration = std::chrono::duration<double>;
using time_point = std::chrono::time_point<qclock, duration>;

inline time_point I_FloatTime()
{
    return qclock::now();
}

template<typename ...Args>
inline void Print(const char *fmt, const Args &...args)
{
    fmt::print(fmt, std::forward<const Args &>(args)...);
}

[[noreturn]] void Error(const char *error) __attribute__((noreturn));

template<typename ...Args>
[[noreturn]] inline void Error(const char *fmt, const Args &...args)
{
    auto formatted = fmt::format(fmt, std::forward<const Args &>(args)...);
    Error(formatted.c_str());
}

#define FError(fmt, ...) \
    Error("{}: " fmt, __func__, __VA_ARGS__)

int CheckParm(const char *check);

using qfile_t = std::unique_ptr<FILE, decltype(&fclose)>;

qfile_t SafeOpenWrite(const std::filesystem::path &filename);
qfile_t SafeOpenRead(const std::filesystem::path &filename);
void SafeRead(const qfile_t &f, void *buffer, int count);
void SafeWrite(const qfile_t &f, const void *buffer, int count);
void SafeSeek(const qfile_t &f, long offset, int32_t origin);
long SafeTell(const qfile_t &f);
template<typename ...Args>
inline void SafePrint(const qfile_t &f, const char *fmt, const Args &...args)
{
    if (fmt::fprintf(f.get(), fmt, std::forward<const Args &>(args)...) < 0)
        FError("Error writing to file");
}

long LoadFilePak(std::filesystem::path &filename, void *destptr);
long LoadFile(const std::filesystem::path &filename, void *destptr);

int ParseNum(char *str);

short BigShort(short l);
short LittleShort(short l);
int BigLong(int l);
int LittleLong(int l);
float BigFloat(float l);
float LittleFloat(float l);

const char *COM_Parse(const char *data);

extern char com_token[1024];
extern bool com_eof;

// temporary
#ifdef _WIN32
#define copystring _strdup
#else
#define copystring strdup
#endif

inline void Q_assert_(bool success, const char *expr, const char *file, int line)
{
    if (!success) {
        LogPrint("{}:{}: Q_assert({}) failed.\n", file, line, expr);
        assert(0);
        exit(1);
    }
}

/**
 * assertion macro that is used in all builds (debug/release)
 */
#define Q_assert(x) Q_assert_((x), stringify(x), __FILE__, __LINE__)

#define Q_assert_unreachable() Q_assert(false)
