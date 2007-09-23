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
// mathlib.c -- math primitives

#include "qbsp.h"

vec_t
VectorLengthSq(const vec3_t v)
{
    int i;
    vec_t lensq;

    lensq = 0;
    for (i = 0; i < 3; i++)
	lensq += v[i] * v[i];

    return lensq;
}

vec_t
VectorLength(const vec3_t v)
{
    return sqrt(VectorLengthSq(v));
}

bool
VectorCompare(const vec3_t v1, const vec3_t v2)
{
    int i;

    for (i = 0; i < 3; i++)
	if (fabs(v1[i] - v2[i]) > EQUAL_EPSILON)
	    return false;

    return true;
}

vec_t
Q_rint(vec_t in)
{
    return floor(in + 0.5);
}

void
VectorMA(const vec3_t va, const double scale, const vec3_t vb, vec3_t vc)
{
    vc[0] = va[0] + scale * vb[0];
    vc[1] = va[1] + scale * vb[1];
    vc[2] = va[2] + scale * vb[2];
}

void
CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross)
{
    cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
    cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
    cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

vec_t
DotProduct(const vec3_t v1, const vec3_t v2)
{
    return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}

void
VectorSubtract(const vec3_t va, const vec3_t vb, vec3_t out)
{
    out[0] = va[0] - vb[0];
    out[1] = va[1] - vb[1];
    out[2] = va[2] - vb[2];
}

void
VectorAdd(const vec3_t va, const vec3_t vb, vec3_t out)
{
    out[0] = va[0] + vb[0];
    out[1] = va[1] + vb[1];
    out[2] = va[2] + vb[2];
}

void
VectorCopy(const vec3_t in, vec3_t out)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
}

vec_t
VectorNormalize(vec3_t v)
{
    int i;
    double length;

    length = 0;
    for (i = 0; i < 3; i++)
	length += v[i] * v[i];
    length = sqrt(length);
    if (length == 0)
	return 0;

    for (i = 0; i < 3; i++)
	v[i] /= length;

    return length;
}

void
VectorInverse(vec3_t v)
{
    v[0] = -v[0];
    v[1] = -v[1];
    v[2] = -v[2];
}

void
VectorScale(const vec3_t v, const vec_t scale, vec3_t out)
{
    out[0] = v[0] * scale;
    out[1] = v[1] * scale;
    out[2] = v[2] * scale;
}
