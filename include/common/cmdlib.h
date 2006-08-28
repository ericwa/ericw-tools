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

#ifndef __COMMON_CMDLIB_H__
#define __COMMON_CMDLIB_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>

typedef enum { false, true } qboolean;
typedef unsigned char byte;

/* the dec offsetof macro doesn't work very well... */
#define myoffsetof(type,identifier) ((size_t)&((type *)0)->identifier)

/* set these before calling CheckParm */
extern int myargc;
extern char **myargv;

char *strupr(char *in);
char *strlower(char *in);
int Q_strncasecmp(const char *s1, const char *s2, int n);
int Q_strcasecmp(const char *s1, const char *s2);
void Q_getwd(char *out);

int Sys_filelength(FILE *f);
int FileTime(const char *path);

void Q_mkdir(const char *path);

extern char qdir[1024];
extern char gamedir[1024];

void SetQdirFromPath(char *path);
char *ExpandPath(char *path);
char *ExpandPathAndArchive(char *path);

double I_FloatTime(void);

void Error(const char *error, ...);
int CheckParm(const char *check);

FILE *SafeOpenWrite(const char *filename);
FILE *SafeOpenRead(const char *filename);
void SafeRead(FILE *f, void *buffer, int count);
void SafeWrite(FILE *f, const void *buffer, int count);

int LoadFile(const char *filename, void **bufferptr);
void SaveFile(const char *filename, const void *buffer, int count);

void DefaultExtension(char *path, const char *extension);
void DefaultPath(char *path, const char *basepath);
void StripFilename(char *path);
void StripExtension(char *path);

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


char *COM_Parse(char *data);

extern char com_token[1024];
extern qboolean com_eof;

char *copystring(const char *s);

void CRC_Init(unsigned short *crcvalue);
void CRC_ProcessByte(unsigned short *crcvalue, byte data);
unsigned short CRC_Value(unsigned short crcvalue);

void CreatePath(char *path);
void CopyFile(const char *from, char *to);

extern qboolean archive;
extern char archivedir[1024];

#endif /* __COMMON_CMDLIB_H__ */
