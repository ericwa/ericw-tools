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

#include <common/cmdlib.h>
#include <common/mathlib.h>

vec3_t vec3_origin = { 0, 0, 0 };


double
VectorLength(const vec3_t v)
{
    int i;
    double length;

    length = 0;
    for (i = 0; i < 3; i++)
	length += v[i] * v[i];
    length = sqrt(length);

    return length;
}

qboolean
VectorCompare(const vec3_t v1, const vec3_t v2)
{
    int i;

    for (i = 0; i < 3; i++)
	if (fabs(v1[i] - v2[i]) > EQUAL_EPSILON)
	    return false;

    return true;
}

void
CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross)
{
    cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
    cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
    cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
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
	v[i] /= (vec_t)length;

    return (vec_t)length;
}
