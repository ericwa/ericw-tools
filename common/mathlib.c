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
#include <assert.h>

const vec3_t vec3_origin = { 0, 0, 0 };

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

/*
 * VecStr - handy shortcut for printf, not thread safe, obviously
 */
const char *
VecStr(const vec3_t vec)
{
    static char buffers[8][20];
    static int current = 0;
    char *buf;

    buf = buffers[current++ & 7];
    q_snprintf(buf, sizeof(buffers[0]), "%i %i %i",
             (int)vec[0], (int)vec[1], (int)vec[2]);

    return buf;
}

const char *
VecStrf(const vec3_t vec)
{
    static char buffers[8][20];
    static int current = 0;
    char *buf;

    buf = buffers[current++ & 7];
    q_snprintf(buf, sizeof(buffers[0]), "%.2f %.2f %.2f",
             vec[0], vec[1], vec[2]);

    return buf;
}

// from http://mathworld.wolfram.com/SpherePointPicking.html
// eqns 6,7,8
void
RandomDir(vec3_t dir)
{
    const vec_t theta = Random() * 2.0 * Q_PI;
    const vec_t u = (2.0 * Random()) - 1.0;
    
    const vec_t s = sqrt(1.0 - (u * u));
    dir[0] = s * cos(theta);
    dir[1] = s * sin(theta);
    dir[2] = u;
    
    for (int i=0; i<3; i++) {
        assert(dir[i] >= -1.001);
        assert(dir[i] <=  1.001);
    }
}
