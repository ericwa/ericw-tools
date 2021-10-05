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
#include <common/cmdlib.hh>
#include <vector>
#include <set>
#include <array>
#include <utility>
#include <memory> // for unique_ptr
#include <string>

#include <common/qvec.hh>

using vec_t = double;
using vec3_t = vec_t[3];
constexpr vec_t VECT_MAX = std::numeric_limits<vec_t>::max();

struct plane_t
{
    qvec3d normal;
    vec_t dist;
};

enum side_t : int8_t
{
    SIDE_FRONT,
    SIDE_BACK,
    SIDE_ON,

    SIDE_CROSS = -2
};

constexpr vec_t Q_PI = 3.14159265358979323846;

constexpr vec_t DEG2RAD(vec_t a)
{
    return a * ((2 * Q_PI) / 360.0);
}

extern const vec3_t vec3_origin;

#define ZERO_TRI_AREA_EPSILON 0.05f
#define POINT_EQUAL_EPSILON 0.05f

#define NORMAL_EPSILON 0.000001
#define DEGREES_EPSILON 0.001
constexpr vec_t DEFAULT_ON_EPSILON = 0.1;

template<typename T1, typename T2>
constexpr bool VectorCompare(const T1 &v1, const T2 &v2, vec_t epsilon)
{
    for (size_t i = 0; i < std::size(v1); i++)
        if (fabs(v1[i] - v2[i]) > epsilon)
            return false;

    return true;
}

template<typename T, typename T2, typename T3>
constexpr void CrossProduct(const T &v1, const T2 &v2, T3 &cross)
{
    // static_assert(std::size(v1) == 3);

    cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
    cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
    cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

template<typename Tx, typename Ty>
constexpr vec_t DotProduct(const Tx &x, const Ty &y)
{
    // static_assert(std::size(x) == 3);

    return x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
}

template<typename Tx, typename Ty, typename Tout>
constexpr void VectorSubtract(const Tx &x, const Ty &y, Tout &out)
{
    // static_assert(std::size(x) == 3);

    out[0] = x[0] - y[0];
    out[1] = x[1] - y[1];
    out[2] = x[2] - y[2];
}

template<typename Tx, typename Ty, typename Tout>
constexpr void VectorAdd(const Tx &x, const Ty &y, Tout &out)
{
    // static_assert(std::size(x) == 3);

    out[0] = x[0] + y[0];
    out[1] = x[1] + y[1];
    out[2] = x[2] + y[2];
}

template<typename TFrom, typename TTo>
constexpr void VectorCopy(const TFrom &in, TTo &out)
{
    // static_assert(std::size(in) == 3 && std::size(out) == 3);

    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
}

template<typename TFrom, typename TScale, typename TTo>
constexpr void VectorScale(const TFrom &v, TScale scale, TTo &out)
{
    // static_assert(std::size(v) == 3);

    out[0] = v[0] * scale;
    out[1] = v[1] * scale;
    out[2] = v[2] * scale;
}

template<typename T>
constexpr void VectorInverse(T &v)
{
    // static_assert(std::size(v) == 3);

    v[0] = -v[0];
    v[1] = -v[1];
    v[2] = -v[2];
}

template<typename T>
constexpr void VectorSet(T &out, vec_t x, vec_t y, vec_t z)
{
    // static_assert(std::size(out) == 3);

    out[0] = x;
    out[1] = y;
    out[2] = z;
}

template<typename T>
constexpr void VectorClear(T &out)
{
    // static_assert(std::size(out) == 3);

    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
}

void ClearBounds(vec3_t mins, vec3_t maxs);
void AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs);

plane_t FlipPlane(plane_t input);

#define Q_rint rint

/*
   Random()
   returns a pseudorandom number between 0 and 1
 */

inline vec_t Random(void)
{
    return (vec_t)rand() / RAND_MAX;
}

template<typename Ta, typename Tb, typename Tc>
constexpr void VectorMA(const Ta &va, vec_t scale, const Tb &vb, Tc &vc)
{
    // static_assert(std::size(vc) == 3);

    vc[0] = va[0] + scale * vb[0];
    vc[1] = va[1] + scale * vb[1];
    vc[2] = va[2] + scale * vb[2];
}

template<typename T>
constexpr vec_t VectorLengthSq(const T &v)
{
    // static_assert(std::size(v) == 3);

    vec_t length = 0;
    for (int i = 0; i < 3; i++)
        length += v[i] * v[i];
    return length;
}

template<typename T>
inline vec_t VectorLength(const T &v)
{
    // static_assert(std::size(v) == 3);

    vec_t length = VectorLengthSq(v);
    length = sqrt(length);
    return length;
}

template<typename T>
inline vec_t VectorNormalize(T &v)
{
    // static_assert(std::size(v) == 3);

    vec_t length = 0;
    for (size_t i = 0; i < 3; i++)
        length += v[i] * v[i];
    length = sqrt(length);
    if (length == 0)
        return 0;

    for (size_t i = 0; i < 3; i++)
        v[i] /= (vec_t)length;

    return (vec_t)length;
}

// returns the normalized direction from `start` to `stop` in the `dir` param
// returns the distance from `start` to `stop`
template<typename Tstart, typename Tstop, typename Tdir>
inline vec_t GetDir(const Tstart &start, const Tstop &stop, Tdir &dir)
{
    // static_assert(std::size(dir) == 3);

    VectorSubtract(stop, start, dir);
    return VectorNormalize(dir);
}

template<typename T>
constexpr vec_t DistanceAbovePlane(const T &normal, const vec_t dist, const T &point)
{
    // static_assert(std::size(normal) == 3);

    return DotProduct(normal, point) - dist;
}

template<typename T>
constexpr void ProjectPointOntoPlane(const T &normal, const vec_t dist, T &point)
{
    vec_t distAbove = DistanceAbovePlane(normal, dist, point);
    vec3_t move;
    VectorScale(normal, -distAbove, move);
    VectorAdd(point, move, point);
}

bool SetPlanePts(const std::array<qvec3d, 3> &planepts, qvec3d &normal, vec_t &dist);

// Maps uniform random variables U and V in [0, 1] to uniformly distributed points on a sphere
void UniformPointOnSphere(vec3_t dir, float u, float v);
void RandomDir(vec3_t dir);
qvec3f CosineWeightedHemisphereSample(float u1, float u2);
qvec3f vec_from_mangle(const qvec3f &m);
qvec3f mangle_from_vec(const qvec3f &v);
qmat3x3d RotateAboutX(double radians);
qmat3x3d RotateAboutY(double radians);
qmat3x3d RotateAboutZ(double radians);
qmat3x3f RotateFromUpToSurfaceNormal(const qvec3f &surfaceNormal);

using tri_t = std::tuple<qvec3f, qvec3f, qvec3f>;

/// abc - clockwise ordered triangle
/// p - point to get the barycentric coords of
qvec3f Barycentric_FromPoint(const qvec3f &p, const tri_t &tri);
qvec3f Barycentric_Random(const float r1, const float r2);

/// Evaluates the given barycentric coord for the given triangle
qvec3f Barycentric_ToPoint(const qvec3f &bary, const tri_t &tri);

vec_t TriangleArea(const vec3_t v0, const vec3_t v1, const vec3_t v2);

// noramlizes the given pdf so it sums to 1, then converts to a cdf
std::vector<float> MakeCDF(const std::vector<float> &pdf);

int SampleCDF(const std::vector<float> &cdf, float sample);

// filtering

// width (height) are the filter "radius" (not "diameter")
float Filter_Gaussian(float width, float height, float x, float y);

// sqrt(x^2 + y^2) should be <= a, returns 0 outside that range.
float Lanczos2D(float x, float y, float a);

// Returns (0 0 0) if we couldn't determine the normal
qvec3f GLM_FaceNormal(std::vector<qvec3f> points);
std::pair<bool, qvec4f> GLM_MakeInwardFacingEdgePlane(const qvec3f &v0, const qvec3f &v1, const qvec3f &faceNormal);
std::vector<qvec4f> GLM_MakeInwardFacingEdgePlanes(const std::vector<qvec3f> &points);
bool GLM_EdgePlanes_PointInside(const std::vector<qvec4f> &edgeplanes, const qvec3f &point);
float GLM_EdgePlanes_PointInsideDist(const std::vector<qvec4f> &edgeplanes, const qvec3f &point);
qvec3f GLM_TriangleCentroid(const qvec3f &v0, const qvec3f &v1, const qvec3f &v2);
float GLM_TriangleArea(const qvec3f &v0, const qvec3f &v1, const qvec3f &v2);
qvec4f GLM_MakePlane(const qvec3f &normal, const qvec3f &point);
float GLM_DistAbovePlane(const qvec4f &plane, const qvec3f &point);
qvec3f GLM_ProjectPointOntoPlane(const qvec4f &plane, const qvec3f &point);
float GLM_PolyArea(const std::vector<qvec3f> &points);
qvec3f GLM_PolyCentroid(const std::vector<qvec3f> &points);
qvec4f GLM_PolyPlane(const std::vector<qvec3f> &points);
/// Returns the index of the polygon edge, and the closest point on that edge, to the given point
std::pair<int, qvec3f> GLM_ClosestPointOnPolyBoundary(const std::vector<qvec3f> &poly, const qvec3f &point);
/// Returns `true` and the interpolated normal if `point` is in the polygon, otherwise returns false.
std::pair<bool, qvec3f> GLM_InterpolateNormal(
    const std::vector<qvec3f> &points, const std::vector<qvec3f> &normals, const qvec3f &point);
std::vector<qvec3f> GLM_ShrinkPoly(const std::vector<qvec3f> &poly, const float amount);
/// Returns (front part, back part)
std::pair<std::vector<qvec3f>, std::vector<qvec3f>> GLM_ClipPoly(const std::vector<qvec3f> &poly, const qvec4f &plane);

class poly_random_point_state_t
{
public:
    std::vector<qvec3f> points;
    std::vector<float> triareas;
    std::vector<float> triareas_cdf;
};

poly_random_point_state_t GLM_PolyRandomPoint_Setup(const std::vector<qvec3f> &points);
qvec3f GLM_PolyRandomPoint(const poly_random_point_state_t &state, float r1, float r2, float r3);

/// projects p onto the vw line.
/// returns 0 for p==v, 1 for p==w
float FractionOfLine(const qvec3f &v, const qvec3f &w, const qvec3f &p);

/**
 * Distance from `p` to the line v<->w (extending infinitely in either direction)
 */
float DistToLine(const qvec3f &v, const qvec3f &w, const qvec3f &p);

qvec3f ClosestPointOnLine(const qvec3f &v, const qvec3f &w, const qvec3f &p);

/**
 * Distance from `p` to the line segment v<->w.
 * i.e., 0 if `p` is between v and w.
 */
float DistToLineSegment(const qvec3f &v, const qvec3f &w, const qvec3f &p);

qvec3f ClosestPointOnLineSegment(const qvec3f &v, const qvec3f &w, const qvec3f &p);

float SignedDegreesBetweenUnitVectors(const qvec3f &start, const qvec3f &end, const qvec3f &normal);

enum class concavity_t
{
    Coplanar,
    Concave,
    Convex
};

concavity_t FacePairConcavity(
    const qvec3f &face1Center, const qvec3f &face1Normal, const qvec3f &face2Center, const qvec3f &face2Normal);

// Returns weights for f(0,0), f(1,0), f(0,1), f(1,1)
// from: https://en.wikipedia.org/wiki/Bilinear_interpolation#Unit_Square
inline qvec4f bilinearWeights(const float x, const float y)
{
    Q_assert(x >= 0.0f);
    Q_assert(x <= 1.0f);

    Q_assert(y >= 0.0f);
    Q_assert(y <= 1.0f);

    return qvec4f((1.0f - x) * (1.0f - y), x * (1.0f - y), (1.0f - x) * y, x * y);
}

// This uses a coordinate system where the pixel centers are on integer coords.
// e.g. the corners of a 3x3 pixel bitmap are at (-0.5, -0.5) and (2.5, 2.5).
inline std::array<std::pair<qvec2i, float>, 4> bilinearWeightsAndCoords(qvec2f pos, const qvec2i &size)
{
    Q_assert(pos[0] >= -0.5f && pos[0] <= (size[0] - 0.5f));
    Q_assert(pos[1] >= -0.5f && pos[1] <= (size[1] - 0.5f));

    // Handle extrapolation.
    for (int i = 0; i < 2; i++) {
        if (pos[i] < 0)
            pos[i] = 0;

        if (pos[i] > (size[i] - 1))
            pos[i] = (size[i] - 1);
    }

    Q_assert(pos[0] >= 0.f && pos[0] <= (size[0] - 1));
    Q_assert(pos[1] >= 0.f && pos[1] <= (size[1] - 1));

    qvec2i integerPart{static_cast<int>(qv::floor(pos)[0]), static_cast<int>(qv::floor(pos)[1])};
    qvec2f fractionalPart(pos - qv::floor(pos));

    // ensure integerPart + (1, 1) is still in bounds
    for (int i = 0; i < 2; i++) {
        if (fractionalPart[i] == 0.0f && integerPart[i] > 0) {
            integerPart[i] -= 1;
            fractionalPart[i] = 1.0f;
        }
    }
    Q_assert(integerPart[0] + 1 < size[0]);
    Q_assert(integerPart[1] + 1 < size[1]);

    Q_assert(qvec2f(integerPart) + fractionalPart == pos);

    // f(0,0), f(1,0), f(0,1), f(1,1)
    const qvec4f weights = bilinearWeights(fractionalPart[0], fractionalPart[1]);

    std::array<std::pair<qvec2i, float>, 4> result;
    for (int i = 0; i < 4; i++) {
        const float weight = weights[i];
        qvec2i pos(integerPart);

        if ((i % 2) == 1)
            pos[0] += 1;
        if (i >= 2)
            pos[1] += 1;

        Q_assert(pos[0] >= 0);
        Q_assert(pos[0] < size[0]);

        Q_assert(pos[1] >= 0);
        Q_assert(pos[1] < size[1]);

        result[i] = std::make_pair(pos, weight);
    }
    return result;
}

template<typename V>
V bilinearInterpolate(const V &f00, const V &f10, const V &f01, const V &f11, const float x, const float y)
{
    qvec4f weights = bilinearWeights(x, y);

    const V fxy = f00 * weights[0] + f10 * weights[1] + f01 * weights[2] + f11 * weights[3];

    return fxy;
}

template<typename V>
std::vector<V> PointsAlongLine(const V &start, const V &end, const float step)
{
    const V linesegment = end - start;
    const float len = qv::length(linesegment);
    if (len == 0)
        return {};

    std::vector<V> result;
    result.reserve(stepCount + 1);
    const V dir = linesegment / len;
    const int stepCount = static_cast<int>(len / step);
    for (int i = 0; i <= stepCount; i++) {
        result.push_back(start + (dir * (step * i)));
    }
    return result;
}

bool LinesOverlap(const qvec3f &p0, const qvec3f &p1, const qvec3f &q0, const qvec3f &q1,
    const vec_t &on_epsilon = DEFAULT_ON_EPSILON);
