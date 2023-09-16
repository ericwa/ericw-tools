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

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

// Calculate average of inputs
template<typename... T>
constexpr auto avg(T &&...args)
{
    return (args + ...) / sizeof...(args);
}

using vec_t = double;

constexpr vec_t VECT_MAX = std::numeric_limits<vec_t>::max();

/*
 * The quality of the bsp output is highly sensitive to these epsilon values.
 */
constexpr vec_t ZERO_TRI_AREA_EPSILON = 0.0001;
constexpr vec_t POINT_EQUAL_EPSILON = 0.05;

constexpr vec_t NORMAL_EPSILON = 0.000001;
constexpr vec_t DIST_EPSILON = 0.0001;
constexpr vec_t DEGREES_EPSILON = 0.001;
constexpr vec_t DEFAULT_ON_EPSILON = 0.1;

/*
* The quality of the bsp output is highly sensitive to these epsilon values.
* Notes:
* - some calculations are sensitive to errors and need the various
*   epsilons to be such that QBSP_EQUAL_EPSILON < CONTINUOUS_EPSILON.
*     ( TODO: re-check if CONTINUOUS_EPSILON is still directly related )
*/
constexpr vec_t ANGLEEPSILON = NORMAL_EPSILON;
constexpr vec_t ZERO_EPSILON = DIST_EPSILON;
constexpr vec_t QBSP_EQUAL_EPSILON = DIST_EPSILON;
constexpr vec_t CONTINUOUS_EPSILON = 0.0005;

enum planeside_t : int8_t
{
    SIDE_FRONT,
    SIDE_BACK,
    SIDE_ON,
    SIDE_TOTAL,

    SIDE_CROSS = -2
};

constexpr vec_t Q_PI = 3.14159265358979323846;

constexpr vec_t DEG2RAD(vec_t a)
{
    return a * ((2 * Q_PI) / 360.0);
}

template<typename T>
inline T Q_rint(T in)
{
    return (T)(floor(in + 0.5));
}

/*
   Random()
   returns a pseudorandom number between 0 and 1
 */

vec_t Random();

// noramlizes the given pdf so it sums to 1, then converts to a cdf
std::vector<float> MakeCDF(const std::vector<float> &pdf);

int SampleCDF(const std::vector<float> &cdf, float sample);

// filtering

// width (height) are the filter "radius" (not "diameter")
float Filter_Gaussian(float width, float height, float x, float y);

// sqrt(x^2 + y^2) should be <= a, returns 0 outside that range.
float Lanczos2D(float x, float y, float a);

// mix a value such that 0 == a and 1 == b
template<typename T, typename F>
constexpr T mix(const T &a, const T &b, F frac)
{
    return (a * (static_cast<F>(1.0) - frac)) + (b * frac);
}
