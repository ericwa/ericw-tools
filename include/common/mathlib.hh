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

#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

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

#define EQUAL_EPSILON 0.001

#define ZERO_TRI_AREA_EPSILON 0.05f
#define POINT_EQUAL_EPSILON 0.05f

qboolean VectorCompare(const vec3_t v1, const vec3_t v2);

static inline bool
GLMVectorCompare(const glm::vec3 &v1, const glm::vec3 &v2)
{
    for (int i = 0; i < 3; i++)
        if (fabs(v1[i] - v2[i]) > EQUAL_EPSILON)
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
VectorCopyFromGLM(const glm::vec3 &in, vec3_t out)
{
    out[0] = in.x;
    out[1] = in.y;
    out[2] = in.z;
}

static inline glm::vec3
VectorToGLM(const vec3_t in)
{
    return glm::vec3(in[0], in[1], in[2]);
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
    
/* Shortcut for output of warnings/errors */
//FIXME: change from static buffers to returning std::string for thread safety
const char *VecStr(const vec3_t vec);
const char *VecStrf(const vec3_t vec);

// Maps uniform random variables U and V in [0, 1] to uniformly distributed points on a sphere
void UniformPointOnSphere(vec3_t dir, float u, float v);
void RandomDir(vec3_t dir);
bool AABBsDisjoint(const vec3_t minsA, const vec3_t maxsA, const vec3_t minsB, const vec3_t maxsB);
void AABB_Init(vec3_t mins, vec3_t maxs, const vec3_t pt);
void AABB_Expand(vec3_t mins, vec3_t maxs, const vec3_t pt);
void AABB_Size(const vec3_t mins, const vec3_t maxs, vec3_t size_out);
void AABB_Grow(vec3_t mins, vec3_t maxs, const vec3_t size);

template <class V>
class aabb {
private:
    V m_mins, m_maxs;
    
public:
    aabb() : m_mins(FLT_MAX), m_maxs(-FLT_MAX) {}
    aabb(const V &mins, const V &maxs) : m_mins(mins), m_maxs(maxs) {}
    aabb(const aabb<V> &other) : m_mins(other.m_mins), m_maxs(other.m_maxs) {}
    
    int length() const { return m_mins.length(); }
    
    bool disjoint(const aabb<V> &other) const {
        for (int i=0; i<length(); i++) {
            if (m_maxs[i] < other.m_mins[i]) return true;
            if (m_mins[i] > other.m_maxs[i]) return true;
        }
        return false;
    }
    
    bool contains(const V &p) const {
        for (int i=0; i<length(); i++) {
            if (!(p[i] >= m_mins[i] && p[i] <= m_maxs[i])) return false;
        }
        return true;
    }
    
    aabb<V> expand(const V &pt) const {
        V mins, maxs;
        for (int i=0; i<length(); i++) {
            mins[i] = qmin(m_mins[i], pt[i]);
            maxs[i] = qmax(m_maxs[i], pt[i]);
        }
        return aabb<V>(mins, maxs);
    }
    
    V size() const {
        V result;
        for (int i=0; i<length(); i++) {
            result[i] = m_maxs[i] - m_mins[i];
        }
        return result;
    }
    
    aabb<V> grow(const V &size) const {
        V mins = m_mins;
        V maxs = m_maxs;
        for (int i=0; i<length(); i++) {
            mins[i] -= size[i];
            maxs[i] += size[i];
        }
        return aabb<V>(mins, maxs);
    }
};

using aabb3 = aabb<glm::vec3>;
using aabb2 = aabb<glm::vec2>;

using tri_t = std::tuple<glm::vec3, glm::vec3, glm::vec3>;

/// abc - clockwise ordered triangle
/// p - point to get the barycentric coords of
glm::vec3 Barycentric_FromPoint(const glm::vec3 &p, const tri_t &tri);
glm::vec3 Barycentric_Random(const float r1, const float r2);

/// Evaluates the given barycentric coord for the given triangle
glm::vec3 Barycentric_ToPoint(const glm::vec3 &bary,
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

static inline glm::vec3 vec3_t_to_glm(const vec3_t vec) {
    return glm::vec3(vec[0], vec[1], vec[2]);
}

static inline void glm_to_vec3_t(const glm::vec3 &glm, vec3_t out) {
    out[0] = glm.x;
    out[1] = glm.y;
    out[2] = glm.z;
}

glm::vec3 GLM_FaceNormal(std::vector<glm::vec3> points);
std::pair<bool, glm::vec4> GLM_MakeInwardFacingEdgePlane(const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &faceNormal);
std::vector<glm::vec4> GLM_MakeInwardFacingEdgePlanes(std::vector<glm::vec3> points);
bool GLM_EdgePlanes_PointInside(const std::vector<glm::vec4> &edgeplanes, const glm::vec3 &point);
float GLM_EdgePlanes_PointInsideDist(const std::vector<glm::vec4> &edgeplanes, const glm::vec3 &point);
glm::vec3 GLM_TriangleCentroid(const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2);
float GLM_TriangleArea(const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2);
float GLM_DistAbovePlane(const glm::vec4 &plane, const glm::vec3 &point);
glm::vec3 GLM_PolyCentroid(const std::vector<glm::vec3> &points);
glm::vec4 GLM_PolyPlane(const std::vector<glm::vec3> &points);
/// Returns the index of the polygon edge, and the closest point on that edge, to the given point
std::pair<int, glm::vec3> GLM_ClosestPointOnPolyBoundary(const std::vector<glm::vec3> &poly, const glm::vec3 &point);
/// Returns `true` and the interpolated normal if `point` is in the polygon, otherwise returns false.
std::pair<bool, glm::vec3> GLM_InterpolateNormal(const std::vector<glm::vec3> &points,
                                                 const std::vector<glm::vec3> &normals,
                                                 const glm::vec3 &point);
std::vector<glm::vec3> GLM_ShrinkPoly(const std::vector<glm::vec3> &poly, const float amount);
/// Returns (front part, back part)
std::pair<std::vector<glm::vec3>,std::vector<glm::vec3>> GLM_ClipPoly(const std::vector<glm::vec3> &poly, const glm::vec4 &plane);

#endif /* __COMMON_MATHLIB_H__ */
