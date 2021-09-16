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
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <cctype>
#include <ctime>
#include <cstdarg>
#include <string>
#include <common/log.hh>
#include <common/qvec.hh> // FIXME: For qmax/qmin

#define stringify__(x) #x
#define stringify(x) stringify__(x)

using qboolean = bool;

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
void Q_getwd(char *out);

int Sys_filelength(FILE *f);
int FileTime(const char *path);

void Q_mkdir(const char *path);

extern char qdir[1024];
extern char gamedir[1024];
extern char basedir[1024]; // mxd

bool string_iequals(const std::string &a, const std::string &b); // mxd

void SetQdirFromPath(const char *basedirname, const char *path); // mxd
char *ExpandPath(char *path);
char *ExpandPathAndArchive(char *path);

double I_FloatTime(void);

[[noreturn]] void Error(const char *error, ...) __attribute__((format(printf, 1, 2), noreturn));
int CheckParm(const char *check);

FILE *SafeOpenWrite(const char *filename);
FILE *SafeOpenRead(const char *filename);
void SafeRead(FILE *f, void *buffer, int count);
void SafeWrite(FILE *f, const void *buffer, int count);

int LoadFilePak(char *filename, void *destptr);
int LoadFile(const char *filename, void *destptr);
void SaveFile(const char *filename, const void *buffer, int count);

void DefaultExtension(char *path, const char *extension);
void DefaultPath(char *path, const char *basepath);
std::string StrippedFilename(const std::string &path);
void StripExtension(char *path);
std::string StrippedExtension(const std::string &path);
int IsAbsolutePath(const char *path);

void ExtractFilePath(char *path, char *dest);
void ExtractFileBase(char *path, char *dest);
void ExtractFileExtension(char *path, char *dest);

int ParseNum(char *str);

short BigShort(short l);
short LittleShort(short l);
int BigLong(int l);
int LittleLong(int l);
float BigFloat(float l);
float LittleFloat(float l);

const char *COM_Parse(const char *data);

extern char com_token[1024];
extern qboolean com_eof;

char *copystring(const char *s);

void CRC_Init(unsigned short *crcvalue);
void CRC_ProcessByte(unsigned short *crcvalue, uint8_t data);
unsigned short CRC_Value(unsigned short crcvalue);

void CreatePath(char *path);
void Q_CopyFile(const char *from, char *to);

extern qboolean archive;
extern char archivedir[1024];

inline void Q_assert_(bool success, const char *expr, const char *file, int line)
{
    if (!success) {
        logprint("%s:%d: Q_assert(%s) failed.\n", file, line, expr);
        assert(0);
        exit(1);
    }
}

/**
 * assertion macro that is used in all builds (debug/release)
 */
#define Q_assert(x) Q_assert_((x), stringify(x), __FILE__, __LINE__)

#define Q_assert_unreachable() Q_assert(false)
