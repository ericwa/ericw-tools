#include <catch2/catch_all.hpp>

#include <light/light.hh>
#include <light/entities.hh>

#include <random>
#include <algorithm> // for std::sort
#include <fmt/format.h>

#include <common/qvec.hh>

#include <common/aabb.hh>

using namespace std;

TEST_CASE("MakeCDF", "[mathlib]")
{

    std::vector<float> pdfUnnormzlied{25, 50, 25};
    std::vector<float> cdf = MakeCDF(pdfUnnormzlied);

    REQUIRE(3u == cdf.size());
    REQUIRE(Catch::Approx(0.25) == cdf.at(0));
    REQUIRE(Catch::Approx(0.75) == cdf.at(1));
    REQUIRE(Catch::Approx(1.0) == cdf.at(2));

    // TODO: return pdf
    REQUIRE(0 == SampleCDF(cdf, 0));
    REQUIRE(0 == SampleCDF(cdf, 0.1));
    REQUIRE(0 == SampleCDF(cdf, 0.25));
    REQUIRE(1 == SampleCDF(cdf, 0.26));
    REQUIRE(1 == SampleCDF(cdf, 0.75));
    REQUIRE(2 == SampleCDF(cdf, 0.76));
    REQUIRE(2 == SampleCDF(cdf, 1));
}

static void checkBox(const vector<qvec4f> &edges, const vector<qvec3f> &poly)
{
    CHECK(GLM_EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
    CHECK(GLM_EdgePlanes_PointInside(edges, qvec3f(64, 0, 0)));
    CHECK(GLM_EdgePlanes_PointInside(edges, qvec3f(32, 32, 0)));
    CHECK(GLM_EdgePlanes_PointInside(edges, qvec3f(32, 32, 32))); // off plane

    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(-0.1, 0, 0)));
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(64.1, 0, 0)));
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(0, -0.1, 0)));
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(0, 64.1, 0)));
}

TEST_CASE("EdgePlanesOfNonConvexPoly", "[mathlib]")
{
    // hourglass, non-convex
    const vector<qvec3f> poly{{0, 0, 0}, {64, 64, 0}, {0, 64, 0}, {64, 0, 0}};

    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    //    CHECK(vector<qvec4f>() == edges);
}

TEST_CASE("SlightlyConcavePoly", "[mathlib]")
{
    const vector<qvec3f> poly{qvec3f(225.846161, -1744, 1774), qvec3f(248, -1744, 1798),
        qvec3f(248, -1763.82605, 1799.65222), qvec3f(248, -1764, 1799.66663), qvec3f(248, -1892, 1810.33337),
        qvec3f(248, -1893.21741, 1810.43481), qvec3f(248, -1921.59998, 1812.80005), qvec3f(248, -1924, 1813),
        qvec3f(80, -1924, 1631), qvec3f(80, -1744, 1616)};

    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    REQUIRE_FALSE(edges.empty());
    CHECK(GLM_EdgePlanes_PointInside(edges, qvec3f(152.636963, -1814, 1702)));
}

TEST_CASE("PointInPolygon", "[mathlib]")
{
    // clockwise
    const vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    checkBox(edges, poly);
}

TEST_CASE("PointInPolygon_DegenerateEdgeHandling", "[mathlib]")
{
    // clockwise
    const vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {0, 64, 0}, // repeat of last point
        {64, 64, 0}, {64, 0, 0}};

    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    checkBox(edges, poly);
}

TEST_CASE("PointInPolygon_DegenerateFaceHandling1", "[mathlib]")
{
    const vector<qvec3f> poly{};

    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(10, 10, 10)));
}

TEST_CASE("PointInPolygon_DegenerateFaceHandling2", "[mathlib]")
{
    const vector<qvec3f> poly{
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
    };

    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(10, 10, 10)));
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(-10, -10, -10)));
}

TEST_CASE("PointInPolygon_DegenerateFaceHandling3", "[mathlib]")
{
    const vector<qvec3f> poly{
        {0, 0, 0},
        {10, 10, 10},
        {20, 20, 20},
    };

    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(10, 10, 10)));
    CHECK_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(-10, -10, -10)));
}

TEST_CASE("PointInPolygon_ColinearPointHandling", "[mathlib]")
{
    // clockwise
    const vector<qvec3f> poly{{0, 0, 0}, {0, 32, 0}, // colinear
        {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);

    checkBox(edges, poly);
}

TEST_CASE("ClosestPointOnLineSegment_Degenerate", "[mathlib]")
{
    CHECK(qvec3f(0, 0, 0) == ClosestPointOnLineSegment(qvec3f(0, 0, 0), qvec3f(0, 0, 0), qvec3f(10, 10, 10)));
}

TEST_CASE("ClosestPointOnPolyBoundary", "[mathlib]")
{
    // clockwise
    const vector<qvec3f> poly{
        {0, 0, 0}, // edge 0 start, edge 3 end
        {0, 64, 0}, // edge 1 start, edge 0 end
        {64, 64, 0}, // edge 2 start, edge 1 end
        {64, 0, 0} // edge 3 start, edge 2 end
    };

    CHECK(make_pair(0, qvec3f(0, 0, 0)) == GLM_ClosestPointOnPolyBoundary(poly, qvec3f(0, 0, 0)));

    // Either edge 1 or 2 contain the point qvec3f(64,64,0), but we expect the first edge to be returned
    CHECK(make_pair(1, qvec3f(64, 64, 0)) == GLM_ClosestPointOnPolyBoundary(poly, qvec3f(100, 100, 100)));
    CHECK(make_pair(2, qvec3f(64, 32, 0)) == GLM_ClosestPointOnPolyBoundary(poly, qvec3f(100, 32, 0)));

    CHECK(make_pair(0, qvec3f(0, 0, 0)) == GLM_ClosestPointOnPolyBoundary(poly, qvec3f(-1, -1, 0)));
}

TEST_CASE("PolygonCentroid_empty", "[mathlib]")
{
    const std::initializer_list<qvec3d> empty{};
    const qvec3f res = qv::PolyCentroid(empty.begin(), empty.end());

    for (int i = 0; i < 3; i++) {
        CHECK(std::isnan(res[i]));
    }
}

TEST_CASE("PolygonCentroid_point", "[mathlib]")
{
    const std::initializer_list<qvec3d> point{{1, 1, 1}};
    CHECK(*point.begin() == qv::PolyCentroid(point.begin(), point.end()));
}

TEST_CASE("PolygonCentroid_line", "[mathlib]")
{
    const std::initializer_list<qvec3d> line{{0, 0, 0}, {2, 2, 2}};
    CHECK(qvec3d(1, 1, 1) == qv::PolyCentroid(line.begin(), line.end()));
}

TEST_CASE("PolygonCentroid", "[mathlib]")
{
    // poor test.. but at least checks that the colinear point is treated correctly
    const std::initializer_list<qvec3d> poly{{0, 0, 0}, {0, 32, 0}, // colinear
        {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    CHECK(qvec3f(32, 32, 0) == qv::PolyCentroid(poly.begin(), poly.end()));
}

TEST_CASE("PolygonArea", "[mathlib]")
{
    // poor test.. but at least checks that the colinear point is treated correctly
    const std::initializer_list<qvec3d> poly{{0, 0, 0}, {0, 32, 0}, // colinear
        {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    CHECK(64.0f * 64.0f == qv::PolyArea(poly.begin(), poly.end()));

    // 0, 1, or 2 vertices return 0 area
    CHECK(0.0f == qv::PolyArea(poly.begin(), poly.begin()));
    CHECK(0.0f == qv::PolyArea(poly.begin(), poly.begin() + 1));
    CHECK(0.0f == qv::PolyArea(poly.begin(), poly.begin() + 2));
}

TEST_CASE("BarycentricFromPoint", "[mathlib]")
{
    // clockwise
    const std::array<qvec3f, 3> tri{qvec3f{0, 0, 0}, {0, 64, 0}, {64, 0, 0}};

    CHECK(qvec3f(1, 0, 0) == qv::Barycentric_FromPoint(tri[0], tri[0], tri[1], tri[2]));
    CHECK(qvec3f(0, 1, 0) == qv::Barycentric_FromPoint(tri[1], tri[0], tri[1], tri[2]));
    CHECK(qvec3f(0, 0, 1) == qv::Barycentric_FromPoint(tri[2], tri[0], tri[1], tri[2]));

    CHECK(qvec3f(0.5, 0.5, 0.0) == qv::Barycentric_FromPoint({0, 32, 0}, tri[0], tri[1], tri[2]));
    CHECK(qvec3f(0.0, 0.5, 0.5) == qv::Barycentric_FromPoint({32, 32, 0}, tri[0], tri[1], tri[2]));
    CHECK(qvec3f(0.5, 0.0, 0.5) == qv::Barycentric_FromPoint({32, 0, 0}, tri[0], tri[1], tri[2]));
}

TEST_CASE("BarycentricToPoint", "[mathlib]")
{
    // clockwise
    const std::array<qvec3f, 3> tri{qvec3f{0, 0, 0}, {0, 64, 0}, {64, 0, 0}};

    CHECK(tri[0] == qv::Barycentric_ToPoint({1, 0, 0}, tri[0], tri[1], tri[2]));
    CHECK(tri[1] == qv::Barycentric_ToPoint({0, 1, 0}, tri[0], tri[1], tri[2]));
    CHECK(tri[2] == qv::Barycentric_ToPoint({0, 0, 1}, tri[0], tri[1], tri[2]));

    CHECK(qvec3f(0, 32, 0) == qv::Barycentric_ToPoint({0.5, 0.5, 0.0}, tri[0], tri[1], tri[2]));
    CHECK(qvec3f(32, 32, 0) == qv::Barycentric_ToPoint({0.0, 0.5, 0.5}, tri[0], tri[1], tri[2]));
    CHECK(qvec3f(32, 0, 0) == qv::Barycentric_ToPoint({0.5, 0.0, 0.5}, tri[0], tri[1], tri[2]));
}

TEST_CASE("BarycentricRandom", "[mathlib]")
{
    // clockwise
    const std::array<qvec3f, 3> tri{qvec3f{0, 0, 0}, {0, 64, 0}, {64, 0, 0}};

    const auto triAsVec = vector<qvec3f>{tri.begin(), tri.end()};
    const auto edges = GLM_MakeInwardFacingEdgePlanes(triAsVec);
    const auto plane = GLM_PolyPlane(triAsVec);

    for (int i = 0; i < 100; i++) {
        const float r0 = Random();
        const float r1 = Random();

        REQUIRE(r0 >= 0);
        REQUIRE(r1 >= 0);
        REQUIRE(r0 <= 1);
        REQUIRE(r1 <= 1);

        const auto bary = qv::Barycentric_Random(r0, r1);
        CHECK(Catch::Approx(1.0f) == bary[0] + bary[1] + bary[2]);

        const qvec3f point = qv::Barycentric_ToPoint(bary, tri[0], tri[1], tri[2]);
        CHECK(GLM_EdgePlanes_PointInside(edges, point));

        CHECK(Catch::Approx(0.0f) == GLM_DistAbovePlane(plane, point));
    }
}

TEST_CASE("RotateFromUpToSurfaceNormal", "[mathlib]")
{
    std::mt19937 engine(0);
    std::uniform_real_distribution<float> dis(-4096, 4096);

    for (int i = 0; i < 100; i++) {
        const qvec3f randvec = qv::normalize(qvec3f(dis(engine), dis(engine), dis(engine)));
        const qmat3x3f m = RotateFromUpToSurfaceNormal(randvec);

        const qvec3f roundtrip = m * qvec3f(0, 0, 1);
        REQUIRE(qv::epsilonEqual(randvec, roundtrip, 0.01f));
    }
}

TEST_CASE("MakePlane", "[mathlib]")
{
    CHECK(qvec4f(0, 0, 1, 10) == GLM_MakePlane(qvec3f(0, 0, 1), qvec3f(0, 0, 10)));
    CHECK(qvec4f(0, 0, 1, 10) == GLM_MakePlane(qvec3f(0, 0, 1), qvec3f(100, 100, 10)));
}

TEST_CASE("DistAbovePlane", "[mathlib]")
{
    qvec4f plane(0, 0, 1, 10);
    qvec3f point(100, 100, 100);
    CHECK(Catch::Approx(90) == GLM_DistAbovePlane(plane, point));
}

TEST_CASE("InterpolateNormalsDegenerate", "[mathlib]")
{
    CHECK_FALSE(GLM_InterpolateNormal({}, std::vector<qvec3f>{}, qvec3f(0, 0, 0)).first);
    CHECK_FALSE(GLM_InterpolateNormal({qvec3f(0, 0, 0)}, {qvec3f(0, 0, 1)}, qvec3f(0, 0, 0)).first);
    CHECK_FALSE(
        GLM_InterpolateNormal({qvec3f(0, 0, 0), qvec3f(10, 0, 0)}, {qvec3f(0, 0, 1), qvec3f(0, 0, 1)}, qvec3f(0, 0, 0))
            .first);
}

TEST_CASE("InterpolateNormals", "[mathlib]")
{
    // This test relies on the way GLM_InterpolateNormal is implemented

    // o--o--o
    // | / / |
    // |//   |
    // o-----o

    const vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {32, 64, 0}, // colinear
        {64, 64, 0}, {64, 0, 0}};

    const vector<qvec3f> normals{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, // colinear
        {0, 0, 0}, {-1, 0, 0}};

    // First try all the known points
    for (int i = 0; i < poly.size(); i++) {
        const auto res = GLM_InterpolateNormal(poly, normals, poly.at(i));
        CHECK(true == res.first);
        CHECK(qv::epsilonEqual(normals.at(i), res.second, static_cast<float>(POINT_EQUAL_EPSILON)));
    }

    {
        const qvec3f firstTriCentroid = (poly[0] + poly[1] + poly[2]) / 3.0f;
        const auto res = GLM_InterpolateNormal(poly, normals, firstTriCentroid);
        CHECK(true == res.first);
        CHECK(qv::epsilonEqual(qvec3f(1 / 3.0f), res.second, static_cast<float>(POINT_EQUAL_EPSILON)));
    }

    // Outside poly
    CHECK_FALSE(GLM_InterpolateNormal(poly, normals, qvec3f(-0.1, 0, 0)).first);
}

static bool polysEqual(const vector<qvec3f> &p1, const vector<qvec3f> &p2)
{
    if (p1.size() != p2.size())
        return false;
    for (int i = 0; i < p1.size(); i++) {
        if (!qv::epsilonEqual(p1[i], p2[i], static_cast<float>(POINT_EQUAL_EPSILON)))
            return false;
    }
    return true;
}

TEST_CASE("ClipPoly1", "[mathlib]")
{
    const vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    const vector<qvec3f> frontRes{{0, 0, 0}, {0, 64, 0}, {32, 64, 0}, {32, 0, 0}};

    const vector<qvec3f> backRes{{32, 64, 0}, {64, 64, 0}, {64, 0, 0}, {32, 0, 0}};

    auto clipRes = GLM_ClipPoly(poly, qvec4f(-1, 0, 0, -32));

    CHECK(polysEqual(frontRes, clipRes.first));
    CHECK(polysEqual(backRes, clipRes.second));
}

TEST_CASE("ShrinkPoly1", "[mathlib]")
{
    const vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    const vector<qvec3f> shrunkPoly{{1, 1, 0}, {1, 63, 0}, {63, 63, 0}, {63, 1, 0}};

    const auto actualShrunk = GLM_ShrinkPoly(poly, 1.0f);

    CHECK(polysEqual(shrunkPoly, actualShrunk));
}

TEST_CASE("ShrinkPoly2", "[mathlib]")
{
    const vector<qvec3f> poly{{0, 0, 0}, {64, 64, 0}, {64, 0, 0}};

    const vector<qvec3f> shrunkPoly{
        {1.0f + sqrtf(2.0f), 1.0f, 0.0f},
        {63.0f, 63.0f - sqrtf(2.0f), 0.0f},
        {63, 1, 0},
    };

    const auto actualShrunk = GLM_ShrinkPoly(poly, 1.0f);

    CHECK(polysEqual(shrunkPoly, actualShrunk));
}

TEST_CASE("SignedDegreesBetweenUnitVectors", "[mathlib]")
{
    const qvec3f up{0, 0, 1};
    const qvec3f fwd{0, 1, 0};
    const qvec3f right{1, 0, 0};

    CHECK(Catch::Approx(-90) == SignedDegreesBetweenUnitVectors(right, fwd, up));
    CHECK(Catch::Approx(90) == SignedDegreesBetweenUnitVectors(fwd, right, up));
    CHECK(Catch::Approx(0) == SignedDegreesBetweenUnitVectors(right, right, up));
}

TEST_CASE("ConcavityTest_concave", "[mathlib]")
{
    const qvec3f face1center{0, 0, 10};
    const qvec3f face2center{10, 0, 200};

    const qvec3f face1normal{0, 0, 1};
    const qvec3f face2normal{-1, 0, 0};

    CHECK(concavity_t::Concave == FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST_CASE("ConcavityTest_concave2", "[mathlib]")
{
    const qvec3f face1center{0, 0, 10};
    const qvec3f face2center{-10, 0, 200};

    const qvec3f face1normal{0, 0, 1};
    const qvec3f face2normal{1, 0, 0};

    CHECK(concavity_t::Concave == FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST_CASE("ConcavityTest_convex", "[mathlib]")
{
    const qvec3f face1center{0, 0, 10};
    const qvec3f face2center{10, 0, 5};

    const qvec3f face1normal{0, 0, 1};
    const qvec3f face2normal{1, 0, 0};

    CHECK(concavity_t::Convex == FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST_CASE("ConcavityTest_convex2", "[mathlib]")
{
    const qvec3f face1center{0, 0, 10};
    const qvec3f face2center{-10, 0, 5};

    const qvec3f face1normal{0, 0, 1};
    const qvec3f face2normal{-1, 0, 0};

    CHECK(concavity_t::Convex == FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST_CASE("ConcavityTest_coplanar", "[mathlib]")
{
    const qvec3f face1center{0, 0, 10};
    const qvec3f face2center{100, 100, 10};

    const qvec3f face1normal{0, 0, 1};
    const qvec3f face2normal{0, 0, 1};

    CHECK(concavity_t::Coplanar == FacePairConcavity(face1center, face1normal, face2center, face2normal));
}
static const float MANGLE_EPSILON = 0.1f;

TEST_CASE("vec_from_mangle", "[light]")
{
    CHECK(qv::epsilonEqual(qvec3f(1, 0, 0), qv::vec_from_mangle(qvec3f(0, 0, 0)), MANGLE_EPSILON));
    CHECK(qv::epsilonEqual(qvec3f(-1, 0, 0), qv::vec_from_mangle(qvec3f(180, 0, 0)), MANGLE_EPSILON));
    CHECK(qv::epsilonEqual(qvec3f(0, 0, 1), qv::vec_from_mangle(qvec3f(0, 90, 0)), MANGLE_EPSILON));
    CHECK(qv::epsilonEqual(qvec3f(0, 0, -1), qv::vec_from_mangle(qvec3f(0, -90, 0)), MANGLE_EPSILON));
}

TEST_CASE("mangle_from_vec", "[light]")
{
    CHECK(qv::epsilonEqual(qvec3f(0, 0, 0), qv::mangle_from_vec(qvec3f(1, 0, 0)), MANGLE_EPSILON));
    CHECK(qv::epsilonEqual(qvec3f(180, 0, 0), qv::mangle_from_vec(qvec3f(-1, 0, 0)), MANGLE_EPSILON));
    CHECK(qv::epsilonEqual(qvec3f(0, 90, 0), qv::mangle_from_vec(qvec3f(0, 0, 1)), MANGLE_EPSILON));
    CHECK(qv::epsilonEqual(qvec3f(0, -90, 0), qv::mangle_from_vec(qvec3f(0, 0, -1)), MANGLE_EPSILON));

    for (int yaw = -179; yaw <= 179; yaw++) {
        for (int pitch = -89; pitch <= 89; pitch++) {
            const qvec3f origMangle = qvec3f(yaw, pitch, 0);
            const qvec3f vec = qv::vec_from_mangle(origMangle);
            const qvec3f roundtrip = qv::mangle_from_vec(vec);
            CHECK(qv::epsilonEqual(origMangle, roundtrip, MANGLE_EPSILON));
        }
    }
}

TEST_CASE("bilinearInterpolate", "[mathlib]")
{
    const qvec4f v1(0, 1, 2, 3);
    const qvec4f v2(4, 5, 6, 7);
    const qvec4f v3(1, 1, 1, 1);
    const qvec4f v4(2, 2, 2, 2);

    CHECK(v1 == bilinearInterpolate(v1, v2, v3, v4, 0.0f, 0.0f));
    CHECK(v2 == bilinearInterpolate(v1, v2, v3, v4, 1.0f, 0.0f));
    CHECK(v3 == bilinearInterpolate(v1, v2, v3, v4, 0.0f, 1.0f));
    CHECK(v4 == bilinearInterpolate(v1, v2, v3, v4, 1.0f, 1.0f));

    CHECK(qvec4f(1.5, 1.5, 1.5, 1.5) == bilinearInterpolate(v1, v2, v3, v4, 0.5f, 1.0f));
    CHECK(qvec4f(2, 3, 4, 5) == bilinearInterpolate(v1, v2, v3, v4, 0.5f, 0.0f));
    CHECK(qvec4f(1.75, 2.25, 2.75, 3.25) == bilinearInterpolate(v1, v2, v3, v4, 0.5f, 0.5f));
}

TEST_CASE("bilinearWeightsAndCoords", "[mathlib]")
{
    const auto res = bilinearWeightsAndCoords(qvec2f(0.5, 0.25), qvec2i(2, 2));

    qvec2f sum{};
    for (int i = 0; i < 4; i++) {
        const float weight = res[i].second;
        const qvec2i intPos = res[i].first;
        sum += qvec2f(intPos) * weight;
    }
    CHECK(qvec2f(0.5, 0.25) == sum);
}

TEST_CASE("bilinearWeightsAndCoords2", "[mathlib]")
{
    const auto res = bilinearWeightsAndCoords(qvec2f(1.5, 0.5), qvec2i(2, 2));

    qvec2f sum{};
    for (int i = 0; i < 4; i++) {
        const float weight = res[i].second;
        const qvec2i intPos = res[i].first;
        sum += qvec2f(intPos) * weight;
    }
    CHECK(qvec2f(1.0, 0.5) == sum);
}

TEST_CASE("pointsAlongLine", "[mathlib]")
{
    const auto res = PointsAlongLine(qvec3f(1, 0, 0), qvec3f(3.5, 0, 0), 1.5f);

    REQUIRE(2 == res.size());
    REQUIRE(qv::epsilonEqual(qvec3f(1, 0, 0), res[0], static_cast<float>(POINT_EQUAL_EPSILON)));
    REQUIRE(qv::epsilonEqual(qvec3f(2.5, 0, 0), res[1], static_cast<float>(POINT_EQUAL_EPSILON)));
}

// FIXME: this is failing
#if 0
TEST_CASE("RandomPointInPoly", "[mathlib]") {
    const vector<qvec3f> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear point
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const auto edgeplanes = GLM_MakeInwardFacingEdgePlanes(poly);
    
    qvec3f min(FLT_MAX);
    qvec3f max(-FLT_MAX);
    qvec3f avg{};
    
    const auto randomstate = GLM_PolyRandomPoint_Setup(poly);
    
    const int N=100;
    for (int i=0; i<N; i++) {
        const qvec3f point = GLM_PolyRandomPoint(randomstate, Random(), Random(), Random());
        REQUIRE(GLM_EdgePlanes_PointInside(edgeplanes, point));
        
        //std::cout << "point: " << qv::to_string(point) << std::endl;
        
        min = qv::min(min, point);
        max = qv::max(max, point);
        avg += point;
    }
    avg /= N;
    
    REQUIRE(min[0] < 4);
    REQUIRE(min[1] < 4);
    REQUIRE(min[2] == 0);
    
    REQUIRE(max[0] > 60);
    REQUIRE(max[1] > 60);
    REQUIRE(max[2] == 0);
    
    REQUIRE(qv::length(avg - qvec3f(32, 32, 0)) < 4);
}
#endif

TEST_CASE("FractionOfLine", "[mathlib]")
{
    REQUIRE(Catch::Approx(0) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0, 0, 0)));
    REQUIRE(Catch::Approx(0.5) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0.5, 0.5, 0.5)));
    REQUIRE(Catch::Approx(1) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(1, 1, 1)));
    REQUIRE(Catch::Approx(2) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(2, 2, 2)));
    REQUIRE(Catch::Approx(-1) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(-1, -1, -1)));

    REQUIRE(Catch::Approx(0) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(0, 0, 0), qvec3f(0, 0, 0)));
}

TEST_CASE("DistToLine", "[mathlib]")
{
    const float epsilon = 0.001;

    REQUIRE(fabs(0 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0, 0, 0))) < epsilon);
    REQUIRE(fabs(0 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0.5, 0.5, 0.5))) < epsilon);
    REQUIRE(fabs(0 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(1, 1, 1))) < epsilon);
    REQUIRE(fabs(0 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(2, 2, 2))) < epsilon);
    REQUIRE(fabs(0 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(-1, -1, -1))) < epsilon);

    REQUIRE(fabs(sqrt(2) / 2 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 0), qvec3f(0, 1, 0))) < epsilon);
    REQUIRE(fabs(sqrt(2) / 2 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 0), qvec3f(1, 0, 0))) < epsilon);

    REQUIRE(fabs(0.5 - DistToLine(qvec3f(10, 0, 0), qvec3f(10, 0, 100), qvec3f(9.5, 0, 0))) < epsilon);
}

TEST_CASE("DistToLineSegment", "[mathlib]")
{
    const float epsilon = 0.001;

    REQUIRE(fabs(0 - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0, 0, 0))) < epsilon);
    REQUIRE(fabs(0 - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0.5, 0.5, 0.5))) < epsilon);
    REQUIRE(fabs(0 - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(1, 1, 1))) < epsilon);
    REQUIRE(fabs(sqrt(3) - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(2, 2, 2))) < epsilon);
    REQUIRE(fabs(sqrt(3) - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(-1, -1, -1))) < epsilon);

    REQUIRE(fabs(sqrt(2) / 2 - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 0), qvec3f(0, 1, 0))) < epsilon);
    REQUIRE(fabs(sqrt(2) / 2 - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 0), qvec3f(1, 0, 0))) < epsilon);

    REQUIRE(fabs(0.5 - DistToLineSegment(qvec3f(10, 0, 0), qvec3f(10, 0, 100), qvec3f(9.5, 0, 0))) < epsilon);
}

TEST_CASE("linesOverlap_points", "[mathlib]")
{
    REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}));
}

TEST_CASE("linesOverlap_point_line", "[mathlib]")
{
    REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 1}));
}

TEST_CASE("linesOverlap_same", "[mathlib]")
{
    REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 0}, {0, 0, 1}));
}

TEST_CASE("linesOverlap_same_opposite_dir", "[mathlib]")
{
    REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 1}, {0, 0, 0}));
}

TEST_CASE("linesOverlap_overlap", "[mathlib]")
{
    REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 0.5}, {0, 0, 1.5}));
}

TEST_CASE("linesOverlap_overlap_opposite_dir", "[mathlib]")
{
    REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 1.5}, {0, 0, 0.5}));
}

TEST_CASE("linesOverlap_only_tips_touching", "[mathlib]")
{
    REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 1}, {0, 0, 2}));
}

TEST_CASE("linesOverlap_non_colinear", "[mathlib]")
{
    REQUIRE_FALSE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {5, 0, 0}, {5, 0, 1}));
}

TEST_CASE("linesOverlap_colinear_not_touching", "[mathlib]")
{
    REQUIRE_FALSE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 0, 3}));
}

// qvec

TEST_CASE("qvec_expand", "[mathlib]")
{
    const qvec2f test(1, 2);
    const qvec4f test2(test);

    CHECK(1 == test2[0]);
    CHECK(2 == test2[1]);
    CHECK(0 == test2[2]);
    CHECK(0 == test2[3]);
}

TEST_CASE("qvec_contract", "[mathlib]")
{
    const qvec4f test(1, 2, 0, 0);
    const qvec2f test2(test);

    CHECK(1 == test2[0]);
    CHECK(2 == test2[1]);
}

TEST_CASE("qvec_copy", "[mathlib]")
{
    const qvec2f test(1, 2);
    const qvec2f test2(test);

    CHECK(1 == test2[0]);
    CHECK(2 == test2[1]);
}

TEST_CASE("qvec_constructor_init", "[mathlib]")
{
    const qvec2f test{};
    CHECK(0 == test[0]);
    CHECK(0 == test[1]);
}

TEST_CASE("qvec_constructor_1", "[mathlib]")
{
    const qvec2f test(42);
    CHECK(42 == test[0]);
    CHECK(42 == test[1]);
}

TEST_CASE("qvec_constructor_fewer", "[mathlib]")
{
    const qvec4f test(1, 2, 3);
    CHECK(1 == test[0]);
    CHECK(2 == test[1]);
    CHECK(3 == test[2]);
    CHECK(0 == test[3]);
}

TEST_CASE("qvec_constructor_extra", "[mathlib]")
{
    const qvec2f test(1, 2, 3);
    CHECK(1 == test[0]);
    CHECK(2 == test[1]);
}

// aabb3f

TEST_CASE("aabb_basic", "[mathlib]")
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    CHECK(qvec3f(1, 1, 1) == b1.mins());
    CHECK(qvec3f(10, 10, 10) == b1.maxs());
    CHECK(qvec3f(9, 9, 9) == b1.size());
}

TEST_CASE("aabb_grow", "[mathlib]")
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    CHECK(aabb3f(qvec3f(0, 0, 0), qvec3f(11, 11, 11)) == b1.grow(qvec3f(1, 1, 1)));
}

TEST_CASE("aabb_unionwith", "[mathlib]")
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));
    const aabb3f b2(qvec3f(11, 11, 11), qvec3f(12, 12, 12));

    CHECK(aabb3f(qvec3f(1, 1, 1), qvec3f(12, 12, 12)) == b1.unionWith(b2));
}

TEST_CASE("aabb_expand", "[mathlib]")
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    CHECK(b1 == b1.expand(qvec3f(1, 1, 1)));
    CHECK(b1 == b1.expand(qvec3f(5, 5, 5)));
    CHECK(b1 == b1.expand(qvec3f(10, 10, 10)));

    const aabb3f b2(qvec3f(1, 1, 1), qvec3f(100, 10, 10));
    CHECK(b2 == b1.expand(qvec3f(100, 10, 10)));

    const aabb3f b3(qvec3f(0, 1, 1), qvec3f(10, 10, 10));
    CHECK(b3 == b1.expand(qvec3f(0, 1, 1)));
}

TEST_CASE("aabb_disjoint", "[mathlib]")
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    const aabb3f yes1(qvec3f(-1, -1, -1), qvec3f(0, 0, 0));
    const aabb3f yes2(qvec3f(11, 1, 1), qvec3f(12, 10, 10));

    const aabb3f no1(qvec3f(-1, -1, -1), qvec3f(1, 1, 1));
    const aabb3f no2(qvec3f(10, 10, 10), qvec3f(10.5, 10.5, 10.5));
    const aabb3f no3(qvec3f(5, 5, 5), qvec3f(100, 6, 6));

    CHECK(b1.disjoint(yes1));
    CHECK(b1.disjoint(yes2));
    CHECK_FALSE(b1.disjoint(no1));
    CHECK_FALSE(b1.disjoint(no2));
    CHECK_FALSE(b1.disjoint(no3));

    CHECK_FALSE(b1.intersectWith(yes1));
    CHECK_FALSE(b1.intersectWith(yes2));

    // these intersections are single points
    CHECK(aabb3f::intersection_t(aabb3f(qvec3f(1, 1, 1), qvec3f(1, 1, 1))) == b1.intersectWith(no1));
    CHECK(aabb3f::intersection_t(aabb3f(qvec3f(10, 10, 10), qvec3f(10, 10, 10))) == b1.intersectWith(no2));

    // an intersection with a volume
    CHECK(aabb3f::intersection_t(aabb3f(qvec3f(5, 5, 5), qvec3f(10, 6, 6))) == b1.intersectWith(no3));

    CHECK(b1.disjoint_or_touching(aabb3f(qvec3f(10, 1, 1), qvec3f(20, 10, 10))));
    CHECK(b1.disjoint_or_touching(aabb3f(qvec3f(11, 1, 1), qvec3f(20, 10, 10))));
    CHECK_FALSE(b1.disjoint_or_touching(aabb3f(qvec3f(9.99, 1, 1), qvec3f(20, 10, 10))));
}

TEST_CASE("aabb_contains", "[mathlib]")
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    const aabb3f yes1(qvec3f(1, 1, 1), qvec3f(2, 2, 2));
    const aabb3f yes2(qvec3f(9, 9, 9), qvec3f(10, 10, 10));

    const aabb3f no1(qvec3f(-1, 1, 1), qvec3f(2, 2, 2));
    const aabb3f no2(qvec3f(9, 9, 9), qvec3f(10.5, 10, 10));

    CHECK(b1.contains(yes1));
    CHECK(b1.contains(yes2));
    CHECK_FALSE(b1.contains(no1));
    CHECK_FALSE(b1.contains(no2));
}

TEST_CASE("aabb_containsPoint", "[mathlib]")
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    const qvec3f yes1(1, 1, 1);
    const qvec3f yes2(2, 2, 2);
    const qvec3f yes3(10, 10, 10);

    const qvec3f no1(0, 0, 0);
    const qvec3f no2(1, 1, 0);
    const qvec3f no3(10.1, 10.1, 10.1);

    CHECK(b1.containsPoint(yes1));
    CHECK(b1.containsPoint(yes2));
    CHECK(b1.containsPoint(yes3));
    CHECK_FALSE(b1.containsPoint(no1));
    CHECK_FALSE(b1.containsPoint(no2));
    CHECK_FALSE(b1.containsPoint(no3));
}

TEST_CASE("aabb_create_invalid", "[mathlib]")
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(-1, -1, -1));
    const aabb3f fixed(qvec3f(1, 1, 1), qvec3f(1, 1, 1));

    CHECK(fixed == b1);
    CHECK(qvec3f(0, 0, 0) == b1.size());
}

TEST_CASE("matrix2x2inv", "[qvec]")
{
    std::mt19937 engine(0);
    std::uniform_real_distribution<float> dis(-4096, 4096);

    qmat2x2f randMat;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            randMat.at(i, j) = dis(engine);

    qmat2x2f randInv = qv::inverse(randMat);
    REQUIRE_FALSE(std::isnan(randInv.at(0, 0)));

    qmat2x2f prod = randMat * randInv;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            float exp = (i == j) ? 1.0f : 0.0f;
            REQUIRE(fabs(exp - prod.at(i, j)) < 0.001);
        }
    }

    // check non-invertible gives nan
    qmat2x2f nanMat = qv::inverse(qmat2x2f(0));
    REQUIRE(std::isnan(nanMat.at(0, 0)));
}

TEST_CASE("matrix4x4inv", "[qvec]")
{
    std::mt19937 engine(0);
    std::uniform_real_distribution<float> dis(-4096, 4096);

    qmat4x4f randMat;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            randMat.at(i, j) = dis(engine);

    qmat4x4f randInv = qv::inverse(randMat);
    REQUIRE_FALSE(std::isnan(randInv.at(0, 0)));

    qmat4x4f prod = randMat * randInv;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float exp = (i == j) ? 1.0f : 0.0f;
            REQUIRE(fabs(exp - prod.at(i, j)) < 0.001);
        }
    }

    // check non-invertible gives nan
    qmat4x4f nanMat = qv::inverse(qmat4x4f(0));
    REQUIRE(std::isnan(nanMat.at(0, 0)));
}

TEST_CASE("clamp_texcoord_small", "[trace]")
{
    // positive
    CHECK(0 == clamp_texcoord(0.0f, 2));
    CHECK(0 == clamp_texcoord(0.5f, 2));
    CHECK(1 == clamp_texcoord(1.0f, 2));
    CHECK(1 == clamp_texcoord(1.5f, 2));
    CHECK(0 == clamp_texcoord(2.0f, 2));
    CHECK(0 == clamp_texcoord(2.5f, 2));

    // negative
    CHECK(1 == clamp_texcoord(-0.5f, 2));
    CHECK(1 == clamp_texcoord(-1.0f, 2));
    CHECK(0 == clamp_texcoord(-1.5f, 2));
    CHECK(0 == clamp_texcoord(-2.0f, 2));
    CHECK(1 == clamp_texcoord(-2.5f, 2));
}

TEST_CASE("clamp_texcoord", "[trace]")
{
    // positive
    CHECK(0 == clamp_texcoord(0.0f, 128));
    CHECK(64 == clamp_texcoord(64.0f, 128));
    CHECK(64 == clamp_texcoord(64.5f, 128));
    CHECK(127 == clamp_texcoord(127.0f, 128));
    CHECK(0 == clamp_texcoord(128.0f, 128));
    CHECK(1 == clamp_texcoord(129.0f, 128));

    // negative
    CHECK(127 == clamp_texcoord(-0.5f, 128));
    CHECK(127 == clamp_texcoord(-1.0f, 128));
    CHECK(1 == clamp_texcoord(-127.0f, 128));
    CHECK(0 == clamp_texcoord(-127.5f, 128));
    CHECK(0 == clamp_texcoord(-128.0f, 128));
    CHECK(127 == clamp_texcoord(-129.0f, 128));
}

TEST_CASE("delayDefault", "[settings]")
{
    light_t light;
    CHECK(LF_LINEAR == light.formula.value());
}

TEST_CASE("delayParseInt", "[settings]")
{
    light_t light;
    CHECK(light.formula.parseString("2"));
    CHECK(LF_INVERSE2 == light.formula.value());
}

TEST_CASE("delayParseIntUnknown", "[settings]")
{
    light_t light;
    CHECK(light.formula.parseString("500"));
    // not sure if we should be strict and reject parsing this?
    CHECK(500 == light.formula.value());
}

TEST_CASE("delayParseFloat", "[settings]")
{
    light_t light;
    CHECK(light.formula.parseString("2.0"));
    CHECK(LF_INVERSE2 == light.formula.value());
}

TEST_CASE("delayParseString", "[settings]")
{
    light_t light;
    CHECK(light.formula.parseString("inverse2"));
    CHECK(LF_INVERSE2 == light.formula.value());
}
