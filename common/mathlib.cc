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
#include <common/log.hh>
#include <cassert>

#include <tuple>
#include <cmath>

#include <common/qvec.hh>

qmat3x3d RotateAboutX(double t)
{
    // https://en.wikipedia.org/wiki/Rotation_matrix#Examples

    const double cost = cos(t);
    const double sint = sin(t);

    return qmat3x3d{
        1, 0, 0, // col0
        0, cost, sint, // col1
        0, -sint, cost // col1
    };
}

qmat3x3d RotateAboutY(double t)
{
    const double cost = cos(t);
    const double sint = sin(t);

    return qmat3x3d{
        cost, 0, -sint, // col0
        0, 1, 0, // col1
        sint, 0, cost // col2
    };
}

qmat3x3d RotateAboutZ(double t)
{
    const double cost = cos(t);
    const double sint = sin(t);

    return qmat3x3d{
        cost, sint, 0, // col0
        -sint, cost, 0, // col1
        0, 0, 1 // col2
    };
}

// Returns a 3x3 matrix that rotates (0,0,1) to the given surface normal.
qmat3x3f RotateFromUpToSurfaceNormal(const qvec3f &surfaceNormal)
{
    constexpr qvec3f up(0, 0, 1);
    constexpr qvec3f east(1, 0, 0);
    constexpr qvec3f north(0, 1, 0);

    // get rotation about Z axis
    float x = qv::dot(east, surfaceNormal);
    float y = qv::dot(north, surfaceNormal);
    float theta = atan2f(y, x);

    // get angle away from Z axis
    float cosangleFromUp = qv::dot(up, surfaceNormal);
    cosangleFromUp = std::min(std::max(-1.0f, cosangleFromUp), 1.0f);
    float radiansFromUp = acosf(cosangleFromUp);

    const qmat3x3d rotations = RotateAboutZ(theta) * RotateAboutY(radiansFromUp);
    return qmat3x3f(rotations);
}

double Random()
{
    return (double)rand() / RAND_MAX;
}

static std::vector<float> NormalizePDF(const std::vector<float> &pdf)
{
    float pdfSum = 0.0f;
    for (float val : pdf) {
        pdfSum += val;
    }

    std::vector<float> normalizedPdf;
    normalizedPdf.reserve(pdf.size());
    for (float val : pdf) {
        normalizedPdf.push_back(val / pdfSum);
    }
    return normalizedPdf;
}

std::vector<float> MakeCDF(const std::vector<float> &pdf)
{
    const std::vector<float> normzliedPdf = NormalizePDF(pdf);
    std::vector<float> cdf;
    cdf.reserve(normzliedPdf.size());
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
    for (size_t i = 0; i < size; i++) {
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
    return Gaussian1D(width, x, alpha) * Gaussian1D(height, y, alpha);
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
    float dist = sqrtf((x * x) + (y * y));
    float lanczos = Lanczos1D(dist, a);
    return lanczos;
}

qvec3f FaceNormal(std::vector<qvec3f> points)
{
    const int N = static_cast<int>(points.size());
    float maxArea = -FLT_MAX;
    int bestI = -1;

    const qvec3f &p0 = points[0];

    for (int i = 2; i < N; i++) {
        const qvec3f &p1 = points[i - 1];
        const qvec3f &p2 = points[i];

        const float area = qv::TriangleArea(p0, p1, p2);
        if (area > maxArea) {
            maxArea = area;
            bestI = i;
        }
    }

    if (bestI == -1 || maxArea < ZERO_TRI_AREA_EPSILON)
        return qvec3f{};

    const qvec3f &p1 = points[bestI - 1];
    const qvec3f &p2 = points[bestI];
    const qvec3f normal = qv::normalize(qv::cross(p2 - p0, p1 - p0));
    return normal;
}

qvec4f PolyPlane(const std::vector<qvec3f> &points)
{
    const qvec3f normal = FaceNormal(points);
    const float dist = qv::dot(points.at(0), normal);
    return qvec4f(normal[0], normal[1], normal[2], dist);
}

std::pair<bool, qvec4f> MakeInwardFacingEdgePlane(const qvec3f &v0, const qvec3f &v1, const qvec3f &faceNormal)
{
    const float v0v1len = qv::length(v1 - v0);
    if (v0v1len < POINT_EQUAL_EPSILON)
        return std::make_pair(false, qvec4f(0));

    const qvec3f edgedir = (v1 - v0) / v0v1len;
    const qvec3f edgeplane_normal = qv::cross(edgedir, faceNormal);
    const float edgeplane_dist = qv::dot(edgeplane_normal, v0);

    return std::make_pair(true, qvec4f(edgeplane_normal[0], edgeplane_normal[1], edgeplane_normal[2], edgeplane_dist));
}

std::vector<qvec4f> MakeInwardFacingEdgePlanes(const std::vector<qvec3f> &points)
{
    const size_t N = points.size();
    if (N < 3)
        return {};

    std::vector<qvec4f> result;
    result.reserve(points.size());

    const qvec3f faceNormal = FaceNormal(points);

    if (qv::emptyExact(faceNormal))
        return {};

    for (int i = 0; i < N; i++) {
        const qvec3f &v0 = points[i];
        const qvec3f &v1 = points[(i + 1) % N];

        const auto edgeplane = MakeInwardFacingEdgePlane(v0, v1, faceNormal);
        if (!edgeplane.first)
            continue;

        result.push_back(edgeplane.second);
    }

    return result;
}

float EdgePlanes_PointInsideDist(const std::vector<qvec4f> &edgeplanes, const qvec3f &point)
{
    float min = FLT_MAX;

    for (int i = 0; i < edgeplanes.size(); i++) {
        const float planedist = DistAbovePlane(edgeplanes[i], point);
        if (planedist < min)
            min = planedist;
    }

    return min; // "outermost" point
}

bool EdgePlanes_PointInside(const std::vector<qvec4f> &edgeplanes, const qvec3f &point)
{
    if (edgeplanes.empty())
        return false;

    const float minDist = EdgePlanes_PointInsideDist(edgeplanes, point);
    return minDist >= -POINT_EQUAL_EPSILON;
}

qvec4f MakePlane(const qvec3f &normal, const qvec3f &point)
{
    return qvec4f(normal[0], normal[1], normal[2], qv::dot(point, normal));
}

float DistAbovePlane(const qvec4f &plane, const qvec3f &point)
{
    return qv::dot(qvec3f(plane), point) - plane[3];
}

qvec3f ProjectPointOntoPlane(const qvec4f &plane, const qvec3f &point)
{
    float dist = DistAbovePlane(plane, point);
    qvec3f move = qvec3f(plane) * -dist;
    return point + move;
}

poly_random_point_state_t PolyRandomPoint_Setup(const std::vector<qvec3f> &points)
{
    Q_assert(points.size() >= 3);

    std::vector<float> triareas;
    triareas.reserve(points.size() - 2);

    const qvec3f &v0 = points.at(0);
    for (int i = 2; i < points.size(); i++) {
        const qvec3f &v1 = points.at(i - 1);
        const qvec3f &v2 = points.at(i);

        const float triarea = qv::TriangleArea(v0, v1, v2);
        Q_assert(triarea >= 0.0f);

        triareas.push_back(triarea);
    }
    const std::vector<float> cdf = MakeCDF(triareas);

    poly_random_point_state_t result;
    result.points = points;
    result.triareas = triareas;
    result.triareas_cdf = cdf;
    return result;
}

// r1, r2, r3 must be in [0, 1]
qvec3f PolyRandomPoint(const poly_random_point_state_t &state, float r1, float r2, float r3)
{
    // Pick a random triangle, with probability proportional to triangle area
    const float uniformRandom = r1;
    const int whichTri = SampleCDF(state.triareas_cdf, uniformRandom);

    Q_assert(whichTri >= 0 && whichTri < state.triareas.size());

    // Pick random barycentric coords.
    const qvec3f bary = qv::Barycentric_Random(r2, r3);
    const qvec3f point =
        qv::Barycentric_ToPoint(bary, state.points.at(0), state.points.at(1 + whichTri), state.points.at(2 + whichTri));

    return point;
}

std::pair<int, qvec3f> ClosestPointOnPolyBoundary(const std::vector<qvec3f> &poly, const qvec3f &point)
{
    const int N = static_cast<int>(poly.size());

    int bestI = -1;
    float bestDist = FLT_MAX;
    qvec3f bestPointOnPoly{};

    for (int i = 0; i < N; i++) {
        const qvec3f &p0 = poly.at(i);
        const qvec3f &p1 = poly.at((i + 1) % N);

        const qvec3f c = ClosestPointOnLineSegment(p0, p1, point);
        const float distToC = qv::length(c - point);

        if (distToC < bestDist) {
            bestI = i;
            bestDist = distToC;
            bestPointOnPoly = c;
        }
    }

    Q_assert(bestI != -1);

    return std::make_pair(bestI, bestPointOnPoly);
}

std::pair<bool, qvec3f> InterpolateNormal(
    const std::vector<qvec3f> &points, const std::vector<face_normal_t> &normals, const qvec3f &point)
{
    std::vector<qvec3f> normalvecs;
    for (auto &normal : normals) {
        normalvecs.push_back(normal.normal);
    }

    return InterpolateNormal(points, normalvecs, point);
}

std::pair<bool, qvec3f> InterpolateNormal(
    const std::vector<qvec3f> &points, const std::vector<qvec3f> &normals, const qvec3f &point)
{
    Q_assert(points.size() == normals.size());

    if (points.size() < 3)
        return std::make_pair(false, qvec3f{});

    // Step through the triangles, being careful to handle zero-size ones

    const qvec3f &p0 = points.at(0);
    const qvec3f &n0 = normals.at(0);

    const int N = points.size();
    for (int i = 2; i < N; i++) {
        const qvec3f &p1 = points.at(i - 1);
        const qvec3f &n1 = normals.at(i - 1);
        const qvec3f &p2 = points.at(i);
        const qvec3f &n2 = normals.at(i);

        const auto edgeplanes = MakeInwardFacingEdgePlanes({p0, p1, p2});
        if (edgeplanes.size() != 3)
            continue;

        if (EdgePlanes_PointInside(edgeplanes, point)) {
            // Found the correct triangle

            const qvec3f bary = qv::Barycentric_FromPoint(point, p0, p1, p2);

            if (!std::isfinite(bary[0]) || !std::isfinite(bary[1]) || !std::isfinite(bary[2]))
                continue;

            const qvec3f interpolatedNormal = qv::Barycentric_ToPoint(bary, n0, n1, n2);
            return std::make_pair(true, interpolatedNormal);
        }
    }

    return std::make_pair(false, qvec3f{});
}

/// Returns (front part, back part)
std::pair<std::vector<qvec3f>, std::vector<qvec3f>> ClipPoly(const std::vector<qvec3f> &poly, const qvec4f &plane)
{
    if (poly.empty())
        return make_pair(std::vector<qvec3f>(), std::vector<qvec3f>());

    auto w = polylib::winding_t::from_winding_points(poly);

    auto clipped = w.clip({plane.xyz(), plane[3]});

    std::pair<std::vector<qvec3f>, std::vector<qvec3f>> result;
    if (clipped[0]) {
        result.first = clipped[0]->glm_winding_points();
    }
    if (clipped[1]) {
        result.second = clipped[1]->glm_winding_points();
    }
    return result;
}

std::vector<qvec3f> ShrinkPoly(const std::vector<qvec3f> &poly, const float amount)
{
    const std::vector<qvec4f> edgeplanes = MakeInwardFacingEdgePlanes(poly);

    std::vector<qvec3f> clipped = poly;

    for (const qvec4f &edge : edgeplanes) {
        const qvec4f shrunkEdgePlane(edge[0], edge[1], edge[2], edge[3] + amount);
        clipped = ClipPoly(clipped, shrunkEdgePlane).first;
    }

    return clipped;
}

// from: http://stackoverflow.com/a/1501725
// see also: http://mathworld.wolfram.com/Projection.html
float FractionOfLine(const qvec3f &v, const qvec3f &w, const qvec3f &p)
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

float DistToLine(const qvec3f &v, const qvec3f &w, const qvec3f &p)
{
    const qvec3f closest = ClosestPointOnLine(v, w, p);
    return qv::distance(p, closest);
}

qvec3f ClosestPointOnLine(const qvec3f &v, const qvec3f &w, const qvec3f &p)
{
    const qvec3f vp = p - v;
    const qvec3f vw_norm = qv::normalize(w - v);

    if (qv::emptyExact(vw_norm)) {
        return p;
    }

    const float vp_scalarproj = qv::dot(vp, vw_norm);

    const qvec3f p_projected_on_vw = v + (vw_norm * vp_scalarproj);

    return p_projected_on_vw;
}

float DistToLineSegment(const qvec3f &v, const qvec3f &w, const qvec3f &p)
{
    const qvec3f closest = ClosestPointOnLineSegment(v, w, p);
    return qv::distance(p, closest);
}

qvec3f ClosestPointOnLineSegment(const qvec3f &v, const qvec3f &w, const qvec3f &p)
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
    const float cosangle = std::max(-1.0f, std::min(1.0f, qv::dot(start, end)));
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

concavity_t FacePairConcavity(
    const qvec3f &face1Center, const qvec3f &face1Normal, const qvec3f &face2Center, const qvec3f &face2Normal)
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

qvec4f bilinearWeights(const float x, const float y)
{
    Q_assert(x >= 0.0f);
    Q_assert(x <= 1.0f);

    Q_assert(y >= 0.0f);
    Q_assert(y <= 1.0f);

    return qvec4f((1.0f - x) * (1.0f - y), x * (1.0f - y), (1.0f - x) * y, x * y);
}

std::array<std::pair<qvec2i, float>, 4> bilinearWeightsAndCoords(qvec2f pos, const qvec2i &size)
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

/**
 * do the line segments overlap at all?
 * - if not colinear, returns false.
 * - the direction doesn't matter.
 * - only tips touching is enough
 */
bool LinesOverlap(const qvec3f &p0, const qvec3f &p1, const qvec3f &q0, const qvec3f &q1, double on_epsilon)
{
    const float q0_linedist = DistToLine(p0, p1, q0);
    if (q0_linedist > on_epsilon)
        return false; // not colinear

    const float q1_linedist = DistToLine(p0, p1, q1);
    if (q1_linedist > on_epsilon)
        return false; // not colinear

    const float q0_frac = FractionOfLine(p0, p1, q0);
    const float q1_frac = FractionOfLine(p0, p1, q1);

    if (q0_frac < 0.0 && q1_frac < 0.0)
        return false;

    if (q0_frac > 1.0 && q1_frac > 1.0)
        return false;

    return true;
}
