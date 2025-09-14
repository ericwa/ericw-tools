#include <gtest/gtest.h>

#include <light/light.hh>
#include <light/trace.hh> // for clamp_texcoord
#include <light/entities.hh>

#include <random>
#include <algorithm> // for std::sort

#include <common/qvec.hh>

#include <common/aabb.hh>
#include <common/litfile.hh>

TEST(mathlib, MakeCDF)
{
    std::vector<float> pdfUnnormzlied{25, 50, 25};
    std::vector<float> cdf = MakeCDF(pdfUnnormzlied);

    ASSERT_EQ(3u, cdf.size());
    ASSERT_FLOAT_EQ(0.25, cdf.at(0));
    ASSERT_FLOAT_EQ(0.75, cdf.at(1));
    ASSERT_FLOAT_EQ(1.0, cdf.at(2));

    // TODO: return pdf
    ASSERT_EQ(0, SampleCDF(cdf, 0));
    ASSERT_EQ(0, SampleCDF(cdf, 0.1));
    ASSERT_EQ(0, SampleCDF(cdf, 0.25));
    ASSERT_EQ(1, SampleCDF(cdf, 0.26));
    ASSERT_EQ(1, SampleCDF(cdf, 0.75));
    ASSERT_EQ(2, SampleCDF(cdf, 0.76));
    ASSERT_EQ(2, SampleCDF(cdf, 1));
}

static void checkBox(const std::vector<qvec4f> &edges, const std::vector<qvec3f> &poly)
{
    EXPECT_TRUE(EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
    EXPECT_TRUE(EdgePlanes_PointInside(edges, qvec3f(64, 0, 0)));
    EXPECT_TRUE(EdgePlanes_PointInside(edges, qvec3f(32, 32, 0)));
    EXPECT_TRUE(EdgePlanes_PointInside(edges, qvec3f(32, 32, 32))); // off plane

    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(-0.1, 0, 0)));
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(64.1, 0, 0)));
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(0, -0.1, 0)));
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(0, 64.1, 0)));
}

TEST(mathlib, EdgePlanesOfNonConvexPoly)
{
    // hourglass, non-convex
    const std::vector<qvec3f> poly{{0, 0, 0}, {64, 64, 0}, {0, 64, 0}, {64, 0, 0}};

    const auto edges = MakeInwardFacingEdgePlanes(poly);
    //    EXPECT_EQ(vector<qvec4f>(), edges);
}

TEST(mathlib, SlightlyConcavePoly)
{
    const std::vector<qvec3f> poly{qvec3f(225.846161, -1744, 1774), qvec3f(248, -1744, 1798),
        qvec3f(248, -1763.82605, 1799.65222), qvec3f(248, -1764, 1799.66663), qvec3f(248, -1892, 1810.33337),
        qvec3f(248, -1893.21741, 1810.43481), qvec3f(248, -1921.59998, 1812.80005), qvec3f(248, -1924, 1813),
        qvec3f(80, -1924, 1631), qvec3f(80, -1744, 1616)};

    const auto edges = MakeInwardFacingEdgePlanes(poly);
    ASSERT_FALSE(edges.empty());
    EXPECT_TRUE(EdgePlanes_PointInside(edges, qvec3f(152.636963, -1814, 1702)));
}

TEST(polylib, PointInPolygonBasic)
{
    // clockwise
    const std::vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    const auto edges = MakeInwardFacingEdgePlanes(poly);
    checkBox(edges, poly);
}

TEST(polylib, PointInPolygonDegenerateEdgeHandling)
{
    // clockwise
    const std::vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {0, 64, 0}, // repeat of last point
        {64, 64, 0}, {64, 0, 0}};

    const auto edges = MakeInwardFacingEdgePlanes(poly);
    checkBox(edges, poly);
}

TEST(polylib, PointInPolygonDegenerateFaceHandling1)
{
    const std::vector<qvec3f> poly{};

    const auto edges = MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(10, 10, 10)));
}

TEST(polylib, PointInPolygonDegenerateFaceHandling2)
{
    const std::vector<qvec3f> poly{
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
    };

    const auto edges = MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(10, 10, 10)));
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(-10, -10, -10)));
}

TEST(polylib, PointInPolygonDegenerateFaceHandling3)
{
    const std::vector<qvec3f> poly{
        {0, 0, 0},
        {10, 10, 10},
        {20, 20, 20},
    };

    const auto edges = MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(10, 10, 10)));
    EXPECT_FALSE(EdgePlanes_PointInside(edges, qvec3f(-10, -10, -10)));
}

TEST(polylib, PointInPolygonColinearPointHandling)
{
    // clockwise
    const std::vector<qvec3f> poly{{0, 0, 0}, {0, 32, 0}, // colinear
        {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    const auto edges = MakeInwardFacingEdgePlanes(poly);

    checkBox(edges, poly);
}

TEST(mathlib, ClosestPointOnLineSegmentDegenerate)
{
    EXPECT_EQ(qvec3f(0, 0, 0), ClosestPointOnLineSegment(qvec3f(0, 0, 0), qvec3f(0, 0, 0), qvec3f(10, 10, 10)));
}

TEST(polylib, ClosestPointOnPolyBoundary)
{
    // clockwise
    const std::vector<qvec3f> poly{
        {0, 0, 0}, // edge 0 start, edge 3 end
        {0, 64, 0}, // edge 1 start, edge 0 end
        {64, 64, 0}, // edge 2 start, edge 1 end
        {64, 0, 0} // edge 3 start, edge 2 end
    };

    EXPECT_EQ(std::make_pair(0, qvec3f(0, 0, 0)), ClosestPointOnPolyBoundary(poly, qvec3f(0, 0, 0)));

    // Either edge 1 or 2 contain the point qvec3f(64,64,0), but we expect the first edge to be returned
    EXPECT_EQ(std::make_pair(1, qvec3f(64, 64, 0)), ClosestPointOnPolyBoundary(poly, qvec3f(100, 100, 100)));
    EXPECT_EQ(std::make_pair(2, qvec3f(64, 32, 0)), ClosestPointOnPolyBoundary(poly, qvec3f(100, 32, 0)));

    EXPECT_EQ(std::make_pair(0, qvec3f(0, 0, 0)), ClosestPointOnPolyBoundary(poly, qvec3f(-1, -1, 0)));
}

TEST(polylib, PolygonCentroidEmpty)
{
    const std::initializer_list<qvec3d> empty{};
    const qvec3f res = qv::PolyCentroid(empty.begin(), empty.end());

    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(std::isnan(res[i]));
    }
}

TEST(polylib, PolygonCentroidPoint)
{
    const std::initializer_list<qvec3d> point{{1, 1, 1}};
    EXPECT_EQ(*point.begin(), qv::PolyCentroid(point.begin(), point.end()));
}

TEST(polylib, PolygonCentroidLine)
{
    const std::initializer_list<qvec3d> line{{0, 0, 0}, {2, 2, 2}};
    EXPECT_EQ(qvec3d(1, 1, 1), qv::PolyCentroid(line.begin(), line.end()));
}

TEST(polylib, PolygonCentroid)
{
    // poor test.. but at least checks that the colinear point is treated correctly
    const std::initializer_list<qvec3d> poly{{0, 0, 0}, {0, 32, 0}, // colinear
        {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    EXPECT_EQ(qvec3d(32, 32, 0), qv::PolyCentroid(poly.begin(), poly.end()));
}

TEST(mathlib, PolygonArea)
{
    // poor test.. but at least checks that the colinear point is treated correctly
    const std::initializer_list<qvec3d> poly{{0, 0, 0}, {0, 32, 0}, // colinear
        {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    EXPECT_EQ(64.0f * 64.0f, qv::PolyArea(poly.begin(), poly.end()));

    // 0, 1, or 2 vertices return 0 area
    EXPECT_EQ(0.0f, qv::PolyArea(poly.begin(), poly.begin()));
    EXPECT_EQ(0.0f, qv::PolyArea(poly.begin(), poly.begin() + 1));
    EXPECT_EQ(0.0f, qv::PolyArea(poly.begin(), poly.begin() + 2));
}

TEST(mathlib, BarycentricFromPoint)
{
    // clockwise
    const std::array<qvec3f, 3> tri{qvec3f{0, 0, 0}, {0, 64, 0}, {64, 0, 0}};

    EXPECT_EQ(qvec3f(1, 0, 0), qv::Barycentric_FromPoint(tri[0], tri[0], tri[1], tri[2]));
    EXPECT_EQ(qvec3f(0, 1, 0), qv::Barycentric_FromPoint(tri[1], tri[0], tri[1], tri[2]));
    EXPECT_EQ(qvec3f(0, 0, 1), qv::Barycentric_FromPoint(tri[2], tri[0], tri[1], tri[2]));

    EXPECT_EQ(qvec3f(0.5, 0.5, 0.0), qv::Barycentric_FromPoint({0, 32, 0}, tri[0], tri[1], tri[2]));
    EXPECT_EQ(qvec3f(0.0, 0.5, 0.5), qv::Barycentric_FromPoint({32, 32, 0}, tri[0], tri[1], tri[2]));
    EXPECT_EQ(qvec3f(0.5, 0.0, 0.5), qv::Barycentric_FromPoint({32, 0, 0}, tri[0], tri[1], tri[2]));
}

TEST(mathlib, BarycentricToPoint)
{
    // clockwise
    const std::array<qvec3f, 3> tri{qvec3f{0, 0, 0}, {0, 64, 0}, {64, 0, 0}};

    EXPECT_EQ(tri[0], qv::Barycentric_ToPoint({1, 0, 0}, tri[0], tri[1], tri[2]));
    EXPECT_EQ(tri[1], qv::Barycentric_ToPoint({0, 1, 0}, tri[0], tri[1], tri[2]));
    EXPECT_EQ(tri[2], qv::Barycentric_ToPoint({0, 0, 1}, tri[0], tri[1], tri[2]));

    EXPECT_EQ(qvec3f(0, 32, 0), qv::Barycentric_ToPoint({0.5, 0.5, 0.0}, tri[0], tri[1], tri[2]));
    EXPECT_EQ(qvec3f(32, 32, 0), qv::Barycentric_ToPoint({0.0, 0.5, 0.5}, tri[0], tri[1], tri[2]));
    EXPECT_EQ(qvec3f(32, 0, 0), qv::Barycentric_ToPoint({0.5, 0.0, 0.5}, tri[0], tri[1], tri[2]));
}

TEST(mathlib, BarycentricRandom)
{
    // clockwise
    const std::array<qvec3f, 3> tri{qvec3f{0, 0, 0}, {0, 64, 0}, {64, 0, 0}};

    const auto triAsVec = std::vector<qvec3f>{tri.begin(), tri.end()};
    const auto edges = MakeInwardFacingEdgePlanes(triAsVec);
    const auto plane = PolyPlane(triAsVec);

    for (int i = 0; i < 100; i++) {
        const float r0 = Random();
        const float r1 = Random();

        ASSERT_GE(r0, 0);
        ASSERT_GE(r1, 0);
        ASSERT_LE(r0, 1);
        ASSERT_LE(r1, 1);

        const auto bary = qv::Barycentric_Random(r0, r1);
        EXPECT_FLOAT_EQ(1.0f, bary[0] + bary[1] + bary[2]);

        const qvec3f point = qv::Barycentric_ToPoint(bary, tri[0], tri[1], tri[2]);
        EXPECT_TRUE(EdgePlanes_PointInside(edges, point));

        EXPECT_FLOAT_EQ(0.0f, DistAbovePlane(plane, point));
    }
}

TEST(mathlib, RotateFromUpToSurfaceNormal)
{
    std::mt19937 engine(0);
    std::uniform_real_distribution<float> dis(-4096, 4096);

    for (int i = 0; i < 100; i++) {
        const qvec3f randvec = qv::normalize(qvec3f(dis(engine), dis(engine), dis(engine)));
        const qmat3x3f m = RotateFromUpToSurfaceNormal(randvec);

        const qvec3f roundtrip = m * qvec3f(0, 0, 1);
        ASSERT_TRUE(qv::epsilonEqual(randvec, roundtrip, 0.01f));
    }
}

TEST(mathlib, MakePlane)
{
    EXPECT_EQ(qvec4f(0, 0, 1, 10), MakePlane(qvec3f(0, 0, 1), qvec3f(0, 0, 10)));
    EXPECT_EQ(qvec4f(0, 0, 1, 10), MakePlane(qvec3f(0, 0, 1), qvec3f(100, 100, 10)));
}

TEST(mathlib, DistAbovePlane)
{
    qvec4f plane(0, 0, 1, 10);
    qvec3f point(100, 100, 100);
    EXPECT_FLOAT_EQ(90, DistAbovePlane(plane, point));
}

TEST(mathlib, InterpolateNormalsDegenerate)
{
    EXPECT_FALSE(InterpolateNormal({}, std::vector<qvec3f>{}, qvec3f(0, 0, 0)).first);
    EXPECT_FALSE(InterpolateNormal({qvec3f(0, 0, 0)}, {qvec3f(0, 0, 1)}, qvec3f(0, 0, 0)).first);
    EXPECT_FALSE(
        InterpolateNormal({qvec3f(0, 0, 0), qvec3f(10, 0, 0)}, {qvec3f(0, 0, 1), qvec3f(0, 0, 1)}, qvec3f(0, 0, 0))
            .first);
}

TEST(mathlib, InterpolateNormals)
{
    // This test relies on the way InterpolateNormal is implemented

    // o--o--o
    // | / / |
    // |//   |
    // o-----o

    const std::vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {32, 64, 0}, // colinear
        {64, 64, 0}, {64, 0, 0}};

    const std::vector<qvec3f> normals{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, // colinear
        {0, 0, 0}, {-1, 0, 0}};

    // First try all the known points
    for (int i = 0; i < poly.size(); i++) {
        const auto res = InterpolateNormal(poly, normals, poly.at(i));
        EXPECT_EQ(true, res.first);
        EXPECT_TRUE(qv::epsilonEqual(normals.at(i), res.second, static_cast<float>(POINT_EQUAL_EPSILON)));
    }

    {
        const qvec3f firstTriCentroid = (poly[0] + poly[1] + poly[2]) / 3.0f;
        const auto res = InterpolateNormal(poly, normals, firstTriCentroid);
        EXPECT_EQ(true, res.first);
        EXPECT_TRUE(qv::epsilonEqual(qvec3f(1 / 3.0f), res.second, static_cast<float>(POINT_EQUAL_EPSILON)));
    }

    // Outside poly
    EXPECT_FALSE(InterpolateNormal(poly, normals, qvec3f(-0.1, 0, 0)).first);
}

static bool polysEqual(const std::vector<qvec3f> &p1, const std::vector<qvec3f> &p2)
{
    if (p1.size() != p2.size())
        return false;
    for (int i = 0; i < p1.size(); i++) {
        if (!qv::epsilonEqual(p1[i], p2[i], static_cast<float>(POINT_EQUAL_EPSILON)))
            return false;
    }
    return true;
}

TEST(polylib, ClipPoly1)
{
    const std::vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    const std::vector<qvec3f> frontRes{{0, 0, 0}, {0, 64, 0}, {32, 64, 0}, {32, 0, 0}};

    const std::vector<qvec3f> backRes{{32, 64, 0}, {64, 64, 0}, {64, 0, 0}, {32, 0, 0}};

    auto clipRes = ClipPoly(poly, qvec4f(-1, 0, 0, -32));

    EXPECT_TRUE(polysEqual(frontRes, clipRes.first));
    EXPECT_TRUE(polysEqual(backRes, clipRes.second));
}

TEST(polylib, ShrinkPoly1)
{
    const std::vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    const std::vector<qvec3f> shrunkPoly{{1, 1, 0}, {1, 63, 0}, {63, 63, 0}, {63, 1, 0}};

    const auto actualShrunk = ShrinkPoly(poly, 1.0f);

    EXPECT_TRUE(polysEqual(shrunkPoly, actualShrunk));
}

TEST(polylib, ShrinkPoly2)
{
    const std::vector<qvec3f> poly{{0, 0, 0}, {64, 64, 0}, {64, 0, 0}};

    const std::vector<qvec3f> shrunkPoly{
        {1.0f + sqrtf(2.0f), 1.0f, 0.0f},
        {63.0f, 63.0f - sqrtf(2.0f), 0.0f},
        {63, 1, 0},
    };

    const auto actualShrunk = ShrinkPoly(poly, 1.0f);

    EXPECT_TRUE(polysEqual(shrunkPoly, actualShrunk));
}

TEST(mathlib, SignedDegreesBetweenUnitVectors)
{
    const qvec3f up{0, 0, 1};
    const qvec3f fwd{0, 1, 0};
    const qvec3f right{1, 0, 0};

    EXPECT_FLOAT_EQ(-90, SignedDegreesBetweenUnitVectors(right, fwd, up));
    EXPECT_FLOAT_EQ(90, SignedDegreesBetweenUnitVectors(fwd, right, up));
    EXPECT_FLOAT_EQ(0, SignedDegreesBetweenUnitVectors(right, right, up));
}

TEST(mathlib, ConcavityTestConcave)
{
    const qvec3f face1center{0, 0, 10};
    const qvec3f face2center{10, 0, 200};

    const qvec3f face1normal{0, 0, 1};
    const qvec3f face2normal{-1, 0, 0};

    EXPECT_EQ(concavity_t::Concave, FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST(mathlib, ConcavityTestConcave2)
{
    const qvec3f face1center{0, 0, 10};
    const qvec3f face2center{-10, 0, 200};

    const qvec3f face1normal{0, 0, 1};
    const qvec3f face2normal{1, 0, 0};

    EXPECT_EQ(concavity_t::Concave, FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST(mathlib, ConcavityTestConvex)
{
    const qvec3f face1center{0, 0, 10};
    const qvec3f face2center{10, 0, 5};

    const qvec3f face1normal{0, 0, 1};
    const qvec3f face2normal{1, 0, 0};

    EXPECT_EQ(concavity_t::Convex, FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST(mathlib, ConcavityTestConvex2)
{
    const qvec3f face1center{0, 0, 10};
    const qvec3f face2center{-10, 0, 5};

    const qvec3f face1normal{0, 0, 1};
    const qvec3f face2normal{-1, 0, 0};

    EXPECT_EQ(concavity_t::Convex, FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST(mathlib, ConcavityTestCoplanar)
{
    const qvec3f face1center{0, 0, 10};
    const qvec3f face2center{100, 100, 10};

    const qvec3f face1normal{0, 0, 1};
    const qvec3f face2normal{0, 0, 1};

    EXPECT_EQ(concavity_t::Coplanar, FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

static const float MANGLE_EPSILON = 0.1f;

TEST(mathlib, vecFromMangle)
{
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(1, 0, 0), qv::vec_from_mangle(qvec3f(0, 0, 0)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(-1, 0, 0), qv::vec_from_mangle(qvec3f(180, 0, 0)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(0, 0, 1), qv::vec_from_mangle(qvec3f(0, 90, 0)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(0, 0, -1), qv::vec_from_mangle(qvec3f(0, -90, 0)), MANGLE_EPSILON));
}

TEST(mathlib, mangleFromVec)
{
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(0, 0, 0), qv::mangle_from_vec(qvec3f(1, 0, 0)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(180, 0, 0), qv::mangle_from_vec(qvec3f(-1, 0, 0)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(0, 90, 0), qv::mangle_from_vec(qvec3f(0, 0, 1)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(0, -90, 0), qv::mangle_from_vec(qvec3f(0, 0, -1)), MANGLE_EPSILON));

    for (int yaw = -179; yaw <= 179; yaw++) {
        for (int pitch = -89; pitch <= 89; pitch++) {
            const qvec3f origMangle = qvec3f(yaw, pitch, 0);
            const qvec3f vec = qv::vec_from_mangle(origMangle);
            const qvec3f roundtrip = qv::mangle_from_vec(vec);
            EXPECT_TRUE(qv::epsilonEqual(origMangle, roundtrip, MANGLE_EPSILON));
        }
    }
}

TEST(mathlib, bilinearInterpolate)
{
    const qvec4f v1(0, 1, 2, 3);
    const qvec4f v2(4, 5, 6, 7);
    const qvec4f v3(1, 1, 1, 1);
    const qvec4f v4(2, 2, 2, 2);

    EXPECT_EQ(v1, bilinearInterpolate(v1, v2, v3, v4, 0.0f, 0.0f));
    EXPECT_EQ(v2, bilinearInterpolate(v1, v2, v3, v4, 1.0f, 0.0f));
    EXPECT_EQ(v3, bilinearInterpolate(v1, v2, v3, v4, 0.0f, 1.0f));
    EXPECT_EQ(v4, bilinearInterpolate(v1, v2, v3, v4, 1.0f, 1.0f));

    EXPECT_EQ(qvec4f(1.5, 1.5, 1.5, 1.5), bilinearInterpolate(v1, v2, v3, v4, 0.5f, 1.0f));
    EXPECT_EQ(qvec4f(2, 3, 4, 5), bilinearInterpolate(v1, v2, v3, v4, 0.5f, 0.0f));
    EXPECT_EQ(qvec4f(1.75, 2.25, 2.75, 3.25), bilinearInterpolate(v1, v2, v3, v4, 0.5f, 0.5f));
}

TEST(mathlib, bilinearWeightsAndCoords)
{
    const auto res = bilinearWeightsAndCoords(qvec2f(0.5, 0.25), qvec2i(2, 2));

    qvec2f sum{};
    for (int i = 0; i < 4; i++) {
        const float weight = res[i].second;
        const qvec2i intPos = res[i].first;
        sum += qvec2f(intPos) * weight;
    }
    EXPECT_EQ(qvec2f(0.5, 0.25), sum);
}

TEST(mathlib, bilinearWeightsAndCoords2)
{
    const auto res = bilinearWeightsAndCoords(qvec2f(1.5, 0.5), qvec2i(2, 2));

    qvec2f sum{};
    for (int i = 0; i < 4; i++) {
        const float weight = res[i].second;
        const qvec2i intPos = res[i].first;
        sum += qvec2f(intPos) * weight;
    }
    EXPECT_EQ(qvec2f(1.0, 0.5), sum);
}

TEST(mathlib, pointsAlongLine)
{
    const auto res = PointsAlongLine(qvec3f(1, 0, 0), qvec3f(3.5, 0, 0), 1.5f);

    ASSERT_EQ(2, res.size());
    ASSERT_TRUE(qv::epsilonEqual(qvec3f(1, 0, 0), res[0], static_cast<float>(POINT_EQUAL_EPSILON)));
    ASSERT_TRUE(qv::epsilonEqual(qvec3f(2.5, 0, 0), res[1], static_cast<float>(POINT_EQUAL_EPSILON)));
}

// FIXME: this is failing
#if 0
TEST(RandomPointInPoly) {
    const vector<qvec3f> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear point
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const auto edgeplanes = MakeInwardFacingEdgePlanes(poly);
    
    qvec3f min(FLT_MAX);
    qvec3f max(-FLT_MAX);
    qvec3f avg{};
    
    const auto randomstate = PolyRandomPoint_Setup(poly);
    
    const int N=100;
    for (int i=0; i<N; i++) {
        const qvec3f point = PolyRandomPoint(randomstate, Random(), Random(), Random());
        ASSERT_TRUE(EdgePlanes_PointInside(edgeplanes, point));
        
        //std::cout << "point: " << qv::to_string(point) << std::endl;
        
        min = qv::min(min, point);
        max = qv::max(max, point);
        avg += point;
    }
    avg /= N;
    
    ASSERT_LT(min[0], 4);
    ASSERT_LT(min[1], 4);
    ASSERT_EQ(min[2], 0);
    
    ASSERT_GT(max[0], 60);
    ASSERT_GT(max[1], 60);
    ASSERT_EQ(max[2], 0);
    
    ASSERT_LT(qv::length(avg - qvec3f(32, 32, 0)), 4);
}
#endif

TEST(mathlib, FractionOfLine)
{
    ASSERT_FLOAT_EQ(0, FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0, 0, 0)));
    ASSERT_FLOAT_EQ(0.5, FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0.5, 0.5, 0.5)));
    ASSERT_FLOAT_EQ(1, FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(1, 1, 1)));
    ASSERT_FLOAT_EQ(2, FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(2, 2, 2)));
    ASSERT_FLOAT_EQ(-1, FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(-1, -1, -1)));

    ASSERT_FLOAT_EQ(0, FractionOfLine(qvec3f(0, 0, 0), qvec3f(0, 0, 0), qvec3f(0, 0, 0)));
}

TEST(mathlib, DistToLine)
{
    const float epsilon = 0.001;

    ASSERT_LT(fabs(0 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0, 0, 0))), epsilon);
    ASSERT_LT(fabs(0 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0.5, 0.5, 0.5))), epsilon);
    ASSERT_LT(fabs(0 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(1, 1, 1))), epsilon);
    ASSERT_LT(fabs(0 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(2, 2, 2))), epsilon);
    ASSERT_LT(fabs(0 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(-1, -1, -1))), epsilon);

    ASSERT_LT(fabs(sqrt(2) / 2 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 0), qvec3f(0, 1, 0))), epsilon);
    ASSERT_LT(fabs(sqrt(2) / 2 - DistToLine(qvec3f(0, 0, 0), qvec3f(1, 1, 0), qvec3f(1, 0, 0))), epsilon);

    ASSERT_LT(fabs(0.5 - DistToLine(qvec3f(10, 0, 0), qvec3f(10, 0, 100), qvec3f(9.5, 0, 0))), epsilon);
}

TEST(mathlib, DistToLineSegment)
{
    const float epsilon = 0.001;

    ASSERT_LT(fabs(0 - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0, 0, 0))), epsilon);
    ASSERT_LT(fabs(0 - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0.5, 0.5, 0.5))), epsilon);
    ASSERT_LT(fabs(0 - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(1, 1, 1))), epsilon);
    ASSERT_LT(fabs(sqrt(3) - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(2, 2, 2))), epsilon);
    ASSERT_LT(fabs(sqrt(3) - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(-1, -1, -1))), epsilon);

    ASSERT_LT(fabs(sqrt(2) / 2 - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 0), qvec3f(0, 1, 0))), epsilon);
    ASSERT_LT(fabs(sqrt(2) / 2 - DistToLineSegment(qvec3f(0, 0, 0), qvec3f(1, 1, 0), qvec3f(1, 0, 0))), epsilon);

    ASSERT_LT(fabs(0.5 - DistToLineSegment(qvec3f(10, 0, 0), qvec3f(10, 0, 100), qvec3f(9.5, 0, 0))), epsilon);
}

TEST(mathlib, linesOverlapPoints)
{
    ASSERT_TRUE(LinesOverlap({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}));
}

TEST(mathlib, linesOverlapPointLine)
{
    ASSERT_TRUE(LinesOverlap({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 1}));
}

TEST(mathlib, linesOverlapSame)
{
    ASSERT_TRUE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 0}, {0, 0, 1}));
}

TEST(mathlib, linesOverlapSameOppositeDir)
{
    ASSERT_TRUE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 1}, {0, 0, 0}));
}

TEST(mathlib, linesOverlapOverlap)
{
    ASSERT_TRUE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 0.5}, {0, 0, 1.5}));
}

TEST(mathlib, linesOverlapOverlapOppositeDir)
{
    ASSERT_TRUE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 1.5}, {0, 0, 0.5}));
}

TEST(mathlib, linesOverlapOnlyTipsTouching)
{
    ASSERT_TRUE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 1}, {0, 0, 2}));
}

TEST(mathlib, linesOverlapNonColinear)
{
    ASSERT_FALSE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {5, 0, 0}, {5, 0, 1}));
}

TEST(mathlib, linesOverlapColinearNotTouching)
{
    ASSERT_FALSE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 0, 3}));
}

// qvec

TEST(mathlib, qvecExpand)
{
    const qvec2f test(1, 2);
    const qvec4f test2(test);

    EXPECT_EQ(1, test2[0]);
    EXPECT_EQ(2, test2[1]);
    EXPECT_EQ(0, test2[2]);
    EXPECT_EQ(0, test2[3]);
}

TEST(mathlib, qvecContract)
{
    const qvec4f test(1, 2, 0, 0);
    const qvec2f test2(test);

    EXPECT_EQ(1, test2[0]);
    EXPECT_EQ(2, test2[1]);
}

TEST(mathlib, qvecCopy)
{
    const qvec2f test(1, 2);
    const qvec2f test2(test);

    EXPECT_EQ(1, test2[0]);
    EXPECT_EQ(2, test2[1]);
}

TEST(mathlib, qvecConstructorInit)
{
    const qvec2f test{};
    EXPECT_EQ(0, test[0]);
    EXPECT_EQ(0, test[1]);
}

TEST(mathlib, qvecConstructor1)
{
    const qvec2f test(42);
    EXPECT_EQ(42, test[0]);
    EXPECT_EQ(42, test[1]);
}

TEST(mathlib, qvecConstructorFewer)
{
    const qvec4f test(1, 2, 3);
    EXPECT_EQ(1, test[0]);
    EXPECT_EQ(2, test[1]);
    EXPECT_EQ(3, test[2]);
    EXPECT_EQ(0, test[3]);
}

TEST(mathlib, qvecConstructorExtra)
{
    const qvec2f test(1, 2, 3);
    EXPECT_EQ(1, test[0]);
    EXPECT_EQ(2, test[1]);
}

TEST(mathlib, qvecGTestPrint)
{
    const qvec2f test(1, 2);

    EXPECT_EQ(testing::PrintToString(test), "1 2");
}

TEST(mathlib, qvecFmtFormat)
{
    const qvec2f test(1, 2);

    EXPECT_EQ("1 2", fmt::format("{}", test));
}

// aabb3f

TEST(mathlib, aabbBasic)
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    EXPECT_EQ(qvec3f(1, 1, 1), b1.mins());
    EXPECT_EQ(qvec3f(10, 10, 10), b1.maxs());
    EXPECT_EQ(qvec3f(9, 9, 9), b1.size());
}

TEST(mathlib, aabbGrow)
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    EXPECT_EQ(aabb3f(qvec3f(0, 0, 0), qvec3f(11, 11, 11)), b1.grow(qvec3f(1, 1, 1)));
}

TEST(mathlib, aabbUnionwith)
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));
    const aabb3f b2(qvec3f(11, 11, 11), qvec3f(12, 12, 12));

    EXPECT_EQ(aabb3f(qvec3f(1, 1, 1), qvec3f(12, 12, 12)), b1.unionWith(b2));
}

TEST(mathlib, aabbExpand)
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    EXPECT_EQ(b1, b1.expand(qvec3f(1, 1, 1)));
    EXPECT_EQ(b1, b1.expand(qvec3f(5, 5, 5)));
    EXPECT_EQ(b1, b1.expand(qvec3f(10, 10, 10)));

    const aabb3f b2(qvec3f(1, 1, 1), qvec3f(100, 10, 10));
    EXPECT_EQ(b2, b1.expand(qvec3f(100, 10, 10)));

    const aabb3f b3(qvec3f(0, 1, 1), qvec3f(10, 10, 10));
    EXPECT_EQ(b3, b1.expand(qvec3f(0, 1, 1)));
}

TEST(mathlib, aabbDisjoint)
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    const aabb3f yes1(qvec3f(-1, -1, -1), qvec3f(0, 0, 0));
    const aabb3f yes2(qvec3f(11, 1, 1), qvec3f(12, 10, 10));

    const aabb3f no1(qvec3f(-1, -1, -1), qvec3f(1, 1, 1));
    const aabb3f no2(qvec3f(10, 10, 10), qvec3f(10.5, 10.5, 10.5));
    const aabb3f no3(qvec3f(5, 5, 5), qvec3f(100, 6, 6));

    EXPECT_TRUE(b1.disjoint(yes1));
    EXPECT_TRUE(b1.disjoint(yes2));
    EXPECT_FALSE(b1.disjoint(no1));
    EXPECT_FALSE(b1.disjoint(no2));
    EXPECT_FALSE(b1.disjoint(no3));

    EXPECT_FALSE(b1.intersectWith(yes1));
    EXPECT_FALSE(b1.intersectWith(yes2));

    // these intersections are single points
    EXPECT_EQ(aabb3f::intersection_t(aabb3f(qvec3f(1, 1, 1), qvec3f(1, 1, 1))), b1.intersectWith(no1));
    EXPECT_EQ(aabb3f::intersection_t(aabb3f(qvec3f(10, 10, 10), qvec3f(10, 10, 10))), b1.intersectWith(no2));

    // an intersection with a volume
    EXPECT_EQ(aabb3f::intersection_t(aabb3f(qvec3f(5, 5, 5), qvec3f(10, 6, 6))), b1.intersectWith(no3));

    EXPECT_TRUE(b1.disjoint_or_touching(aabb3f(qvec3f(10, 1, 1), qvec3f(20, 10, 10))));
    EXPECT_TRUE(b1.disjoint_or_touching(aabb3f(qvec3f(11, 1, 1), qvec3f(20, 10, 10))));
    EXPECT_FALSE(b1.disjoint_or_touching(aabb3f(qvec3f(9.99, 1, 1), qvec3f(20, 10, 10))));
}

TEST(mathlib, aabbContains)
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    const aabb3f yes1(qvec3f(1, 1, 1), qvec3f(2, 2, 2));
    const aabb3f yes2(qvec3f(9, 9, 9), qvec3f(10, 10, 10));

    const aabb3f no1(qvec3f(-1, 1, 1), qvec3f(2, 2, 2));
    const aabb3f no2(qvec3f(9, 9, 9), qvec3f(10.5, 10, 10));

    EXPECT_TRUE(b1.contains(yes1));
    EXPECT_TRUE(b1.contains(yes2));
    EXPECT_FALSE(b1.contains(no1));
    EXPECT_FALSE(b1.contains(no2));
}

TEST(mathlib, aabbContainsPoint)
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

    const qvec3f yes1(1, 1, 1);
    const qvec3f yes2(2, 2, 2);
    const qvec3f yes3(10, 10, 10);

    const qvec3f no1(0, 0, 0);
    const qvec3f no2(1, 1, 0);
    const qvec3f no3(10.1, 10.1, 10.1);

    EXPECT_TRUE(b1.containsPoint(yes1));
    EXPECT_TRUE(b1.containsPoint(yes2));
    EXPECT_TRUE(b1.containsPoint(yes3));
    EXPECT_FALSE(b1.containsPoint(no1));
    EXPECT_FALSE(b1.containsPoint(no2));
    EXPECT_FALSE(b1.containsPoint(no3));
}

TEST(mathlib, aabbCreateInvalid)
{
    const aabb3f b1(qvec3f(1, 1, 1), qvec3f(-1, -1, -1));
    const aabb3f fixed(qvec3f(1, 1, 1), qvec3f(1, 1, 1));

    EXPECT_EQ(fixed, b1);
    EXPECT_EQ(qvec3f(0, 0, 0), b1.size());
}

TEST(qmat, matrix2x2inv)
{
    std::mt19937 engine(0);
    std::uniform_real_distribution<float> dis(-4096, 4096);

    qmat2x2f randMat;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            randMat.at(i, j) = dis(engine);

    qmat2x2f randInv = qv::inverse(randMat);
    ASSERT_FALSE(std::isnan(randInv.at(0, 0)));

    qmat2x2f prod = randMat * randInv;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            float exp = (i == j) ? 1.0f : 0.0f;
            ASSERT_LT(fabs(exp - prod.at(i, j)), 0.001);
        }
    }

    // check non-invertible gives nan
    qmat2x2f nanMat = qv::inverse(qmat2x2f(0));
    ASSERT_TRUE(std::isnan(nanMat.at(0, 0)));
}

TEST(qmat, matrix3x3inv)
{
    std::mt19937 engine(0);
    std::uniform_real_distribution<float> dis(-4096, 4096);

    qmat3x3f randMat;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            randMat.at(i, j) = dis(engine);

    qmat3x3f randInv = qv::inverse(randMat);
    ASSERT_FALSE(std::isnan(randInv.at(0, 0)));

    qmat3x3f prod = randMat * randInv;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float exp = (i == j) ? 1.0f : 0.0f;
            ASSERT_LT(fabs(exp - prod.at(i, j)), 0.001);
        }
    }

    // check non-invertible gives nan
    qmat3x3f nanMat = qv::inverse(qmat3x3f(0));
    ASSERT_TRUE(std::isnan(nanMat.at(0, 0)));
}

TEST(qmat, matrix4x4inv)
{
    std::mt19937 engine(0);
    std::uniform_real_distribution<float> dis(-4096, 4096);

    qmat4x4f randMat;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            randMat.at(i, j) = dis(engine);

    qmat4x4f randInv = qv::inverse(randMat);
    ASSERT_FALSE(std::isnan(randInv.at(0, 0)));

    qmat4x4f prod = randMat * randInv;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float exp = (i == j) ? 1.0f : 0.0f;
            ASSERT_LT(fabs(exp - prod.at(i, j)), 0.001);
        }
    }

    // check non-invertible gives nan
    qmat4x4f nanMat = qv::inverse(qmat4x4f(0));
    ASSERT_TRUE(std::isnan(nanMat.at(0, 0)));
}

TEST(qmat, qmatConstructInitialize)
{
    const qmat2x2f test{1, 2, 3, 4}; // column major

    EXPECT_EQ(qvec2f(1, 3), test.row(0));
    EXPECT_EQ(qvec2f(2, 4), test.row(1));
}

TEST(qmat, qmatConstructRowMajor)
{
    const qmat2x2f test = qmat2x2f::row_major({1, 2, 3, 4});

    EXPECT_EQ(qvec2f(1, 2), test.row(0));
    EXPECT_EQ(qvec2f(3, 4), test.row(1));
}

TEST(mathlib, clampTexcoordSmall)
{
    // positive
    EXPECT_EQ(0, clamp_texcoord(0.0f, 2));
    EXPECT_EQ(0, clamp_texcoord(0.5f, 2));
    EXPECT_EQ(1, clamp_texcoord(1.0f, 2));
    EXPECT_EQ(1, clamp_texcoord(1.5f, 2));
    EXPECT_EQ(0, clamp_texcoord(2.0f, 2));
    EXPECT_EQ(0, clamp_texcoord(2.5f, 2));

    // negative
    EXPECT_EQ(1, clamp_texcoord(-0.5f, 2));
    EXPECT_EQ(1, clamp_texcoord(-1.0f, 2));
    EXPECT_EQ(0, clamp_texcoord(-1.5f, 2));
    EXPECT_EQ(0, clamp_texcoord(-2.0f, 2));
    EXPECT_EQ(1, clamp_texcoord(-2.5f, 2));
}

TEST(mathlib, clampTexcoord)
{
    // positive
    EXPECT_EQ(0, clamp_texcoord(0.0f, 128));
    EXPECT_EQ(64, clamp_texcoord(64.0f, 128));
    EXPECT_EQ(64, clamp_texcoord(64.5f, 128));
    EXPECT_EQ(127, clamp_texcoord(127.0f, 128));
    EXPECT_EQ(0, clamp_texcoord(128.0f, 128));
    EXPECT_EQ(1, clamp_texcoord(129.0f, 128));

    // negative
    EXPECT_EQ(127, clamp_texcoord(-0.5f, 128));
    EXPECT_EQ(127, clamp_texcoord(-1.0f, 128));
    EXPECT_EQ(1, clamp_texcoord(-127.0f, 128));
    EXPECT_EQ(0, clamp_texcoord(-127.5f, 128));
    EXPECT_EQ(0, clamp_texcoord(-128.0f, 128));
    EXPECT_EQ(127, clamp_texcoord(-129.0f, 128));
}

TEST(mathlib, windingFormat)
{
    const polylib::winding_t poly{{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

    const char *exp = "{(0 0 0), (0 64 0), (64 64 0), (64 0 0)}";

    EXPECT_EQ(exp, testing::PrintToString(poly));
    EXPECT_EQ(exp, fmt::format("{}", poly));
}

TEST(Settings, delayDefault)
{
    light_t light;
    EXPECT_EQ(LF_LINEAR, light.formula.value());
}

TEST(Settings, delayParseInt)
{
    light_t light;
    parser_t p("2", {});
    EXPECT_TRUE(light.formula.parse(light.formula.primary_name(), p, settings::source::MAP));
    EXPECT_EQ(LF_INVERSE2, light.formula.value());
}

TEST(Settings, delayParseIntUnknown)
{
    light_t light;
    parser_t p("500", {});
    EXPECT_TRUE(light.formula.parse(light.formula.primary_name(), p, settings::source::MAP));
    // not sure if we should be strict and reject parsing this?
    EXPECT_EQ(500, light.formula.value());
}

TEST(Settings, delayParseFloat)
{
    light_t light;
    parser_t p("2.0", {});
    EXPECT_TRUE(light.formula.parse(light.formula.primary_name(), p, settings::source::MAP));
    EXPECT_EQ(LF_INVERSE2, light.formula.value());
}

TEST(Settings, delayParseString)
{
    light_t light;
    parser_t p("inverse2", {});
    EXPECT_TRUE(light.formula.parse(light.formula.primary_name(), p, settings::source::MAP));
    EXPECT_EQ(LF_INVERSE2, light.formula.value());
}

TEST(LightFormats, e5bgr9pack1)
{
    uint32_t packed = HDR_PackE5BRG9(qvec3f{511.0f, 1.0f, 0.0f});

    //                  e          | b         | g        | r
    uint32_t expected = (24 << 27) | (0 << 18) | (1 << 9) | (511 << 0);
    EXPECT_EQ(packed, expected);

    qvec3f roundtripped = HDR_UnpackE5BRG9(expected);
    EXPECT_EQ(roundtripped[0], 511);
    EXPECT_EQ(roundtripped[1], 1);
    EXPECT_EQ(roundtripped[2], 0);
}

TEST(LightFormats, e5bgr9pack2)
{
    uint32_t packed = HDR_PackE5BRG9(qvec3f{1'000'000.0f, 0.0f, 0.0f});

    //                  e            | b         | g        | r
    uint32_t expected = (0x1f << 27) | (0 << 18) | (0 << 9) | (0x1ff << 0);
    EXPECT_EQ(packed, expected);

    qvec3f roundtripped = HDR_UnpackE5BRG9(packed);
    EXPECT_EQ(roundtripped[0], 65408.0f);
    EXPECT_EQ(roundtripped[1], 0.0f);
    EXPECT_EQ(roundtripped[2], 0.0f);
}

TEST(LightFormats, e5bgr9pack3)
{
    qvec3f in = qvec3f{0.1, 0.01, 0.001};
    uint32_t packed = HDR_PackE5BRG9(in);

    qvec3f roundtripped = HDR_UnpackE5BRG9(packed);
    qvec3f error = qv::abs(in - roundtripped);

    EXPECT_LT(error[0], 0.000098);
    EXPECT_LT(error[1], 0.00001);
    EXPECT_LT(error[2], 0.000025);
}
