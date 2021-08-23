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

#include <float.h>
#include <math.h>
#include <common/cmdlib.hh>
#include <vector>
#include <set>
#include <array>
#include <utility>
#include <memory> // for unique_ptr
#include <string>

#include <common/qvec.hh>

#ifdef DOUBLEVEC_T
#define vec_t double
#define VECT_MAX DBL_MAX
#else
#define vec_t float
#define VECT_MAX FLT_MAX
#endif
typedef vec_t vec3_t[3];

typedef struct {
    vec3_t normal;
    vec_t dist;
} plane_t;
    
#define SIDE_FRONT  0
#define SIDE_ON     2
#define SIDE_BACK   1
#define SIDE_CROSS -2

#define Q_PI 3.14159265358979323846

#define DEG2RAD( a ) ( ( a ) * ( ( 2 * Q_PI ) / 360.0 ) )

extern const vec3_t vec3_origin;

#define ZERO_TRI_AREA_EPSILON 0.05f
#define POINT_EQUAL_EPSILON 0.05f

#define NORMAL_EPSILON          0.000001
#define DEGREES_EPSILON		0.001

qboolean VectorCompare(const vec3_t v1, const vec3_t v2, vec_t epsilon);

static inline bool
GLMVectorCompare(const qvec3f &v1, const qvec3f &v2, float epsilon)
{
    for (int i = 0; i < 3; i++)
        if (fabs(v1[i] - v2[i]) > epsilon)
            return false;
    return true;
}

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

#ifdef DOUBLEVEC_T
static inline void
VectorCopy(const float in[3], vec3_t out)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
}
#endif

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

static inline void
VectorSet(vec3_t out, vec_t x, vec_t y, vec_t z)
{
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

static inline void
VectorClear(vec3_t out)
{
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
}

static inline void
VectorCopyFromGLM(const qvec3f &in, vec3_t out)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
}

void ClearBounds(vec3_t mins, vec3_t maxs);
void AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs);

plane_t FlipPlane(plane_t input);

static inline qvec3f
VectorToGLM(const vec3_t in)
{
    return qvec3f(in[0], in[1], in[2]);
}

static inline vec_t
Q_rint(vec_t in)
{
    return (vec_t)(floor(in + 0.5));
}

/*
   Random()
   returns a pseudorandom number between 0 and 1
 */

static inline vec_t
Random( void )
{
    return (vec_t) rand() / RAND_MAX;
}

static inline void
VectorMA(const vec3_t va, vec_t scale, const vec3_t vb, vec3_t vc)
{
    vc[0] = va[0] + scale * vb[0];
    vc[1] = va[1] + scale * vb[1];
    vc[2] = va[2] + scale * vb[2];
}


void CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross);

static inline double
VectorLengthSq(const vec3_t v)
{
    double length = 0;
    for (int i = 0; i < 3; i++)
        length += v[i] * v[i];
    return length;
}

static inline double
VectorLength(const vec3_t v)
{
    double length = VectorLengthSq(v);
    length = sqrt(length);
    return length;
}

static inline vec_t
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

// returns the normalized direction from `start` to `stop` in the `dir` param
// returns the distance from `start` to `stop`
static inline vec_t
GetDir(const vec3_t start, const vec3_t stop, vec3_t dir)
{
    VectorSubtract(stop, start, dir);
    return VectorNormalize(dir);
}

static inline vec_t
DistanceAbovePlane(const vec3_t normal, const vec_t dist, const vec3_t point)
{
    return DotProduct(normal, point) - dist;
}

static inline void
ProjectPointOntoPlane(const vec3_t normal, const vec_t dist, vec3_t point)
{
    vec_t distAbove = DistanceAbovePlane(normal, dist, point);
    vec3_t move;
    VectorScale(normal, -distAbove, move);
    VectorAdd(point, move, point);
}

bool SetPlanePts(const vec3_t planepts[3], vec3_t normal, vec_t *dist);

/* Shortcut for output of warnings/errors */
std::string VecStr(const vec3_t vec);
std::string VecStrf(const vec3_t vec);
std::string VecStr(const qvec3f vec); //mxd
std::string VecStrf(const qvec3f vec); //mxd

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

bool AABBsDisjoint(const vec3_t minsA, const vec3_t maxsA, const vec3_t minsB, const vec3_t maxsB);
void AABB_Init(vec3_t mins, vec3_t maxs, const vec3_t pt);
void AABB_Expand(vec3_t mins, vec3_t maxs, const vec3_t pt);
void AABB_Size(const vec3_t mins, const vec3_t maxs, vec3_t size_out);
void AABB_Grow(vec3_t mins, vec3_t maxs, const vec3_t size);

using tri_t = std::tuple<qvec3f, qvec3f, qvec3f>;

/// abc - clockwise ordered triangle
/// p - point to get the barycentric coords of
qvec3f Barycentric_FromPoint(const qvec3f &p, const tri_t &tri);
qvec3f Barycentric_Random(const float r1, const float r2);

/// Evaluates the given barycentric coord for the given triangle
qvec3f Barycentric_ToPoint(const qvec3f &bary,
                              const tri_t &tri);

vec_t TriangleArea(const vec3_t v0, const vec3_t v1, const vec3_t v2);

// noramlizes the given pdf so it sums to 1, then converts to a cdf
std::vector<float> MakeCDF(const std::vector<float> &pdf);

int SampleCDF(const std::vector<float> &cdf, float sample);

// filtering

// width (height) are the filter "radius" (not "diameter")
float Filter_Gaussian(float width, float height, float x, float y);

// sqrt(x^2 + y^2) should be <= a, returns 0 outside that range.
float Lanczos2D(float x, float y, float a);

// glm geometry

static inline qvec3f vec3_t_to_glm(const vec3_t vec) {
    return qvec3f(vec[0], vec[1], vec[2]);
}

static inline qvec3d qvec3d_from_vec3(const vec3_t vec) {
    return qvec3d(vec[0], vec[1], vec[2]);
}

static inline void glm_to_vec3_t(const qvec3f &glm, vec3_t out) {
    out[0] = glm[0];
    out[1] = glm[1];
    out[2] = glm[2];
}

static inline void glm_to_vec3_t(const qvec3d &glm, vec3_t out) {
    out[0] = glm[0];
    out[1] = glm[1];
    out[2] = glm[2];
}


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
std::pair<bool, qvec3f> GLM_InterpolateNormal(const std::vector<qvec3f> &points,
                                                 const std::vector<qvec3f> &normals,
                                                 const qvec3f &point);
std::vector<qvec3f> GLM_ShrinkPoly(const std::vector<qvec3f> &poly, const float amount);
/// Returns (front part, back part)
std::pair<std::vector<qvec3f>,std::vector<qvec3f>> GLM_ClipPoly(const std::vector<qvec3f> &poly, const qvec4f &plane);

class poly_random_point_state_t {
public:
    std::vector<qvec3f> points;
    std::vector<float> triareas;
    std::vector<float> triareas_cdf;
};

poly_random_point_state_t GLM_PolyRandomPoint_Setup(const std::vector<qvec3f> &points);
qvec3f GLM_PolyRandomPoint(const poly_random_point_state_t &state, float r1, float r2, float r3);

/// projects p onto the vw line.
/// returns 0 for p==v, 1 for p==w
float FractionOfLine(const qvec3f &v, const qvec3f &w, const qvec3f& p);

/**
 * Distance from `p` to the line v<->w (extending infinitely in either direction)
 */
float DistToLine(const qvec3f &v, const qvec3f &w, const qvec3f& p);

qvec3f ClosestPointOnLine(const qvec3f &v, const qvec3f &w, const qvec3f &p);

/**
 * Distance from `p` to the line segment v<->w.
 * i.e., 0 if `p` is between v and w.
 */
float DistToLineSegment(const qvec3f &v, const qvec3f &w, const qvec3f &p);

qvec3f ClosestPointOnLineSegment(const qvec3f &v, const qvec3f &w, const qvec3f &p);

float SignedDegreesBetweenUnitVectors(const qvec3f &start, const qvec3f &end, const qvec3f &normal);

enum class concavity_t {
    Coplanar,
    Concave,
    Convex
};

concavity_t FacePairConcavity(const qvec3f &face1Center,
                      const qvec3f &face1Normal,
                      const qvec3f &face2Center,
                      const qvec3f &face2Normal);

// Returns weights for f(0,0), f(1,0), f(0,1), f(1,1)
// from: https://en.wikipedia.org/wiki/Bilinear_interpolation#Unit_Square
static inline qvec4f bilinearWeights(const float x, const float y) {
    Q_assert(x >= 0.0f);
    Q_assert(x <= 1.0f);
    
    Q_assert(y >= 0.0f);
    Q_assert(y <= 1.0f);
    
    return qvec4f((1.0f - x) * (1.0f - y), x * (1.0f - y), (1.0f - x) * y, x * y);
}

// This uses a coordinate system where the pixel centers are on integer coords.
// e.g. the corners of a 3x3 pixel bitmap are at (-0.5, -0.5) and (2.5, 2.5).
static inline std::array<std::pair<qvec2i, float>, 4>
bilinearWeightsAndCoords(qvec2f pos, const qvec2i &size)
{
    Q_assert(pos[0] >= -0.5f && pos[0] <= (size[0] - 0.5f));
    Q_assert(pos[1] >= -0.5f && pos[1] <= (size[1] - 0.5f));
    
    // Handle extrapolation.
    for (int i=0; i<2; i++) {
        if (pos[i] < 0)
            pos[i] = 0;
        
        if (pos[i] > (size[i] - 1))
            pos[i] = (size[i] - 1);
    }
    
    Q_assert(pos[0] >= 0.f && pos[0] <= (size[0] - 1));
    Q_assert(pos[1] >= 0.f && pos[1] <= (size[1] - 1));
    
    qvec2i integerPart{static_cast<int>(qv::floor(pos)[0]),
                       static_cast<int>(qv::floor(pos)[1])};
    qvec2f fractionalPart(pos - qv::floor(pos));
    
    // ensure integerPart + (1, 1) is still in bounds
    for (int i=0; i<2; i++) {
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
    for (int i=0; i<4; i++) {
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

template <typename V>
V bilinearInterpolate(const V &f00, const V &f10, const V &f01, const V &f11, const float x, const float y)
{
    qvec4f weights = bilinearWeights(x,y);
    
    const V fxy = f00 * weights[0] + \
                    f10 * weights[1] + \
                    f01 * weights[2] + \
                    f11 * weights[3];
    
    return fxy;
}

template <typename V>
std::vector<V> PointsAlongLine(const V &start, const V &end, const float step)
{
    const V linesegment = end - start;
    const float len = qv::length(linesegment);
    if (len == 0)
        return {};
    
    std::vector<V> result;
    const V dir = linesegment / len;
    const int stepCount = static_cast<int>(len / step);
    for (int i=0; i<=stepCount; i++) {
        const V pt = start + (dir * (step * i));
        result.push_back(pt);
    }
    return result;
}

bool LinesOverlap(const qvec3f p0, const qvec3f p1,
                  const qvec3f q0, const qvec3f q1);

#endif /* __COMMON_MATHLIB_H__ */
