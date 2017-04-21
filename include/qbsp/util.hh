/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

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

#ifndef QBSP_UTIL_HH
#define QBSP_UTIL_HH

#define msgWarning      1
#define msgStat         2
#define msgProgress     3
#define msgLiteral      4
#define msgFile         5
#define msgScreen       6
#define msgPercent      7

extern const char *rgszWarnings[cWarnings];
extern const int *MemSize;
extern const int MemSize_BSP29[GLOBAL + 1];
extern const int MemSize_BSP2rmq[GLOBAL + 1];
extern const int MemSize_BSP2[GLOBAL + 1];

void *AllocMem(int Type, int cSize, bool fZero);
void FreeMem(void *pMem, int Type, int cSize);
void FreeAllMem(void);
void PrintMem(void);

void Message(int MsgType, ...);
void Error(const char *error, ...)
    __attribute__((format(printf,1,2),noreturn));

int q_snprintf(char *str, size_t size, const char *format, ...);

#endif
