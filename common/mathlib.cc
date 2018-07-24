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

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/polylib.hh>
#include <assert.h>

#include <tuple>
#include <map>
#include <cmath>

#include <common/qvec.hh>

using namespace polylib;

const vec3_t vec3_origin = { 0, 0, 0 };

qboolean
VectorCompare(const vec3_t v1, const vec3_t v2, vec_t epsilon)
{
    int i;

    for (i = 0; i < 3; i++)
        if (fabs(v1[i] - v2[i]) > epsilon)
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

bool
SetPlanePts(const vec3_t planepts[3], vec3_t normal, vec_t *dist)
{
    vec3_t planevecs[2];
    
    /* calculate the normal/dist plane equation */
    VectorSubtract(planepts[0], planepts[1], planevecs[0]);
    VectorSubtract(planepts[2], planepts[1], planevecs[1]);
    
    CrossProduct(planevecs[0], planevecs[1], normal);
    vec_t length = VectorNormalize(normal);
    *dist = DotProduct(planepts[1], normal);
    
    if (length < NORMAL_EPSILON) {
        return false;
    }
    
    return true;
}

/*
 * VecStr - handy shortcut for printf
 */
std::string
VecStr(const vec3_t vec)
{
    char buf[128];

    q_snprintf(buf, sizeof(buf), "%i %i %i",
              (int)vec[0], (int)vec[1], (int)vec[2]);

    return buf;
}

std::string //mxd
VecStr(const qvec3f vec)
{
    vec3_t v;
    glm_to_vec3_t(vec, v);
    return VecStr(v);
}

std::string
VecStrf(const vec3_t vec)
{
    char buf[128];
    
    q_snprintf(buf, sizeof(buf), "%.2f %.2f %.2f",
             vec[0], vec[1], vec[2]);

    return buf;
}

std::string //mxd
VecStrf(const qvec3f vec)
{
    vec3_t v;
    glm_to_vec3_t(vec, v);
    return VecStrf(v);
}

void ClearBounds(vec3_t mins, vec3_t maxs)
{
    for (int i=0; i<3; i++) {
        mins[i] = VECT_MAX;
        maxs[i] = -VECT_MAX;
    }
}

void AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs)
{
    for (int i=0; i<3; i++) {
        const vec_t val = v[i];
        
        if (val < mins[i])
            mins[i] = val;
        if (val > maxs[i])
            maxs[i] = val;
    }
}

plane_t FlipPlane(plane_t input)
{
    plane_t result;
    
    VectorScale(input.normal, -1, result.normal);
    result.dist = -input.dist;
    
    return result;
}

// from http://mathworld.wolfram.com/SpherePointPicking.html
// eqns 6,7,8
void
UniformPointOnSphere(vec3_t dir, float u1, float u2)
{
    Q_assert(u1 >= 0 && u1 <= 1);
    Q_assert(u2 >= 0 && u2 <= 1);
    
    const vec_t theta = u1 * 2.0 * Q_PI;
    const vec_t u = (2.0 * u2) - 1.0;
    
    const vec_t s = sqrt(1.0 - (u * u));
    dir[0] = s * cos(theta);
    dir[1] = s * sin(theta);
    dir[2] = u;
    
    for (int i=0; i<3; i++) {
        Q_assert(dir[i] >= -1.001);
        Q_assert(dir[i] <=  1.001);
    }
}

void
RandomDir(vec3_t dir)
{
    UniformPointOnSphere(dir, Random(), Random());
}

qvec3f CosineWeightedHemisphereSample(float u1, float u2)
{
    Q_assert(u1 >= 0.0f && u1 <= 1.0f);
    Q_assert(u2 >= 0.0f && u2 <= 1.0f);
    
    // Generate a uniform sample on the unit disk
    // http://mathworld.wolfram.com/DiskPointPicking.html
    const float sqrt_u1 = sqrt(u1);
    const float theta = 2.0f * Q_PI * u2;
    
    const float x = sqrt_u1 * cos(theta);
    const float y = sqrt_u1 * sin(theta);
    
    // Project it up onto the sphere (calculate z)
    //
    // We know sqrt(x^2 + y^2 + z^2) = 1
    // so      x^2 + y^2 + z^2 = 1
    //         z = sqrt(1 - x^2 - y^2)
    
    const float temp = 1.0f - x*x - y*y;
    const float z = sqrt(qmax(0.0f, temp));
    
    return qvec3f(x, y, z);
}

qvec3f vec_from_mangle(const qvec3f &m)
{
    const qvec3f mRadians = m * static_cast<float>(Q_PI / 180.0f);
    const qmat3x3d rotations = RotateAboutZ(mRadians[0]) * RotateAboutY(-mRadians[1]);
    const qvec3f v = qvec3f(rotations * qvec3d(1,0,0));
    return v;
}

qvec3f mangle_from_vec(const qvec3f &v)
{
    const qvec3f up(0, 0, 1);
    const qvec3f east(1, 0, 0);
    const qvec3f north(0, 1, 0);
    
    // get rotation about Z axis
    float x = qv::dot(east, v);
    float y = qv::dot(north, v);
    float theta = atan2f(y, x);
    
    // get angle away from Z axis
    float cosangleFromUp = qv::dot(up, v);
    cosangleFromUp = qmin(qmax(-1.0f, cosangleFromUp), 1.0f);
    float radiansFromUp = acosf(cosangleFromUp);
    
    const qvec3f mangle = qvec3f(theta, -(radiansFromUp - Q_PI/2.0), 0) * static_cast<float>(180.0f / Q_PI);
    return mangle;
}

qmat3x3d RotateAboutX(double t)
{
    //https://en.wikipedia.org/wiki/Rotation_matrix#Examples

    const double cost = cos(t);
    const double sint = sin(t);
    
    return qmat3x3d {
        1, 0, 0, //col0
        0, cost, sint, // col1
        0, -sint, cost // col1
    };
}

qmat3x3d RotateAboutY(double t)
{
    const double cost = cos(t);
    const double sint = sin(t);
    
    return qmat3x3d {
        cost, 0, -sint, // col0
        0, 1, 0, // col1
        sint, 0, cost //col2
    };
}

qmat3x3d RotateAboutZ(double t)
{
    const double cost = cos(t);
    const double sint = sin(t);
    
    return qmat3x3d {
        cost, sint, 0, // col0
        -sint, cost, 0, // col1
        0, 0, 1 //col2
    };
}

// Returns a 3x3 matrix that rotates (0,0,1) to the given surface normal.
qmat3x3f RotateFromUpToSurfaceNormal(const qvec3f &surfaceNormal)
{
    const qvec3f up(0, 0, 1);
    const qvec3f east(1, 0, 0);
    const qvec3f north(0, 1, 0);
    
    // get rotation about Z axis
    float x = qv::dot(east, surfaceNormal);
    float y = qv::dot(north, surfaceNormal);
    float theta = atan2f(y, x);
    
    // get angle away from Z axis
    float cosangleFromUp = qv::dot(up, surfaceNormal);
    cosangleFromUp = qmin(qmax(-1.0f, cosangleFromUp), 1.0f);
    float radiansFromUp = acosf(cosangleFromUp);
    
    const qmat3x3d rotations = RotateAboutZ(theta) * RotateAboutY(radiansFromUp);
    return qmat3x3f(rotations);
}

// FIXME: remove these
bool AABBsDisjoint(const vec3_t minsA, const vec3_t maxsA,
                   const vec3_t minsB, const vec3_t maxsB)
{
    for (int i=0; i<3; i++) {
        if (maxsA[i] < (minsB[i] - 0.001)) return true;
        if (minsA[i] > (maxsB[i] + 0.001)) return true;
    }
    return false;
}

void AABB_Init(vec3_t mins, vec3_t maxs, const vec3_t pt) {
    VectorCopy(pt, mins);
    VectorCopy(pt, maxs);
}

void AABB_Expand(vec3_t mins, vec3_t maxs, const vec3_t pt) {
    for (int i=0; i<3; i++) {
        mins[i] = qmin(mins[i], pt[i]);
        maxs[i] = qmax(maxs[i], pt[i]);
    }
}

void AABB_Size(const vec3_t mins, const vec3_t maxs, vec3_t size_out) {
    for (int i=0; i<3; i++) {
        size_out[i] = maxs[i] - mins[i];
    }
}

void AABB_Grow(vec3_t mins, vec3_t maxs, const vec3_t size) {
    for (int i=0; i<3; i++) {
        mins[i] -= size[i];
        maxs[i] += size[i];
    }
}

qvec3f Barycentric_FromPoint(const qvec3f &p, const tri_t &tri)
{
    using std::get;
    
    const qvec3f v0 = get<1>(tri) - get<0>(tri);
    const qvec3f v1 = get<2>(tri) - get<0>(tri);
    const qvec3f v2 =           p - get<0>(tri);
    float d00 = qv::dot(v0, v0);
    float d01 = qv::dot(v0, v1);
    float d11 = qv::dot(v1, v1);
    float d20 = qv::dot(v2, v0);
    float d21 = qv::dot(v2, v1);
    float invDenom = (d00 * d11 - d01 * d01);
    invDenom = 1.0/invDenom;
    
    qvec3f res;
    res[1] = (d11 * d20 - d01 * d21) * invDenom;
    res[2] = (d00 * d21 - d01 * d20) * invDenom;
    res[0] = 1.0f - res[1] - res[2];
    return res;
}

// from global illumination total compendium p. 12
qvec3f Barycentric_Random(const float r1, const float r2)
{
    qvec3f res;
    res[0] = 1.0f - sqrtf(r1);
    res[1] = r2 * sqrtf(r1);
    res[2] = 1.0f - res[0] - res[1];
    return res;
}

/// Evaluates the given barycentric coord for the given triangle
qvec3f Barycentric_ToPoint(const qvec3f &bary,
                              const tri_t &tri)
{
    using std::get;
    
    const qvec3f pt = \
          (get<0>(tri) * bary[0])
        + (get<1>(tri) * bary[1])
        + (get<2>(tri) * bary[2]);
    
    return pt;
}


vec_t
TriangleArea(const vec3_t v0, const vec3_t v1, const vec3_t v2)
{
    vec3_t edge0, edge1, cross;
    VectorSubtract(v2, v0, edge0);
    VectorSubtract(v1, v0, edge1);
    CrossProduct(edge0, edge1, cross);
    
    return VectorLength(cross) * 0.5;
}

static std::vector<float>
NormalizePDF(const std::vector<float> &pdf)
{
    float pdfSum = 0.0f;
    for (float val : pdf) {
        pdfSum += val;
    }
    
    std::vector<float> normalizedPdf;
    normalizedPdf.reserve(pdf.size()); //mxd. https://clang.llvm.org/extra/clang-tidy/checks/performance-inefficient-vector-operation.html
    for (float val : pdf) {
        normalizedPdf.push_back(val / pdfSum);
    }
    return normalizedPdf;
}

std::vector<float> MakeCDF(const std::vector<float> &pdf)
{
    const std::vector<float> normzliedPdf = NormalizePDF(pdf);
    std::vector<float> cdf;
    float cdfSum = 0.0f;
    for (float val : normzliedPdf) {
        cdfSum += val;
        cdf.push_back(cdfSum);
    }
    return cdf;
}

int SampleCDF(const std::vector<float> &cdf, float sample)
{
    const size_t size = cdf.size();
    for (size_t i=0; i<size; i++) {
        float cdfVal = cdf.at(i);
        if (sample <= cdfVal) {
            return i;
        }
    }
    Q_assert_unreachable();
    return 0;
}

static float Gaussian1D(float width, float x, float alpha)
{
    if (fabs(x) > width)
        return 0.0f;
    
    return expf(-alpha * x * x) - expf(-alpha * width * width);
}

float Filter_Gaussian(float width, float height, float x, float y)
{
    const float alpha = 0.5;
    return Gaussian1D(width, x, alpha)
        * Gaussian1D(height, y, alpha);
}

// from https://en.wikipedia.org/wiki/Lanczos_resampling
static float Lanczos1D(float x, float a)
{
    if (x == 0)
        return 1;
    
    if (x < -a || x >= a)
        return 0;
    
    float lanczos = (a * sinf(Q_PI * x) * sinf(Q_PI * x / a)) / (Q_PI * Q_PI * x * x);
    return lanczos;
}

// from https://en.wikipedia.org/wiki/Lanczos_resampling#Multidimensional_interpolation
float Lanczos2D(float x, float y, float a)
{
    float dist = sqrtf((x*x) + (y*y));
    float lanczos = Lanczos1D(dist, a);
    return lanczos;
}

using namespace std;

qvec3f GLM_FaceNormal(std::vector<qvec3f> points)
{
    const int N = static_cast<int>(points.size());
    float maxArea = -FLT_MAX;
    int bestI = -1;
    
    const qvec3f p0 = points[0];
    
    for (int i=2; i<N; i++) {
        const qvec3f p1 = points[i-1];
        const qvec3f p2 = points[i];
        
        const float area = GLM_TriangleArea(p0, p1, p2);
        if (area > maxArea) {
            maxArea = area;
            bestI = i;
        }
    }
    
    if (bestI == -1 || maxArea < ZERO_TRI_AREA_EPSILON)
        return qvec3f(0);
    
    const qvec3f p1 = points[bestI-1];
    const qvec3f p2 = points[bestI];
    const qvec3f normal = qv::normalize(qv::cross(p2 - p0, p1 - p0));
    return normal;
}

qvec4f GLM_PolyPlane(const std::vector<qvec3f> &points)
{
    const qvec3f normal = GLM_FaceNormal(points);
    const float dist = qv::dot(points.at(0), normal);
    return qvec4f(normal[0], normal[1], normal[2], dist);
}

std::pair<bool, qvec4f>
GLM_MakeInwardFacingEdgePlane(const qvec3f &v0, const qvec3f &v1, const qvec3f &faceNormal)
{
    const float v0v1len = qv::length(v1-v0);
    if (v0v1len < POINT_EQUAL_EPSILON)
        return make_pair(false, qvec4f(0));
    
    const qvec3f edgedir = (v1 - v0) / v0v1len;
    const qvec3f edgeplane_normal = qv::cross(edgedir, faceNormal);
    const float edgeplane_dist = qv::dot(edgeplane_normal, v0);
    
    return make_pair(true, qvec4f(edgeplane_normal[0], edgeplane_normal[1], edgeplane_normal[2], edgeplane_dist));
}

vector<qvec4f>
GLM_MakeInwardFacingEdgePlanes(const std::vector<qvec3f> &points)
{
    const int N = points.size();
    if (N < 3)
        return {};
    
    vector<qvec4f> result;
    result.reserve(points.size());
    
    const qvec3f faceNormal = GLM_FaceNormal(points);
    
    if (faceNormal == qvec3f(0,0,0))
        return {};
    
    for (int i=0; i<N; i++)
    {
        const qvec3f v0 = points[i];
        const qvec3f v1 = points[(i+1) % N];
        
        const auto edgeplane = GLM_MakeInwardFacingEdgePlane(v0, v1, faceNormal);
        if (!edgeplane.first)
            continue;
        
        result.push_back(edgeplane.second);
    }
    
    return result;
}

float GLM_EdgePlanes_PointInsideDist(const std::vector<qvec4f> &edgeplanes, const qvec3f &point)
{
    float min = FLT_MAX;
    
    for (int i=0; i<edgeplanes.size(); i++) {
        const float planedist = GLM_DistAbovePlane(edgeplanes[i], point);
        if (planedist < min)
            min = planedist;
    }
    
    return min; // "outermost" point
}

bool
GLM_EdgePlanes_PointInside(const vector<qvec4f> &edgeplanes, const qvec3f &point)
{
    if (edgeplanes.empty())
        return false;
    
    const float minDist = GLM_EdgePlanes_PointInsideDist(edgeplanes, point);
    return minDist >= -POINT_EQUAL_EPSILON;
}

qvec3f
GLM_TriangleCentroid(const qvec3f &v0, const qvec3f &v1, const qvec3f &v2)
{
    return (v0 + v1 + v2) / 3.0f;
}

float
GLM_TriangleArea(const qvec3f &v0, const qvec3f &v1, const qvec3f &v2)
{
    return 0.5f * qv::length(qv::cross(v2 - v0, v1 - v0));
}

qvec4f GLM_MakePlane(const qvec3f &normal, const qvec3f &point)
{
    return qvec4f(normal[0], normal[1], normal[2], qv::dot(point, normal));
}

float GLM_DistAbovePlane(const qvec4f &plane, const qvec3f &point)
{
    return qv::dot(qvec3f(plane), point) - plane[3];
}

qvec3f GLM_ProjectPointOntoPlane(const qvec4f &plane, const qvec3f &point)
{
    float dist = GLM_DistAbovePlane(plane, point);
    qvec3f move = qvec3f(plane[0], plane[1], plane[2]) * -dist;
    return point + move;
}

float GLM_PolyArea(const std::vector<qvec3f> &points)
{
    Q_assert(points.size() >= 3);
    
    float poly_area = 0;
    
    const qvec3f v0 = points.at(0);
    for (int i = 2; i < points.size(); i++) {
        const qvec3f v1 = points.at(i-1);
        const qvec3f v2 = points.at(i);
        
        const float triarea = GLM_TriangleArea(v0, v1, v2);
        
        poly_area += triarea;
    }
    
    return poly_area;
}

qvec3f GLM_PolyCentroid(const std::vector<qvec3f> &points)
{
    if (points.size() == 0)
        return qvec3f(NAN);
    else if (points.size() == 1)
        return points.at(0);
    else if (points.size() == 2)
        return (points.at(0) + points.at(1)) / 2.0;

    Q_assert(points.size() >= 3);
    
    qvec3f poly_centroid(0);
    float poly_area = 0;
    
    const qvec3f v0 = points.at(0);
    for (int i = 2; i < points.size(); i++) {
        const qvec3f v1 = points.at(i-1);
        const qvec3f v2 = points.at(i);
        
        const float triarea = GLM_TriangleArea(v0, v1, v2);
        const qvec3f tricentroid = GLM_TriangleCentroid(v0, v1, v2);
        
        poly_area += triarea;
        poly_centroid = poly_centroid + (tricentroid * triarea);
    }
    
    poly_centroid /= poly_area;
    
    return poly_centroid;
}

poly_random_point_state_t GLM_PolyRandomPoint_Setup(const std::vector<qvec3f> &points)
{
    Q_assert(points.size() >= 3);

    float poly_area = 0;
    std::vector<float> triareas;
    
    const qvec3f v0 = points.at(0);
    for (int i = 2; i < points.size(); i++) {
        const qvec3f v1 = points.at(i-1);
        const qvec3f v2 = points.at(i);
        
        const float triarea = GLM_TriangleArea(v0, v1, v2);
        Q_assert(triarea >= 0.0f);
        
        triareas.push_back(triarea);
        poly_area += triarea;
    }
    const std::vector<float> cdf = MakeCDF(triareas);
    
    poly_random_point_state_t result;
    result.points = points;
    result.triareas = triareas;
    result.triareas_cdf = cdf;
    return result;
}

// r1, r2, r3 must be in [0, 1]
qvec3f GLM_PolyRandomPoint(const poly_random_point_state_t &state, float r1, float r2, float r3)
{
    // Pick a random triangle, with probability proportional to triangle area
    const float uniformRandom = r1;
    const int whichTri = SampleCDF(state.triareas_cdf, uniformRandom);
    
    Q_assert(whichTri >= 0 && whichTri < state.triareas.size());

    const tri_t tri { state.points.at(0), state.points.at(1 + whichTri), state.points.at(2 + whichTri) };
    
    // Pick random barycentric coords.
    const qvec3f bary = Barycentric_Random(r2, r3);
    const qvec3f point = Barycentric_ToPoint(bary, tri);
    
    return point;
}

std::pair<int, qvec3f> GLM_ClosestPointOnPolyBoundary(const std::vector<qvec3f> &poly, const qvec3f &point)
{
    const int N = static_cast<int>(poly.size());
    
    int bestI = -1;
    float bestDist = FLT_MAX;
    qvec3f bestPointOnPoly(0);
    
    for (int i=0; i<N; i++) {
        const qvec3f p0 = poly.at(i);
        const qvec3f p1 = poly.at((i + 1) % N);
        
        const qvec3f c = ClosestPointOnLineSegment(p0, p1, point);
        const float distToC = qv::length(c - point);
        
        if (distToC < bestDist) {
            bestI = i;
            bestDist = distToC;
            bestPointOnPoly = c;
        }
    }
    
    Q_assert(bestI != -1);
    
    return make_pair(bestI, bestPointOnPoly);
}

std::pair<bool, qvec3f> GLM_InterpolateNormal(const std::vector<qvec3f> &points,
                                                 const std::vector<qvec3f> &normals,
                                                 const qvec3f &point)
{
    Q_assert(points.size() == normals.size());
    
    if (points.size() < 3)
        return make_pair(false, qvec3f(0));
    
    // Step through the triangles, being careful to handle zero-size ones

    const qvec3f &p0 = points.at(0);
    const qvec3f &n0 = normals.at(0);
    
    const int N = points.size();
    for (int i=2; i<N; i++) {
        const qvec3f &p1 = points.at(i-1);
        const qvec3f &n1 = normals.at(i-1);
        const qvec3f &p2 = points.at(i);
        const qvec3f &n2 = normals.at(i);
     
        const auto edgeplanes = GLM_MakeInwardFacingEdgePlanes({p0, p1, p2});
        if (edgeplanes.size() != 3)
            continue;
        
        if (GLM_EdgePlanes_PointInside(edgeplanes, point)) {
            // Found the correct triangle
            
            const qvec3f bary = Barycentric_FromPoint(point, make_tuple(p0, p1, p2));
            
            if (!isfinite(bary[0]) || !isfinite(bary[1]) || !isfinite(bary[2]))
                continue;

            const qvec3f interpolatedNormal = Barycentric_ToPoint(bary, make_tuple(n0, n1, n2));
            return make_pair(true, interpolatedNormal);
        }
    }
    
    return make_pair(false, qvec3f(0));
}

static winding_t *glm_to_winding(const std::vector<qvec3f> &poly)
{
    const int N = poly.size();
    winding_t *winding = AllocWinding(N);
    for (int i=0; i<N; i++) {
        glm_to_vec3_t(poly.at(i), winding->p[i]);
    }
    winding->numpoints = N;
    return winding;
}

static std::vector<qvec3f> winding_to_glm(const winding_t *w)
{
    if (w == nullptr)
        return {};
    std::vector<qvec3f> res;
    res.reserve(w->numpoints); //mxd. https://clang.llvm.org/extra/clang-tidy/checks/performance-inefficient-vector-operation.html
    for (int i=0; i<w->numpoints; i++) {
        res.push_back(vec3_t_to_glm(w->p[i]));
    }
    return res;
}

/// Returns (front part, back part)
std::pair<std::vector<qvec3f>,std::vector<qvec3f>> GLM_ClipPoly(const std::vector<qvec3f> &poly, const qvec4f &plane)
{
    vec3_t normal;
    winding_t *front = nullptr;
    winding_t *back = nullptr;
    
    if (poly.empty())
        return make_pair(vector<qvec3f>(),vector<qvec3f>());
    
    winding_t *w = glm_to_winding(poly);
    glm_to_vec3_t(qvec3f(plane), normal);
    ClipWinding(w, normal, plane[3], &front, &back);
    
    const auto res = make_pair(winding_to_glm(front), winding_to_glm(back));
    free(front);
    free(back);
    return res;
}

std::vector<qvec3f> GLM_ShrinkPoly(const std::vector<qvec3f> &poly, const float amount) {
    const vector<qvec4f> edgeplanes = GLM_MakeInwardFacingEdgePlanes(poly);
    
    vector<qvec3f> clipped = poly;
    
    for (const qvec4f &edge : edgeplanes) {
        const qvec4f shrunkEdgePlane(edge[0], edge[1], edge[2], edge[3] + amount);
        clipped = GLM_ClipPoly(clipped, shrunkEdgePlane).first;
    }
    
    return clipped;
}

// from: http://stackoverflow.com/a/1501725
// see also: http://mathworld.wolfram.com/Projection.html
float FractionOfLine(const qvec3f &v, const qvec3f &w, const qvec3f& p)
{
    const qvec3f vp = p - v;
    const qvec3f vw = w - v;
    
    const float l2 = qv::dot(vw, vw);
    if (l2 == 0) {
        return 0;
    }
    
    const float t = qv::dot(vp, vw) / l2;
    return t;
}

float DistToLine(const qvec3f &v, const qvec3f &w, const qvec3f& p)
{
    const qvec3f closest = ClosestPointOnLine(v,w,p);
    return qv::distance(p, closest);
}

qvec3f ClosestPointOnLine(const qvec3f &v, const qvec3f &w, const qvec3f& p)
{
    const qvec3f vp = p - v;
    const qvec3f vw_norm = qv::normalize(w - v);
    
    const float vp_scalarproj = qv::dot(vp, vw_norm);
    
    const qvec3f p_projected_on_vw = v + (vw_norm * vp_scalarproj);
    
    return p_projected_on_vw;
}

float DistToLineSegment(const qvec3f &v, const qvec3f &w, const qvec3f& p)
{
    const qvec3f closest = ClosestPointOnLineSegment(v,w,p);
    return qv::distance(p, closest);
}

qvec3f ClosestPointOnLineSegment(const qvec3f &v, const qvec3f &w, const qvec3f& p)
{
    const float frac = FractionOfLine(v, w, p);
    if (frac >= 1)
        return w;
    if (frac <= 0)
        return v;
    
    return ClosestPointOnLine(v, w, p);
}

/// Returns degrees of clockwise rotation from start to end, assuming `normal` is pointing towards the viewer
float SignedDegreesBetweenUnitVectors(const qvec3f &start, const qvec3f &end, const qvec3f &normal)
{
    const float cosangle = qmax(-1.0, qmin(1.0, qv::dot(start, end)));
    const float unsigned_degrees = acos(cosangle) * (360.0 / (2.0 * Q_PI));

    // get a normal for the rotation plane using the right-hand rule
    const qvec3f rotationNormal = qv::normalize(qv::cross(start, end));
    
    const float normalsCosAngle = qv::dot(rotationNormal, normal);
    if (normalsCosAngle >= 0) {
        // counterclockwise rotation
        return -unsigned_degrees;
    }
    // clockwise rotation
    return unsigned_degrees;
}

concavity_t FacePairConcavity(const qvec3f &face1Center,
                      const qvec3f &face1Normal,
                      const qvec3f &face2Center,
                      const qvec3f &face2Normal)
{
    const qvec3f face1to2_dir = qv::normalize(face2Center - face1Center);
    const qvec3f towards_viewer_dir = qv::cross(face1to2_dir, face1Normal);
    
    const float degrees = SignedDegreesBetweenUnitVectors(face1Normal, face2Normal, towards_viewer_dir);
    if (fabs(degrees) < DEGREES_EPSILON) {
        return concavity_t::Coplanar;
    } else if (degrees < 0.0f) {
        return concavity_t::Concave;
    } else {
        return concavity_t::Convex;
    }
}

/**
 * do the line segments overlap at all?
 * - if not colinear, returns false.
 * - the direction doesn't matter.
 * - only tips touching is enough
 */
bool
LinesOverlap(const qvec3f p0, const qvec3f p1,
             const qvec3f q0, const qvec3f q1)
{
    const float q0_linedist = DistToLine(p0, p1, q0);
    if (q0_linedist > ON_EPSILON)
        return false; // not colinear
    
    const float q1_linedist = DistToLine(p0, p1, q1);
    if (q1_linedist > ON_EPSILON)
        return false; // not colinear

    const float q0_frac = FractionOfLine(p0, p1, q0);
    const float q1_frac = FractionOfLine(p0, p1, q1);
    
    if (q0_frac < 0.0 && q1_frac < 0.0)
        return false;
    
    if (q0_frac > 1.0 && q1_frac > 1.0)
        return false;
    
    return true;
}
