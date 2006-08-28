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

#ifndef __COMMON_MATHLIB_H__
#define __COMMON_MATHLIB_H__

#include <math.h>

#ifdef DOUBLEVEC_T
typedef double vec_t;
#else
typedef float vec_t;
#endif
typedef vec_t vec3_t[3];

#define SIDE_FRONT  0
#define SIDE_ON     2
#define SIDE_BACK   1
#define SIDE_CROSS -2

#define Q_PI 3.14159265358979323846

extern vec3_t vec3_origin;

#define EQUAL_EPSILON 0.001

qboolean VectorCompare(const vec3_t v1, const vec3_t v2);

static inline vec_t
DotProduct(const vec3_t x, const vec3_t y)
{
    return x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
}

static inline void
VectorSubtract(const vec3_t x, const vec3_t y, vec3_t out)
{
    out[0] = x[0] - y[0];
    out[1] = x[1] - y[1];
    out[2] = x[2] - y[2];
}

static inline void
VectorAdd(const vec3_t x, const vec3_t y, vec3_t out)
{
    out[0] = x[0] + y[0];
    out[1] = x[1] + y[1];
    out[2] = x[2] + y[2];
}

static inline void
VectorCopy(const vec3_t in, vec3_t out)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
}

static inline void
VectorScale(const vec3_t v, vec_t scale, vec3_t out)
{
    out[0] = v[0] * scale;
    out[1] = v[1] * scale;
    out[2] = v[2] * scale;
}

static inline void
VectorInverse(vec3_t v)
{
    v[0] = -v[0];
    v[1] = -v[1];
    v[2] = -v[2];
}

static inline vec_t
Q_rint(vec_t in)
{
    return (vec_t)(floor(in + 0.5));
}

static inline void
VectorMA(const vec3_t va, vec_t scale, const vec3_t vb, vec3_t vc)
{
    vc[0] = va[0] + scale * vb[0];
    vc[1] = va[1] + scale * vb[1];
    vc[2] = va[2] + scale * vb[2];
}


void CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross);
vec_t VectorNormalize(vec3_t v);
double VectorLength(const vec3_t v);

#endif /* __COMMON_MATHLIB_H__ */
