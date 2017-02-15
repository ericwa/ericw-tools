#include "gtest/gtest.h"

#include <light/light.hh>
#include <light/light2.hh>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/epsilon.hpp>

using namespace glm;
using namespace std;

static glm::vec4 extendTo4(const glm::vec3 &v) {
    return glm::vec4(v[0], v[1], v[2], 1.0);
}

TEST(light, RotationAboutLineSegment) {
    using namespace glm;
    
    const vec3 up = vec3(0,0,1);
    const vec3 right = vec3(1,0,0);
    
    const vec3 p0 = vec3(64,64,0);
    const vec3 p1 = vec3(64,-64,0);
    
    //         +z
    //
    // -x  ____p0                +x
    //         |    ^ rotation
    //         |  __/
    //         |
    //
    //        -z
    
    // Rotates the face "right" about the p0-p1 edge
    const auto res = RotationAboutLineSegment(p0, p1, up, right);
    ASSERT_TRUE(res.first);
    
    // Test the endpoints of the line segment don't move when rotated
    ASSERT_EQ(vec4(64, 64, 0, 1), res.second * vec4(64, 64, 0, 1));
    ASSERT_EQ(vec4(64, -64, 0, 1), res.second * vec4(64, -64, 0, 1));
    
    // This point moves
    ASSERT_EQ(vec4(65, 0, 0, 1), res.second * vec4(64, 0, -1, 1));
}

TEST(light, RotationAboutLineSegment2) {
    using namespace glm;
    
    const vec3 up = vec3(0,0,1);
    const vec3 dnleft = glm::normalize(vec3(-1,0,-1));
    
    const vec3 p0 = vec3(64,64,0);
    const vec3 p1 = vec3(64,-64,0);
    
    //         +z
    //
    //      \    ___
    //       \  /   \
    //        \      |
    // -x  ____p0    V rotation    +x
    //         |
    //         |
    //         |
    //
    //         -z
    
    // Rotates the face "dnleft" about the p0-p1 edge
    const auto res = RotationAboutLineSegment(p0, p1, up, dnleft);
    ASSERT_TRUE(res.first);
    
    // Test the endpoints of the line segment don't move when rotated
    ASSERT_EQ(vec4(64, 64, 0, 1), res.second * vec4(64, 64, 0, 1));
    ASSERT_EQ(vec4(64, -64, 0, 1), res.second * vec4(64, -64, 0, 1));
    
    // This point moves
    const vec4 actual = res.second * vec4(0, 0, 64, 1);
    EXPECT_FLOAT_EQ(64 + (64 * sqrt(2)), actual[0]);
    EXPECT_FLOAT_EQ(0, actual[1]);
    EXPECT_TRUE(fabs(actual[2]) < 0.001);
    EXPECT_FLOAT_EQ(1, actual[3]);
}

TEST(light, RotationAboutLineSegment3) {
    using namespace glm;
    
    const vec3 up = vec3(0,0,1);
    const vec3 plusYminusX = glm::normalize(vec3(-1,+1,0));
    
    const vec3 p0 = vec3(0,-64,0);
    const vec3 p1 = vec3(128,64,0);
    const vec3 p2_folded = vec3(64, 0, 0.5f*length(p1 - p0));
    const vec3 p2_unfolded = vec3(128,-64,0);
    
    // after unfolding:
    //
    //       +y
    //     ------ p1
    //     |   /|
    // -x  |  / | +x
    //     | /Q |
    //     |/___|
    //    p0     p2 (test point)
    //       -y
    //
    // before unfolding:
    //  - the Q face is pointing upwards (normal is `plusYminusX`)
    //  - p2 is half way between p0 and p1, and raised up along the Z axis
    
    // Rotates the face "plusYminusX" about the p0-p1 edge
    const auto res = RotationAboutLineSegment(p0, p1, up, plusYminusX);
    ASSERT_TRUE(res.first);
    
    // Test the endpoints of the line segment don't move when rotated
    ASSERT_EQ(extendTo4(p0), res.second * extendTo4(p0));
    ASSERT_EQ(extendTo4(p1), res.second * extendTo4(p1));
    
    // This point moves
    const vec4 p2_unfolded_actual = res.second * extendTo4(p2_folded);
    for (int i=0; i<4; i++) {
        EXPECT_LT(fabs(extendTo4(p2_unfolded)[i] - p2_unfolded_actual[i]), 0.001f);
    }
}

TEST(mathlib, MakeCDF) {
    
    std::vector<float> pdfUnnormzlied { 25, 50, 25 };
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

static void checkBox(const vector<vec4> &edges, const vector<vec3> &poly) {
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, vec3(0,0,0)));
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, vec3(64,0,0)));
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, vec3(32,32,0)));
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, vec3(32,32,32))); // off plane
    
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(-0.1,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(64.1,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(0,-0.1,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(0,64.1,0)));

}

TEST(mathlib, EdgePlanesOfNonConvexPoly) {
    // hourglass, non-convex
    const vector<vec3> poly {
        { 0,0,0 },
        { 64,64,0 },
        { 0,64,0 },
        { 64,0,0 }
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    EXPECT_EQ(vector<vec4>(), edges);
}

TEST(mathlib, PointInPolygon) {
    // clockwise
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    checkBox(edges, poly);
}

TEST(mathlib, PointInPolygon_DegenerateEdgeHandling) {
    // clockwise
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 0,64,0 }, // repeat of last point
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    checkBox(edges, poly);
}

TEST(mathlib, PointInPolygon_DegenerateFaceHandling1) {
    const vector<vec3> poly {
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(0,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(10,10,10)));
}

TEST(mathlib, PointInPolygon_DegenerateFaceHandling2) {
    const vector<vec3> poly {
        {0,0,0},
        {0,0,0},
        {0,0,0},
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(0,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(10,10,10)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(-10,-10,-10)));
}

TEST(mathlib, PointInPolygon_DegenerateFaceHandling3) {
    const vector<vec3> poly {
        {0,0,0},
        {10,10,10},
        {20,20,20},
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(0,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(10,10,10)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(-10,-10,-10)));
}

TEST(mathlib, PointInPolygon_ColinearPointHandling) {
    // clockwise
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    
    checkBox(edges, poly);
}

TEST(mathlib, RotTest4) {
    const vector<vec3> contrib {
        vec3(-1943.306030, -567.731140, 140.192612),
        vec3(-1979.382324, -596.117676, 48.000000),
        vec3(-1968.000000, -672.000000, 48.000000),
        vec3(-1918.430298, -672.000000, 161.302124)
    };
    const auto contribEdges = GLM_MakeInwardFacingEdgePlanes(contrib);
    const auto contribNormal = GLM_FaceNormal(contrib);
    
    const vector<vec3> ref {
        vec3(-1856.000000, -543.676758, 149.279465),
        vec3(-1888.000000, -549.721191, 145.486862),
        vec3(-1943.306030, -567.731140, 140.192612),
        vec3(-1918.430298, -672.000000, 161.302124),
        vec3(-1910.796021, -704.000000, 167.780609),
        vec3(-1856.000000, -704.000000, 176.000000)
    };
    const auto refEdges = GLM_MakeInwardFacingEdgePlanes(ref);
    const auto refNormal = GLM_FaceNormal(ref);
    
    const vec3 contribPoint(-1942.951660, -575.428223, 138.363464);
    
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(contribEdges, contribPoint));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(refEdges, contribPoint));
    
    
    const auto res = RotationAboutLineSegment(contrib.at(3), contrib.at(0), refNormal, contribNormal);
    ASSERT_TRUE(res.first);
    
    const vec3 rotated = vec3(res.second * extendTo4(contribPoint));
    
    cout << to_string(rotated) << endl;
    
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(refEdges, rotated));
    
    /*
    dist inside 9.36064
    
    rotated pos vec4(-1925.450439, -571.562317, 143.509521, 1.000000)
     */
    
}

TEST(mathlib, ClosestPointOnPolyBoundary) {
    // clockwise
    const vector<vec3> poly {
        { 0,0,0 },   // edge 0 start, edge 3 end
        { 0,64,0 },  // edge 1 start, edge 0 end
        { 64,64,0 }, // edge 2 start, edge 1 end
        { 64,0,0 }   // edge 3 start, edge 2 end
    };
    
    EXPECT_EQ(make_pair(0, vec3(0,0,0)), GLM_ClosestPointOnPolyBoundary(poly, vec3(0,0,0)));
    
    // Either edge 1 or 2 contain the point vec3(64,64,0), but we expect the first edge to be returned
    EXPECT_EQ(make_pair(1, vec3(64,64,0)), GLM_ClosestPointOnPolyBoundary(poly, vec3(100,100,100)));
    EXPECT_EQ(make_pair(2, vec3(64,32,0)), GLM_ClosestPointOnPolyBoundary(poly, vec3(100,32,0)));
    
    EXPECT_EQ(make_pair(0, vec3(0,0,0)), GLM_ClosestPointOnPolyBoundary(poly, vec3(-1,-1,0)));
}

TEST(mathlib, PolygonCentroid) {
    // poor test.. but at least checks that the colinear point is treated correctly
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    EXPECT_EQ(vec3(32,32,0), GLM_PolyCentroid(poly));
}

TEST(mathlib, BarycentricFromPoint) {
    const tri_t tri = make_tuple<vec3,vec3,vec3>( // clockwise
                                                 { 0,0,0 },
                                                 { 0,64,0 },
                                                 { 64,0,0 }
                                                 );
    
    EXPECT_EQ(vec3(1,0,0), Barycentric_FromPoint(get<0>(tri), tri));
    EXPECT_EQ(vec3(0,1,0), Barycentric_FromPoint(get<1>(tri), tri));
    EXPECT_EQ(vec3(0,0,1), Barycentric_FromPoint(get<2>(tri), tri));
    
    EXPECT_EQ(vec3(0.5, 0.5, 0.0), Barycentric_FromPoint(vec3(0,32,0), tri));
    EXPECT_EQ(vec3(0.0, 0.5, 0.5), Barycentric_FromPoint(vec3(32,32,0), tri));
    EXPECT_EQ(vec3(0.5, 0.0, 0.5), Barycentric_FromPoint(vec3(32,0,0), tri));
}

TEST(mathlib, BarycentricToPoint) {
    const tri_t tri = make_tuple<vec3,vec3,vec3>( // clockwise
                                                 { 0,0,0 },
                                                 { 0,64,0 },
                                                 { 64,0,0 }
                                                 );
    
    EXPECT_EQ(get<0>(tri), Barycentric_ToPoint(vec3(1,0,0), tri));
    EXPECT_EQ(get<1>(tri), Barycentric_ToPoint(vec3(0,1,0), tri));
    EXPECT_EQ(get<2>(tri), Barycentric_ToPoint(vec3(0,0,1), tri));
    
    EXPECT_EQ(vec3(0,32,0), Barycentric_ToPoint(vec3(0.5, 0.5, 0.0), tri));
    EXPECT_EQ(vec3(32,32,0), Barycentric_ToPoint(vec3(0.0, 0.5, 0.5), tri));
    EXPECT_EQ(vec3(32,0,0), Barycentric_ToPoint(vec3(0.5, 0.0, 0.5), tri));
}

TEST(mathlib, BarycentricRandom) {
    const tri_t tri = make_tuple<vec3,vec3,vec3>( // clockwise
                                                 { 0,0,0 },
                                                 { 0,64,0 },
                                                 { 64,0,0 }
                                                 );
    
    const auto triAsVec = vector<vec3>{get<0>(tri), get<1>(tri), get<2>(tri)};
    const auto edges = GLM_MakeInwardFacingEdgePlanes(triAsVec);
    const auto plane = GLM_PolyPlane(triAsVec);
    
    for (int i=0; i<100; i++) {
        const float r0 = Random();
        const float r1 = Random();
        
        ASSERT_GE(r0, 0);
        ASSERT_GE(r1, 0);
        ASSERT_LE(r0, 1);
        ASSERT_LE(r1, 1);
        
        const auto bary = Barycentric_Random(r0, r1);
        EXPECT_FLOAT_EQ(1.0f, bary.x + bary.y + bary.z);
        
        const vec3 point = Barycentric_ToPoint(bary, tri);
        EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, point));
        
        EXPECT_FLOAT_EQ(0.0f, GLM_DistAbovePlane(plane, point));
    }
}

TEST(mathlib, InterpolateNormals) {
    // This test relies on the way GLM_InterpolateNormal is implemented
    
    // o--o--o
    // | / / |
    // |//   |
    // o-----o
    
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 32,64,0 }, // colinear
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<vec3> normals {
        { 1,0,0 },
        { 0,1,0 },
        { 0,0,1 }, // colinear
        { 0,0,0 },
        { -1,0,0 }
    };
    
    // First try all the known points
    for (int i=0; i<poly.size(); i++) {
        const auto res = GLM_InterpolateNormal(poly, normals, poly.at(i));
        EXPECT_EQ(true, res.first);
        EXPECT_TRUE(all(epsilonEqual(normals.at(i), res.second, vec3(POINT_EQUAL_EPSILON))));
    }
    
    {
        const vec3 firstTriCentroid = (poly[0] + poly[1] + poly[2]) / 3.0f;
        const auto res = GLM_InterpolateNormal(poly, normals, firstTriCentroid);
        EXPECT_EQ(true, res.first);
        EXPECT_TRUE(all(epsilonEqual(vec3(1/3.0f), res.second, vec3(POINT_EQUAL_EPSILON))));
    }
    
    // Outside poly
    EXPECT_FALSE(GLM_InterpolateNormal(poly, normals, vec3(-0.1, 0, 0)).first);
}

static bool pointsEqualEpsilon(const vec3 &a, const vec3 &b) {
    return all(epsilonEqual(a, b, vec3(POINT_EQUAL_EPSILON)));
}

static bool polysEqual(const vector<vec3> &p1, const vector<vec3> &p2) {
    if (p1.size() != p2.size())
        return false;
    for (int i=0; i<p1.size(); i++) {
        if (!pointsEqualEpsilon(p1[i], p2[i]))
            return false;
    }
    return true;
}

TEST(mathlib, ClipPoly1) {
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<vec3> frontRes {
        { 0,0,0 },
        { 0,64,0 },
        { 32,64,0 },
        { 32,0,0 }
    };

    const vector<vec3> backRes {
        { 32,64,0 },
        { 64,64,0 },
        { 64,0,0 },
        { 32,0,0 }
    };
    
    auto clipRes = GLM_ClipPoly(poly, vec4(-1,0,0,-32));
    
    EXPECT_TRUE(polysEqual(frontRes, clipRes.first));
    EXPECT_TRUE(polysEqual(backRes, clipRes.second));
}

TEST(mathlib, ShrinkPoly1) {
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<vec3> shrunkPoly {
        { 1,1,0 },
        { 1,63,0 },
        { 63,63,0 },
        { 63,1,0 }
    };
    
    const auto actualShrunk = GLM_ShrinkPoly(poly, 1.0f);
    
    EXPECT_TRUE(polysEqual(shrunkPoly, actualShrunk));
}

TEST(mathlib, ShrinkPoly2) {
    const vector<vec3> poly {
        { 0,0,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<vec3> shrunkPoly {
        { 1.0f + sqrtf(2.0f), 1.0f, 0.0f },
        { 63.0f, 63.0f - sqrtf(2.0f), 0.0f },
        { 63,1,0 },
    };
    
    const auto actualShrunk = GLM_ShrinkPoly(poly, 1.0f);
    
    EXPECT_TRUE(polysEqual(shrunkPoly, actualShrunk));
}

TEST(light, vec_from_mangle) {
    EXPECT_TRUE(pointsEqualEpsilon(vec3(1,0,0), vec_from_mangle(vec3(0,0,0))));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(-1,0,0), vec_from_mangle(vec3(180,0,0))));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,0,1), vec_from_mangle(vec3(0,90,0))));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,0,-1), vec_from_mangle(vec3(0,-90,0))));
}

TEST(light, mangle_from_vec) {
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,0,0), mangle_from_vec(vec3(1,0,0))));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(180,0,0), mangle_from_vec(vec3(-1,0,0))));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,90,0), mangle_from_vec(vec3(0,0,1))));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,-90,0), mangle_from_vec(vec3(0,0,-1))));
}

