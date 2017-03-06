#include "gtest/gtest.h"

#include <light/light.hh>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/epsilon.hpp>

using namespace glm;
using namespace std;

static glm::vec4 extendTo4(const glm::vec3 &v) {
    return glm::vec4(v[0], v[1], v[2], 1.0);
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
//    EXPECT_EQ(vector<vec4>(), edges);
}

TEST(mathlib, SlightlyConcavePoly) {
    const vector<vec3> poly {
        {225.846161, -1744, 1774},
        {248, -1744, 1798},
        {248, -1763.82605, 1799.65222},
        {248, -1764, 1799.66663},
        {248, -1892, 1810.33337},
        {248, -1893.21741, 1810.43481},
        {248, -1921.59998, 1812.80005},
        {248, -1924, 1813},
        {80, -1924, 1631},
        {80, -1744, 1616}
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    ASSERT_FALSE(edges.empty());
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, vec3(152.636963, -1814, 1702)));
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

TEST(mathlib, DistAbovePlane) {
    vec4 plane(0, 0, 1, 10);
    vec3 point(100, 100, 100);
    EXPECT_FLOAT_EQ(90, GLM_DistAbovePlane(plane, point));
}

TEST(mathlib, ProjectPointOntoPlane) {
    vec4 plane(0, 0, 1, 10);
    vec3 point(100, 100, 100);
    
    vec3 projected = GLM_ProjectPointOntoPlane(plane, point);
    EXPECT_FLOAT_EQ(100, projected.x);
    EXPECT_FLOAT_EQ(100, projected.y);
    EXPECT_FLOAT_EQ(10, projected.z);
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

static bool pointsEqualEpsilon(const vec3 &a, const vec3 &b, const float epsilon) {
    return all(epsilonEqual(a, b, vec3(epsilon)));
}

static bool polysEqual(const vector<vec3> &p1, const vector<vec3> &p2) {
    if (p1.size() != p2.size())
        return false;
    for (int i=0; i<p1.size(); i++) {
        if (!pointsEqualEpsilon(p1[i], p2[i], POINT_EQUAL_EPSILON))
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

static const float MANGLE_EPSILON = 0.1f;

TEST(light, vec_from_mangle) {
    EXPECT_TRUE(pointsEqualEpsilon(vec3(1,0,0), vec_from_mangle(vec3(0,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(-1,0,0), vec_from_mangle(vec3(180,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,0,1), vec_from_mangle(vec3(0,90,0)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,0,-1), vec_from_mangle(vec3(0,-90,0)), MANGLE_EPSILON));
}

TEST(light, mangle_from_vec) {
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,0,0), mangle_from_vec(vec3(1,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(180,0,0), mangle_from_vec(vec3(-1,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,90,0), mangle_from_vec(vec3(0,0,1)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,-90,0), mangle_from_vec(vec3(0,0,-1)), MANGLE_EPSILON));
    
    for (int yaw = -179; yaw <= 179; yaw++) {
        for (int pitch = -89; pitch <= 89; pitch++) {
            const vec3 origMangle = vec3(yaw, pitch, 0);
            const vec3 vec = vec_from_mangle(origMangle);
            const vec3 roundtrip = mangle_from_vec(vec);
            EXPECT_TRUE(pointsEqualEpsilon(origMangle, roundtrip, MANGLE_EPSILON));
        }
    }
}

TEST(mathlib, bilinearInterpolate) {
    const vec4 v1(0,1,2,3);
    const vec4 v2(4,5,6,7);
    const vec4 v3(1,1,1,1);
    const vec4 v4(2,2,2,2);
    
    EXPECT_EQ(v1, bilinearInterpolate(v1, v2, v3, v4, 0.0f, 0.0f));
    EXPECT_EQ(v2, bilinearInterpolate(v1, v2, v3, v4, 1.0f, 0.0f));
    EXPECT_EQ(v3, bilinearInterpolate(v1, v2, v3, v4, 0.0f, 1.0f));
    EXPECT_EQ(v4, bilinearInterpolate(v1, v2, v3, v4, 1.0f, 1.0f));
    
    EXPECT_EQ(vec4(1.5,  1.5,  1.5,  1.5),  bilinearInterpolate(v1, v2, v3, v4, 0.5f, 1.0f));
    EXPECT_EQ(vec4(2,    3,    4,    5),    bilinearInterpolate(v1, v2, v3, v4, 0.5f, 0.0f));
    EXPECT_EQ(vec4(1.75, 2.25, 2.75, 3.25), bilinearInterpolate(v1, v2, v3, v4, 0.5f, 0.5f));
}

TEST(mathlib, bilinearWeightsAndCoords) {
    const auto res = bilinearWeightsAndCoords(vec2(0.5, 0.25), ivec2(2,2));
    
    vec2 sum(0);
    for (int i=0; i<4; i++) {
        const float weight = res[i].second;
        const ivec2 intPos = res[i].first;
        sum += vec2(intPos) * weight;
    }
    EXPECT_EQ(vec2(0.5, 0.25), sum);
}

TEST(mathlib, bilinearWeightsAndCoords2) {
    const auto res = bilinearWeightsAndCoords(vec2(1.5, 0.5), ivec2(2,2));
    
    vec2 sum(0);
    for (int i=0; i<4; i++) {
        const float weight = res[i].second;
        const ivec2 intPos = res[i].first;
        sum += vec2(intPos) * weight;
    }
    EXPECT_EQ(vec2(1.0, 0.5), sum);
}

TEST(mathlib, pointsAlongLine) {
    const auto res = PointsAlongLine(vec3(1,0,0), vec3(3.5, 0, 0), 1.5f);

    ASSERT_EQ(2, res.size());
    ASSERT_TRUE(pointsEqualEpsilon(vec3(1,0,0), res[0], POINT_EQUAL_EPSILON));
    ASSERT_TRUE(pointsEqualEpsilon(vec3(2.5,0,0), res[1], POINT_EQUAL_EPSILON));
}

TEST(mathlib, forcefail) {
    ASSERT_FALSE(true);
}
