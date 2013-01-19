/*  Copyright (C) 2012-2013 Kevin Shanahan

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

#ifndef VIS_LEAFBITS_H
#define VIS_LEAFBITS_H

#include <stdlib.h>
#include <string.h>

/* Use some GCC builtins */
#ifndef ffsl
#define ffsl __builtin_ffsl
#endif
#ifndef offsetof
#define offsetof(type, member)  __builtin_offsetof(type, member)
#endif

typedef unsigned long leafblock_t;
typedef struct {
    int numleafs;
    leafblock_t bits[]; /* Variable Sized */
} leafbits_t;

int __ERRORLONGSIZE(void); /* to generate an error at link time */
#define QBYTESHIFT(x) ((x) == 8 ? 6 : ((x) == 4 ? 5 : __ERRORLONGSIZE() ))
#define LEAFSHIFT QBYTESHIFT(sizeof(leafblock_t))
#define LEAFMASK  ((sizeof(leafblock_t) << 3) - 1UL)

static inline int
TestLeafBit(const leafbits_t *bits, int leafnum)
{
    return !!(bits->bits[leafnum >> LEAFSHIFT] & (1UL << (leafnum & LEAFMASK)));
}

static inline void
SetLeafBit(leafbits_t *bits, int leafnum)
{
    bits->bits[leafnum >> LEAFSHIFT] |= 1UL << (leafnum & LEAFMASK);
}

static inline void
ClearLeafBit(leafbits_t *bits, int leafnum)
{
    bits->bits[leafnum >> LEAFSHIFT] &= ~(1UL << (leafnum & LEAFMASK));
}

static inline size_t
LeafbitsSize(int numleafs)
{
    return offsetof(leafbits_t, bits[(numleafs + LEAFMASK) >> LEAFSHIFT]);
}

#endif /* VIS_LEAFBITS_H */
