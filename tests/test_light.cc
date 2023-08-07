#include <doctest/doctest.h>

#include <light/light.hh>
#include <light/trace.hh> // for clamp_texcoord
#include <light/entities.hh>

#include <random>
#include <algorithm> // for std::sort

#include <common/qvec.hh>

#include <common/aabb.hh>

TEST_SUITE("mathlib")
{

    TEST_CASE("MakeCDF")
    {

        std::vector<float> pdfUnnormzlied{25, 50, 25};
        std::vector<float> cdf = MakeCDF(pdfUnnormzlied);

        REQUIRE(3u == cdf.size());
        REQUIRE(doctest::Approx(0.25) == cdf.at(0));
        REQUIRE(doctest::Approx(0.75) == cdf.at(1));
        REQUIRE(doctest::Approx(1.0) == cdf.at(2));

        // TODO: return pdf
        REQUIRE(0 == SampleCDF(cdf, 0));
        REQUIRE(0 == SampleCDF(cdf, 0.1));
        REQUIRE(0 == SampleCDF(cdf, 0.25));
        REQUIRE(1 == SampleCDF(cdf, 0.26));
        REQUIRE(1 == SampleCDF(cdf, 0.75));
        REQUIRE(2 == SampleCDF(cdf, 0.76));
        REQUIRE(2 == SampleCDF(cdf, 1));
    }

    static void checkBox(const std::vector<qvec4f> &edges, const std::vector<qvec3f> &poly)
    {
        CHECK(EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
        CHECK(EdgePlanes_PointInside(edges, qvec3f(64, 0, 0)));
        CHECK(EdgePlanes_PointInside(edges, qvec3f(32, 32, 0)));
        CHECK(EdgePlanes_PointInside(edges, qvec3f(32, 32, 32))); // off plane

        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(-0.1, 0, 0)));
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(64.1, 0, 0)));
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(0, -0.1, 0)));
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(0, 64.1, 0)));
    }

    TEST_CASE("EdgePlanesOfNonConvexPoly")
    {
        // hourglass, non-convex
        const std::vector<qvec3f> poly{{0, 0, 0}, {64, 64, 0}, {0, 64, 0}, {64, 0, 0}};

        const auto edges = MakeInwardFacingEdgePlanes(poly);
        //    CHECK(vector<qvec4f>() == edges);
    }

    TEST_CASE("SlightlyConcavePoly")
    {
        const std::vector<qvec3f> poly{qvec3f(225.846161, -1744, 1774), qvec3f(248, -1744, 1798),
            qvec3f(248, -1763.82605, 1799.65222), qvec3f(248, -1764, 1799.66663), qvec3f(248, -1892, 1810.33337),
            qvec3f(248, -1893.21741, 1810.43481), qvec3f(248, -1921.59998, 1812.80005), qvec3f(248, -1924, 1813),
            qvec3f(80, -1924, 1631), qvec3f(80, -1744, 1616)};

        const auto edges = MakeInwardFacingEdgePlanes(poly);
        REQUIRE_FALSE(edges.empty());
        CHECK(EdgePlanes_PointInside(edges, qvec3f(152.636963, -1814, 1702)));
    }

    TEST_CASE("PointInPolygon")
    {
        // clockwise
        const std::vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

        const auto edges = MakeInwardFacingEdgePlanes(poly);
        checkBox(edges, poly);
    }

    TEST_CASE("PointInPolygon_DegenerateEdgeHandling")
    {
        // clockwise
        const std::vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {0, 64, 0}, // repeat of last point
            {64, 64, 0}, {64, 0, 0}};

        const auto edges = MakeInwardFacingEdgePlanes(poly);
        checkBox(edges, poly);
    }

    TEST_CASE("PointInPolygon_DegenerateFaceHandling1")
    {
        const std::vector<qvec3f> poly{};

        const auto edges = MakeInwardFacingEdgePlanes(poly);
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(10, 10, 10)));
    }

    TEST_CASE("PointInPolygon_DegenerateFaceHandling2")
    {
        const std::vector<qvec3f> poly{
            {0, 0, 0},
            {0, 0, 0},
            {0, 0, 0},
        };

        const auto edges = MakeInwardFacingEdgePlanes(poly);
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(10, 10, 10)));
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(-10, -10, -10)));
    }

    TEST_CASE("PointInPolygon_DegenerateFaceHandling3")
    {
        const std::vector<qvec3f> poly{
            {0, 0, 0},
            {10, 10, 10},
            {20, 20, 20},
        };

        const auto edges = MakeInwardFacingEdgePlanes(poly);
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(0, 0, 0)));
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(10, 10, 10)));
        CHECK_FALSE(EdgePlanes_PointInside(edges, qvec3f(-10, -10, -10)));
    }

    TEST_CASE("PointInPolygon_ColinearPointHandling")
    {
        // clockwise
        const std::vector<qvec3f> poly{{0, 0, 0}, {0, 32, 0}, // colinear
            {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

        const auto edges = MakeInwardFacingEdgePlanes(poly);

        checkBox(edges, poly);
    }

    TEST_CASE("ClosestPointOnLineSegment_Degenerate")
    {
        CHECK(qvec3f(0, 0, 0) == ClosestPointOnLineSegment(qvec3f(0, 0, 0), qvec3f(0, 0, 0), qvec3f(10, 10, 10)));
    }

    TEST_CASE("ClosestPointOnPolyBoundary")
    {
        // clockwise
        const std::vector<qvec3f> poly{
            {0, 0, 0}, // edge 0 start, edge 3 end
            {0, 64, 0}, // edge 1 start, edge 0 end
            {64, 64, 0}, // edge 2 start, edge 1 end
            {64, 0, 0} // edge 3 start, edge 2 end
        };

        CHECK(std::make_pair(0, qvec3f(0, 0, 0)) == ClosestPointOnPolyBoundary(poly, qvec3f(0, 0, 0)));

        // Either edge 1 or 2 contain the point qvec3f(64,64,0), but we expect the first edge to be returned
        CHECK(std::make_pair(1, qvec3f(64, 64, 0)) == ClosestPointOnPolyBoundary(poly, qvec3f(100, 100, 100)));
        CHECK(std::make_pair(2, qvec3f(64, 32, 0)) == ClosestPointOnPolyBoundary(poly, qvec3f(100, 32, 0)));

        CHECK(std::make_pair(0, qvec3f(0, 0, 0)) == ClosestPointOnPolyBoundary(poly, qvec3f(-1, -1, 0)));
    }

    TEST_CASE("PolygonCentroid_empty")
    {
        const std::initializer_list<qvec3d> empty{};
        const qvec3f res = qv::PolyCentroid(empty.begin(), empty.end());

        for (int i = 0; i < 3; i++) {
            CHECK(std::isnan(res[i]));
        }
    }

    TEST_CASE("PolygonCentroid_point")
    {
        const std::initializer_list<qvec3d> point{{1, 1, 1}};
        CHECK(*point.begin() == qv::PolyCentroid(point.begin(), point.end()));
    }

    TEST_CASE("PolygonCentroid_line")
    {
        const std::initializer_list<qvec3d> line{{0, 0, 0}, {2, 2, 2}};
        CHECK(qvec3d(1, 1, 1) == qv::PolyCentroid(line.begin(), line.end()));
    }

    TEST_CASE("PolygonCentroid")
    {
        // poor test.. but at least checks that the colinear point is treated correctly
        const std::initializer_list<qvec3d> poly{{0, 0, 0}, {0, 32, 0}, // colinear
            {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

        CHECK(qvec3d(32, 32, 0) == qv::PolyCentroid(poly.begin(), poly.end()));
    }

    TEST_CASE("PolygonArea")
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

    TEST_CASE("BarycentricFromPoint")
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

    TEST_CASE("BarycentricToPoint")
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

    TEST_CASE("BarycentricRandom")
    {
        // clockwise
        const std::array<qvec3f, 3> tri{qvec3f{0, 0, 0}, {0, 64, 0}, {64, 0, 0}};

        const auto triAsVec = std::vector<qvec3f>{tri.begin(), tri.end()};
        const auto edges = MakeInwardFacingEdgePlanes(triAsVec);
        const auto plane = PolyPlane(triAsVec);

        for (int i = 0; i < 100; i++) {
            const float r0 = Random();
            const float r1 = Random();

            REQUIRE(r0 >= 0);
            REQUIRE(r1 >= 0);
            REQUIRE(r0 <= 1);
            REQUIRE(r1 <= 1);

            const auto bary = qv::Barycentric_Random(r0, r1);
            CHECK(doctest::Approx(1.0f) == bary[0] + bary[1] + bary[2]);

            const qvec3f point = qv::Barycentric_ToPoint(bary, tri[0], tri[1], tri[2]);
            CHECK(EdgePlanes_PointInside(edges, point));

            CHECK(doctest::Approx(0.0f) == DistAbovePlane(plane, point));
        }
    }

    TEST_CASE("RotateFromUpToSurfaceNormal")
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

    TEST_CASE("MakePlane")
    {
        CHECK(qvec4f(0, 0, 1, 10) == MakePlane(qvec3f(0, 0, 1), qvec3f(0, 0, 10)));
        CHECK(qvec4f(0, 0, 1, 10) == MakePlane(qvec3f(0, 0, 1), qvec3f(100, 100, 10)));
    }

    TEST_CASE("DistAbovePlane")
    {
        qvec4f plane(0, 0, 1, 10);
        qvec3f point(100, 100, 100);
        CHECK(doctest::Approx(90) == DistAbovePlane(plane, point));
    }

    TEST_CASE("InterpolateNormalsDegenerate")
    {
        CHECK_FALSE(InterpolateNormal({}, std::vector<qvec3f>{}, qvec3f(0, 0, 0)).first);
        CHECK_FALSE(InterpolateNormal({qvec3f(0, 0, 0)}, {qvec3f(0, 0, 1)}, qvec3f(0, 0, 0)).first);
        CHECK_FALSE(
            InterpolateNormal({qvec3f(0, 0, 0), qvec3f(10, 0, 0)}, {qvec3f(0, 0, 1), qvec3f(0, 0, 1)}, qvec3f(0, 0, 0))
                .first);
    }

    TEST_CASE("InterpolateNormals")
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
            CHECK(true == res.first);
            CHECK(qv::epsilonEqual(normals.at(i), res.second, static_cast<float>(POINT_EQUAL_EPSILON)));
        }

        {
            const qvec3f firstTriCentroid = (poly[0] + poly[1] + poly[2]) / 3.0f;
            const auto res = InterpolateNormal(poly, normals, firstTriCentroid);
            CHECK(true == res.first);
            CHECK(qv::epsilonEqual(qvec3f(1 / 3.0f), res.second, static_cast<float>(POINT_EQUAL_EPSILON)));
        }

        // Outside poly
        CHECK_FALSE(InterpolateNormal(poly, normals, qvec3f(-0.1, 0, 0)).first);
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

    TEST_CASE("ClipPoly1")
    {
        const std::vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

        const std::vector<qvec3f> frontRes{{0, 0, 0}, {0, 64, 0}, {32, 64, 0}, {32, 0, 0}};

        const std::vector<qvec3f> backRes{{32, 64, 0}, {64, 64, 0}, {64, 0, 0}, {32, 0, 0}};

        auto clipRes = ClipPoly(poly, qvec4f(-1, 0, 0, -32));

        CHECK(polysEqual(frontRes, clipRes.first));
        CHECK(polysEqual(backRes, clipRes.second));
    }

    TEST_CASE("ShrinkPoly1")
    {
        const std::vector<qvec3f> poly{{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};

        const std::vector<qvec3f> shrunkPoly{{1, 1, 0}, {1, 63, 0}, {63, 63, 0}, {63, 1, 0}};

        const auto actualShrunk = ShrinkPoly(poly, 1.0f);

        CHECK(polysEqual(shrunkPoly, actualShrunk));
    }

    TEST_CASE("ShrinkPoly2")
    {
        const std::vector<qvec3f> poly{{0, 0, 0}, {64, 64, 0}, {64, 0, 0}};

        const std::vector<qvec3f> shrunkPoly{
            {1.0f + sqrtf(2.0f), 1.0f, 0.0f},
            {63.0f, 63.0f - sqrtf(2.0f), 0.0f},
            {63, 1, 0},
        };

        const auto actualShrunk = ShrinkPoly(poly, 1.0f);

        CHECK(polysEqual(shrunkPoly, actualShrunk));
    }

    TEST_CASE("SignedDegreesBetweenUnitVectors")
    {
        const qvec3f up{0, 0, 1};
        const qvec3f fwd{0, 1, 0};
        const qvec3f right{1, 0, 0};

        CHECK(doctest::Approx(-90) == SignedDegreesBetweenUnitVectors(right, fwd, up));
        CHECK(doctest::Approx(90) == SignedDegreesBetweenUnitVectors(fwd, right, up));
        CHECK(doctest::Approx(0) == SignedDegreesBetweenUnitVectors(right, right, up));
    }

    TEST_CASE("ConcavityTest_concave")
    {
        const qvec3f face1center{0, 0, 10};
        const qvec3f face2center{10, 0, 200};

        const qvec3f face1normal{0, 0, 1};
        const qvec3f face2normal{-1, 0, 0};

        CHECK(concavity_t::Concave == FacePairConcavity(face1center, face1normal, face2center, face2normal));
    }

    TEST_CASE("ConcavityTest_concave2")
    {
        const qvec3f face1center{0, 0, 10};
        const qvec3f face2center{-10, 0, 200};

        const qvec3f face1normal{0, 0, 1};
        const qvec3f face2normal{1, 0, 0};

        CHECK(concavity_t::Concave == FacePairConcavity(face1center, face1normal, face2center, face2normal));
    }

    TEST_CASE("ConcavityTest_convex")
    {
        const qvec3f face1center{0, 0, 10};
        const qvec3f face2center{10, 0, 5};

        const qvec3f face1normal{0, 0, 1};
        const qvec3f face2normal{1, 0, 0};

        CHECK(concavity_t::Convex == FacePairConcavity(face1center, face1normal, face2center, face2normal));
    }

    TEST_CASE("ConcavityTest_convex2")
    {
        const qvec3f face1center{0, 0, 10};
        const qvec3f face2center{-10, 0, 5};

        const qvec3f face1normal{0, 0, 1};
        const qvec3f face2normal{-1, 0, 0};

        CHECK(concavity_t::Convex == FacePairConcavity(face1center, face1normal, face2center, face2normal));
    }

    TEST_CASE("ConcavityTest_coplanar")
    {
        const qvec3f face1center{0, 0, 10};
        const qvec3f face2center{100, 100, 10};

        const qvec3f face1normal{0, 0, 1};
        const qvec3f face2normal{0, 0, 1};

        CHECK(concavity_t::Coplanar == FacePairConcavity(face1center, face1normal, face2center, face2normal));
    }
}

static const float MANGLE_EPSILON = 0.1f;

TEST_SUITE("light")
{

    TEST_CASE("vec_from_mangle")
    {
        CHECK(qv::epsilonEqual(qvec3f(1, 0, 0), qv::vec_from_mangle(qvec3f(0, 0, 0)), MANGLE_EPSILON));
        CHECK(qv::epsilonEqual(qvec3f(-1, 0, 0), qv::vec_from_mangle(qvec3f(180, 0, 0)), MANGLE_EPSILON));
        CHECK(qv::epsilonEqual(qvec3f(0, 0, 1), qv::vec_from_mangle(qvec3f(0, 90, 0)), MANGLE_EPSILON));
        CHECK(qv::epsilonEqual(qvec3f(0, 0, -1), qv::vec_from_mangle(qvec3f(0, -90, 0)), MANGLE_EPSILON));
    }

    TEST_CASE("mangle_from_vec")
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

    TEST_CASE("bilinearInterpolate")
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

    TEST_CASE("bilinearWeightsAndCoords")
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

    TEST_CASE("bilinearWeightsAndCoords2")
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

    TEST_CASE("pointsAlongLine")
    {
        const auto res = PointsAlongLine(qvec3f(1, 0, 0), qvec3f(3.5, 0, 0), 1.5f);

        REQUIRE(2 == res.size());
        REQUIRE(qv::epsilonEqual(qvec3f(1, 0, 0), res[0], static_cast<float>(POINT_EQUAL_EPSILON)));
        REQUIRE(qv::epsilonEqual(qvec3f(2.5, 0, 0), res[1], static_cast<float>(POINT_EQUAL_EPSILON)));
    }

// FIXME: this is failing
#if 0
TEST_CASE("RandomPointInPoly") {
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
        REQUIRE(EdgePlanes_PointInside(edgeplanes, point));
        
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

    TEST_CASE("FractionOfLine")
    {
        REQUIRE(doctest::Approx(0) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0, 0, 0)));
        REQUIRE(doctest::Approx(0.5) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(0.5, 0.5, 0.5)));
        REQUIRE(doctest::Approx(1) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(1, 1, 1)));
        REQUIRE(doctest::Approx(2) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(2, 2, 2)));
        REQUIRE(doctest::Approx(-1) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(1, 1, 1), qvec3f(-1, -1, -1)));

        REQUIRE(doctest::Approx(0) == FractionOfLine(qvec3f(0, 0, 0), qvec3f(0, 0, 0), qvec3f(0, 0, 0)));
    }

    TEST_CASE("DistToLine")
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

    TEST_CASE("DistToLineSegment")
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

    TEST_CASE("linesOverlap_points")
    {
        REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}));
    }

    TEST_CASE("linesOverlap_point_line")
    {
        REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 1}));
    }

    TEST_CASE("linesOverlap_same")
    {
        REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 0}, {0, 0, 1}));
    }

    TEST_CASE("linesOverlap_same_opposite_dir")
    {
        REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 1}, {0, 0, 0}));
    }

    TEST_CASE("linesOverlap_overlap")
    {
        REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 0.5}, {0, 0, 1.5}));
    }

    TEST_CASE("linesOverlap_overlap_opposite_dir")
    {
        REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 1.5}, {0, 0, 0.5}));
    }

    TEST_CASE("linesOverlap_only_tips_touching")
    {
        REQUIRE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 1}, {0, 0, 2}));
    }

    TEST_CASE("linesOverlap_non_colinear")
    {
        REQUIRE_FALSE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {5, 0, 0}, {5, 0, 1}));
    }

    TEST_CASE("linesOverlap_colinear_not_touching")
    {
        REQUIRE_FALSE(LinesOverlap({0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 0, 3}));
    }

    // qvec

    TEST_CASE("qvec_expand")
    {
        const qvec2f test(1, 2);
        const qvec4f test2(test);

        CHECK(1 == test2[0]);
        CHECK(2 == test2[1]);
        CHECK(0 == test2[2]);
        CHECK(0 == test2[3]);
    }

    TEST_CASE("qvec_contract")
    {
        const qvec4f test(1, 2, 0, 0);
        const qvec2f test2(test);

        CHECK(1 == test2[0]);
        CHECK(2 == test2[1]);
    }

    TEST_CASE("qvec_copy")
    {
        const qvec2f test(1, 2);
        const qvec2f test2(test);

        CHECK(1 == test2[0]);
        CHECK(2 == test2[1]);
    }

    TEST_CASE("qvec_constructor_init")
    {
        const qvec2f test{};
        CHECK(0 == test[0]);
        CHECK(0 == test[1]);
    }

    TEST_CASE("qvec_constructor_1")
    {
        const qvec2f test(42);
        CHECK(42 == test[0]);
        CHECK(42 == test[1]);
    }

    TEST_CASE("qvec_constructor_fewer")
    {
        const qvec4f test(1, 2, 3);
        CHECK(1 == test[0]);
        CHECK(2 == test[1]);
        CHECK(3 == test[2]);
        CHECK(0 == test[3]);
    }

    TEST_CASE("qvec_constructor_extra")
    {
        const qvec2f test(1, 2, 3);
        CHECK(1 == test[0]);
        CHECK(2 == test[1]);
    }

    // aabb3f

    TEST_CASE("aabb_basic")
    {
        const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

        CHECK(qvec3f(1, 1, 1) == b1.mins());
        CHECK(qvec3f(10, 10, 10) == b1.maxs());
        CHECK(qvec3f(9, 9, 9) == b1.size());
    }

    TEST_CASE("aabb_grow")
    {
        const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));

        CHECK(aabb3f(qvec3f(0, 0, 0), qvec3f(11, 11, 11)) == b1.grow(qvec3f(1, 1, 1)));
    }

    TEST_CASE("aabb_unionwith")
    {
        const aabb3f b1(qvec3f(1, 1, 1), qvec3f(10, 10, 10));
        const aabb3f b2(qvec3f(11, 11, 11), qvec3f(12, 12, 12));

        CHECK(aabb3f(qvec3f(1, 1, 1), qvec3f(12, 12, 12)) == b1.unionWith(b2));
    }

    TEST_CASE("aabb_expand")
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

    TEST_CASE("aabb_disjoint")
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

    TEST_CASE("aabb_contains")
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

    TEST_CASE("aabb_containsPoint")
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

    TEST_CASE("aabb_create_invalid")
    {
        const aabb3f b1(qvec3f(1, 1, 1), qvec3f(-1, -1, -1));
        const aabb3f fixed(qvec3f(1, 1, 1), qvec3f(1, 1, 1));

        CHECK(fixed == b1);
        CHECK(qvec3f(0, 0, 0) == b1.size());
    }
}

TEST_SUITE("qvec")
{

    TEST_CASE("matrix2x2inv")
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

    TEST_CASE("matrix3x3inv")
    {
        std::mt19937 engine(0);
        std::uniform_real_distribution<float> dis(-4096, 4096);

        qmat3x3f randMat;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                randMat.at(i, j) = dis(engine);

        qmat3x3f randInv = qv::inverse(randMat);
        REQUIRE_FALSE(std::isnan(randInv.at(0, 0)));

        qmat3x3f prod = randMat * randInv;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                float exp = (i == j) ? 1.0f : 0.0f;
                REQUIRE(fabs(exp - prod.at(i, j)) < 0.001);
            }
        }

        // check non-invertible gives nan
        qmat3x3f nanMat = qv::inverse(qmat3x3f(0));
        REQUIRE(std::isnan(nanMat.at(0, 0)));
    }

    TEST_CASE("matrix4x4inv")
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

    TEST_CASE("qmat_construct_initialize")
    {
        const qmat2x2f test{1, 2, 3, 4}; // column major

        CHECK(qvec2f{1, 3} == test.row(0));
        CHECK(qvec2f{2, 4} == test.row(1));
    }

    TEST_CASE("qmat_construct_row_major")
    {
        const qmat2x2f test = qmat2x2f::row_major({1, 2, 3, 4});

        CHECK(qvec2f{1, 2} == test.row(0));
        CHECK(qvec2f{3, 4} == test.row(1));
    }
}
TEST_SUITE("trace")
{

    TEST_CASE("clamp_texcoord_small")
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

    TEST_CASE("clamp_texcoord")
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
}

TEST_SUITE("settings")
{

    TEST_CASE("delayDefault")
    {
        light_t light;
        CHECK(LF_LINEAR == light.formula.value());
    }

    TEST_CASE("delayParseInt")
    {
        light_t light;
        parser_t p("2", {});
        CHECK(light.formula.parse(light.formula.primary_name(), p, settings::source::MAP));
        CHECK(LF_INVERSE2 == light.formula.value());
    }

    TEST_CASE("delayParseIntUnknown")
    {
        light_t light;
        parser_t p("500", {});
        CHECK(light.formula.parse(light.formula.primary_name(), p, settings::source::MAP));
        // not sure if we should be strict and reject parsing this?
        CHECK(500 == light.formula.value());
    }

    TEST_CASE("delayParseFloat")
    {
        light_t light;
        parser_t p("2.0", {});
        CHECK(light.formula.parse(light.formula.primary_name(), p, settings::source::MAP));
        CHECK(LF_INVERSE2 == light.formula.value());
    }

    TEST_CASE("delayParseString")
    {
        light_t light;
        parser_t p("inverse2", {});
        CHECK(light.formula.parse(light.formula.primary_name(), p, settings::source::MAP));
        CHECK(LF_INVERSE2 == light.formula.value());
    }
}
