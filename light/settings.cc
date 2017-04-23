/*  Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 2016 Eric Wasylishen
 
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

#include <light/settings.hh>

glm::vec3 vec_from_mangle(const glm::vec3 &m)
{
    const glm::vec3 tmp = m * static_cast<float>(Q_PI / 180.0f);

    const glm::vec3 v(cos(tmp[0]) * cos(tmp[1]),
                      sin(tmp[0]) * cos(tmp[1]),
                      sin(tmp[1]));
    return v;
}

glm::vec3 mangle_from_vec(const glm::vec3 &v)
{
    const glm::vec3 up(0, 0, 1);
    const glm::vec3 east(1, 0, 0);
    const glm::vec3 north(0, 1, 0);
    
    // get rotation about Z axis
    float x = glm::dot(east, v);
    float y = glm::dot(north, v);
    float theta = atan2f(y, x);
    
    // get angle away from Z axis
    float cosangleFromUp = glm::dot(up, v);
    cosangleFromUp = qmin(qmax(-1.0f, cosangleFromUp), 1.0f);
    float radiansFromUp = acosf(cosangleFromUp);
    
    const glm::vec3 mangle = glm::vec3(theta, -(radiansFromUp - Q_PI/2.0), 0) * static_cast<float>(180.0f / Q_PI);
    return mangle;
}

/* detect colors with components in 0-1 and scale them to 0-255 */
void
normalize_color_format(vec3_t color)
{
    if (color[0] >= 0 && color[0] <= 1 &&
        color[1] >= 0 && color[1] <= 1 &&
        color[2] >= 0 && color[2] <= 1)
    {
        VectorScale(color, 255, color);
    }
}
