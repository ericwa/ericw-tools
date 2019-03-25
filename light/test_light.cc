#include "gtest/gtest.h"

#include <light/light.hh>

#include <random>
#include <algorithm> // for std::sort

#include <common/qvec.hh>

#include <common/mesh.hh>
#include <common/aabb.hh>
#include <common/octree.hh>

using namespace std;

static qvec4f extendTo4(const qvec3f &v) {
    return qvec4f(v[0], v[1], v[2], 1.0);
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

static void checkBox(const vector<qvec4f> &edges, const vector<qvec3f> &poly) {
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, qvec3f(0,0,0)));
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, qvec3f(64,0,0)));
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, qvec3f(32,32,0)));
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, qvec3f(32,32,32))); // off plane
    
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(-0.1,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(64.1,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(0,-0.1,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(0,64.1,0)));

}

TEST(mathlib, EdgePlanesOfNonConvexPoly) {
    // hourglass, non-convex
    const vector<qvec3f> poly {
        { 0,0,0 },
        { 64,64,0 },
        { 0,64,0 },
        { 64,0,0 }
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
//    EXPECT_EQ(vector<qvec4f>(), edges);
}

TEST(mathlib, SlightlyConcavePoly) {
    const vector<qvec3f> poly {
        qvec3f(225.846161, -1744, 1774),
        qvec3f(248, -1744, 1798),
        qvec3f(248, -1763.82605, 1799.65222),
        qvec3f(248, -1764, 1799.66663),
        qvec3f(248, -1892, 1810.33337),
        qvec3f(248, -1893.21741, 1810.43481),
        qvec3f(248, -1921.59998, 1812.80005),
        qvec3f(248, -1924, 1813),
        qvec3f(80, -1924, 1631),
        qvec3f(80, -1744, 1616)
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    ASSERT_FALSE(edges.empty());
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, qvec3f(152.636963, -1814, 1702)));
}

TEST(mathlib, PointInPolygon) {
    // clockwise
    const vector<qvec3f> poly {
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
    const vector<qvec3f> poly {
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
    const vector<qvec3f> poly {
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(0,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(10,10,10)));
}

TEST(mathlib, PointInPolygon_DegenerateFaceHandling2) {
    const vector<qvec3f> poly {
        {0,0,0},
        {0,0,0},
        {0,0,0},
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(0,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(10,10,10)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(-10,-10,-10)));
}

TEST(mathlib, PointInPolygon_DegenerateFaceHandling3) {
    const vector<qvec3f> poly {
        {0,0,0},
        {10,10,10},
        {20,20,20},
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(0,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(10,10,10)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, qvec3f(-10,-10,-10)));
}

TEST(mathlib, PointInPolygon_ColinearPointHandling) {
    // clockwise
    const vector<qvec3f> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    
    checkBox(edges, poly);
}

TEST(mathlib, ClosestPointOnLineSegment_Degenerate) {
    EXPECT_EQ(qvec3f(0,0,0), ClosestPointOnLineSegment(qvec3f(0,0,0), qvec3f(0,0,0), qvec3f(10,10,10)));
}

TEST(mathlib, ClosestPointOnPolyBoundary) {
    // clockwise
    const vector<qvec3f> poly {
        { 0,0,0 },   // edge 0 start, edge 3 end
        { 0,64,0 },  // edge 1 start, edge 0 end
        { 64,64,0 }, // edge 2 start, edge 1 end
        { 64,0,0 }   // edge 3 start, edge 2 end
    };
    
    EXPECT_EQ(make_pair(0, qvec3f(0,0,0)), GLM_ClosestPointOnPolyBoundary(poly, qvec3f(0,0,0)));
    
    // Either edge 1 or 2 contain the point qvec3f(64,64,0), but we expect the first edge to be returned
    EXPECT_EQ(make_pair(1, qvec3f(64,64,0)), GLM_ClosestPointOnPolyBoundary(poly, qvec3f(100,100,100)));
    EXPECT_EQ(make_pair(2, qvec3f(64,32,0)), GLM_ClosestPointOnPolyBoundary(poly, qvec3f(100,32,0)));
    
    EXPECT_EQ(make_pair(0, qvec3f(0,0,0)), GLM_ClosestPointOnPolyBoundary(poly, qvec3f(-1,-1,0)));
}

TEST(mathlib, PolygonCentroid_empty) {
    const qvec3f res = GLM_PolyCentroid({});
    
    for (int i=0; i<3; i++) {
    	EXPECT_TRUE(std::isnan(res[i]));
    }
}

TEST(mathlib, PolygonCentroid_point) {
    EXPECT_EQ(qvec3f(1,1,1), GLM_PolyCentroid({qvec3f(1,1,1)}));
}

TEST(mathlib, PolygonCentroid_line) {
    EXPECT_EQ(qvec3f(1,1,1), GLM_PolyCentroid({qvec3f(0,0,0), qvec3f(2,2,2)}));
}

TEST(mathlib, PolygonCentroid) {
    // poor test.. but at least checks that the colinear point is treated correctly
    const vector<qvec3f> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    EXPECT_EQ(qvec3f(32,32,0), GLM_PolyCentroid(poly));
}

TEST(mathlib, PolygonArea) {
    // poor test.. but at least checks that the colinear point is treated correctly
    const vector<qvec3f> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    EXPECT_EQ(64.0f * 64.0f, GLM_PolyArea(poly));
}

TEST(mathlib, BarycentricFromPoint) {
    const tri_t tri = make_tuple<qvec3f,qvec3f,qvec3f>( // clockwise
                                                 { 0,0,0 },
                                                 { 0,64,0 },
                                                 { 64,0,0 }
                                                 );
    
    EXPECT_EQ(qvec3f(1,0,0), Barycentric_FromPoint(get<0>(tri), tri));
    EXPECT_EQ(qvec3f(0,1,0), Barycentric_FromPoint(get<1>(tri), tri));
    EXPECT_EQ(qvec3f(0,0,1), Barycentric_FromPoint(get<2>(tri), tri));
    
    EXPECT_EQ(qvec3f(0.5, 0.5, 0.0), Barycentric_FromPoint(qvec3f(0,32,0), tri));
    EXPECT_EQ(qvec3f(0.0, 0.5, 0.5), Barycentric_FromPoint(qvec3f(32,32,0), tri));
    EXPECT_EQ(qvec3f(0.5, 0.0, 0.5), Barycentric_FromPoint(qvec3f(32,0,0), tri));
}

TEST(mathlib, BarycentricToPoint) {
    const tri_t tri = make_tuple<qvec3f,qvec3f,qvec3f>( // clockwise
                                                 { 0,0,0 },
                                                 { 0,64,0 },
                                                 { 64,0,0 }
                                                 );
    
    EXPECT_EQ(get<0>(tri), Barycentric_ToPoint(qvec3f(1,0,0), tri));
    EXPECT_EQ(get<1>(tri), Barycentric_ToPoint(qvec3f(0,1,0), tri));
    EXPECT_EQ(get<2>(tri), Barycentric_ToPoint(qvec3f(0,0,1), tri));
    
    EXPECT_EQ(qvec3f(0,32,0), Barycentric_ToPoint(qvec3f(0.5, 0.5, 0.0), tri));
    EXPECT_EQ(qvec3f(32,32,0), Barycentric_ToPoint(qvec3f(0.0, 0.5, 0.5), tri));
    EXPECT_EQ(qvec3f(32,0,0), Barycentric_ToPoint(qvec3f(0.5, 0.0, 0.5), tri));
}

TEST(mathlib, BarycentricRandom) {
    const tri_t tri = make_tuple<qvec3f,qvec3f,qvec3f>( // clockwise
                                                 { 0,0,0 },
                                                 { 0,64,0 },
                                                 { 64,0,0 }
                                                 );
    
    const auto triAsVec = vector<qvec3f>{get<0>(tri), get<1>(tri), get<2>(tri)};
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
        EXPECT_FLOAT_EQ(1.0f, bary[0] + bary[1] + bary[2]);
        
        const qvec3f point = Barycentric_ToPoint(bary, tri);
        EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, point));
        
        EXPECT_FLOAT_EQ(0.0f, GLM_DistAbovePlane(plane, point));
    }
}

TEST(mathlib, RotateFromUpToSurfaceNormal) {
    std::mt19937 engine(0);
    std::uniform_real_distribution<float> dis(-4096, 4096);
    
    for (int i=0; i<100; i++) {
        const qvec3f randvec = qv::normalize(qvec3f(dis(engine), dis(engine), dis(engine)));
        const qmat3x3f m = RotateFromUpToSurfaceNormal(randvec);
        
        const qvec3f roundtrip = m * qvec3f(0,0,1);
        ASSERT_TRUE(qv::epsilonEqual(randvec, roundtrip, 0.01f));
    }
}

TEST(mathlib, MakePlane) {
    EXPECT_EQ(qvec4f(0, 0, 1, 10), GLM_MakePlane(qvec3f(0,0,1), qvec3f(0,0,10)));
    EXPECT_EQ(qvec4f(0, 0, 1, 10), GLM_MakePlane(qvec3f(0,0,1), qvec3f(100,100,10)));
}

TEST(mathlib, DistAbovePlane) {
    qvec4f plane(0, 0, 1, 10);
    qvec3f point(100, 100, 100);
    EXPECT_FLOAT_EQ(90, GLM_DistAbovePlane(plane, point));
}

TEST(mathlib, ProjectPointOntoPlane) {
    qvec4f plane(0, 0, 1, 10);
    qvec3f point(100, 100, 100);
    
    qvec3f projected = GLM_ProjectPointOntoPlane(plane, point);
    EXPECT_FLOAT_EQ(100, projected[0]);
    EXPECT_FLOAT_EQ(100, projected[1]);
    EXPECT_FLOAT_EQ(10, projected[2]);
}

TEST(mathlib, InterpolateNormalsDegenerate) {
    EXPECT_FALSE(GLM_InterpolateNormal({}, {}, qvec3f(0,0,0)).first);
    EXPECT_FALSE(GLM_InterpolateNormal({qvec3f(0,0,0)}, {qvec3f(0,0,1)}, qvec3f(0,0,0)).first);
    EXPECT_FALSE(GLM_InterpolateNormal({qvec3f(0,0,0), qvec3f(10,0,0)}, {qvec3f(0,0,1), qvec3f(0,0,1)}, qvec3f(0,0,0)).first);
}

TEST(mathlib, InterpolateNormals) {
    // This test relies on the way GLM_InterpolateNormal is implemented
    
    // o--o--o
    // | / / |
    // |//   |
    // o-----o
    
    const vector<qvec3f> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 32,64,0 }, // colinear
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<qvec3f> normals {
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
        EXPECT_TRUE(qv::epsilonEqual(normals.at(i), res.second, POINT_EQUAL_EPSILON));
    }
    
    {
        const qvec3f firstTriCentroid = (poly[0] + poly[1] + poly[2]) / 3.0f;
        const auto res = GLM_InterpolateNormal(poly, normals, firstTriCentroid);
        EXPECT_EQ(true, res.first);
        EXPECT_TRUE(qv::epsilonEqual(qvec3f(1/3.0f), res.second, POINT_EQUAL_EPSILON));
    }
    
    // Outside poly
    EXPECT_FALSE(GLM_InterpolateNormal(poly, normals, qvec3f(-0.1, 0, 0)).first);
}

static bool polysEqual(const vector<qvec3f> &p1, const vector<qvec3f> &p2) {
    if (p1.size() != p2.size())
        return false;
    for (int i=0; i<p1.size(); i++) {
        if (!qv::epsilonEqual(p1[i], p2[i], POINT_EQUAL_EPSILON))
            return false;
    }
    return true;
}

TEST(mathlib, ClipPoly1) {
    const vector<qvec3f> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<qvec3f> frontRes {
        { 0,0,0 },
        { 0,64,0 },
        { 32,64,0 },
        { 32,0,0 }
    };

    const vector<qvec3f> backRes {
        { 32,64,0 },
        { 64,64,0 },
        { 64,0,0 },
        { 32,0,0 }
    };
    
    auto clipRes = GLM_ClipPoly(poly, qvec4f(-1,0,0,-32));
    
    EXPECT_TRUE(polysEqual(frontRes, clipRes.first));
    EXPECT_TRUE(polysEqual(backRes, clipRes.second));
}

TEST(mathlib, ShrinkPoly1) {
    const vector<qvec3f> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<qvec3f> shrunkPoly {
        { 1,1,0 },
        { 1,63,0 },
        { 63,63,0 },
        { 63,1,0 }
    };
    
    const auto actualShrunk = GLM_ShrinkPoly(poly, 1.0f);
    
    EXPECT_TRUE(polysEqual(shrunkPoly, actualShrunk));
}

TEST(mathlib, ShrinkPoly2) {
    const vector<qvec3f> poly {
        { 0,0,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<qvec3f> shrunkPoly {
        { 1.0f + sqrtf(2.0f), 1.0f, 0.0f },
        { 63.0f, 63.0f - sqrtf(2.0f), 0.0f },
        { 63,1,0 },
    };
    
    const auto actualShrunk = GLM_ShrinkPoly(poly, 1.0f);
    
    EXPECT_TRUE(polysEqual(shrunkPoly, actualShrunk));
}

TEST(mathlib, SignedDegreesBetweenUnitVectors) {
    const qvec3f up {0, 0, 1};
    const qvec3f fwd {0, 1, 0};
    const qvec3f right {1, 0, 0};
    
    EXPECT_FLOAT_EQ(-90, SignedDegreesBetweenUnitVectors(right, fwd, up));
    EXPECT_FLOAT_EQ(90, SignedDegreesBetweenUnitVectors(fwd, right, up));
    EXPECT_FLOAT_EQ(0, SignedDegreesBetweenUnitVectors(right, right, up));
}

TEST(mathlib, ConcavityTest_concave) {
    const qvec3f face1center {0, 0, 10};
    const qvec3f face2center {10, 0, 200};
    
    const qvec3f face1normal {0, 0, 1};
    const qvec3f face2normal {-1, 0, 0};
    
    EXPECT_EQ(concavity_t::Concave, FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST(mathlib, ConcavityTest_concave2) {
    const qvec3f face1center {0, 0, 10};
    const qvec3f face2center {-10, 0, 200};
    
    const qvec3f face1normal {0, 0, 1};
    const qvec3f face2normal {1, 0, 0};
    
    EXPECT_EQ(concavity_t::Concave, FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST(mathlib, ConcavityTest_convex) {
    const qvec3f face1center {0, 0, 10};
    const qvec3f face2center {10, 0, 5};
    
    const qvec3f face1normal {0, 0, 1};
    const qvec3f face2normal {1, 0, 0};
    
    EXPECT_EQ(concavity_t::Convex, FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST(mathlib, ConcavityTest_convex2) {
    const qvec3f face1center {0, 0, 10};
    const qvec3f face2center {-10, 0, 5};
    
    const qvec3f face1normal {0, 0, 1};
    const qvec3f face2normal {-1, 0, 0};
    
    EXPECT_EQ(concavity_t::Convex, FacePairConcavity(face1center, face1normal, face2center, face2normal));
}

TEST(mathlib, ConcavityTest_coplanar) {
    const qvec3f face1center {0, 0, 10};
    const qvec3f face2center {100, 100, 10};
    
    const qvec3f face1normal {0, 0, 1};
    const qvec3f face2normal {0, 0, 1};
    
    EXPECT_EQ(concavity_t::Coplanar, FacePairConcavity(face1center, face1normal, face2center, face2normal));
}
static const float MANGLE_EPSILON = 0.1f;

TEST(light, vec_from_mangle) {
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(1,0,0), vec_from_mangle(qvec3f(0,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(-1,0,0), vec_from_mangle(qvec3f(180,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(0,0,1), vec_from_mangle(qvec3f(0,90,0)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(0,0,-1), vec_from_mangle(qvec3f(0,-90,0)), MANGLE_EPSILON));
}

TEST(light, mangle_from_vec) {
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(0,0,0), mangle_from_vec(qvec3f(1,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(180,0,0), mangle_from_vec(qvec3f(-1,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(0,90,0), mangle_from_vec(qvec3f(0,0,1)), MANGLE_EPSILON));
    EXPECT_TRUE(qv::epsilonEqual(qvec3f(0,-90,0), mangle_from_vec(qvec3f(0,0,-1)), MANGLE_EPSILON));
    
    for (int yaw = -179; yaw <= 179; yaw++) {
        for (int pitch = -89; pitch <= 89; pitch++) {
            const qvec3f origMangle = qvec3f(yaw, pitch, 0);
            const qvec3f vec = vec_from_mangle(origMangle);
            const qvec3f roundtrip = mangle_from_vec(vec);
            EXPECT_TRUE(qv::epsilonEqual(origMangle, roundtrip, MANGLE_EPSILON));
        }
    }
}

TEST(mathlib, bilinearInterpolate) {
    const qvec4f v1(0,1,2,3);
    const qvec4f v2(4,5,6,7);
    const qvec4f v3(1,1,1,1);
    const qvec4f v4(2,2,2,2);
    
    EXPECT_EQ(v1, bilinearInterpolate(v1, v2, v3, v4, 0.0f, 0.0f));
    EXPECT_EQ(v2, bilinearInterpolate(v1, v2, v3, v4, 1.0f, 0.0f));
    EXPECT_EQ(v3, bilinearInterpolate(v1, v2, v3, v4, 0.0f, 1.0f));
    EXPECT_EQ(v4, bilinearInterpolate(v1, v2, v3, v4, 1.0f, 1.0f));
    
    EXPECT_EQ(qvec4f(1.5,  1.5,  1.5,  1.5),  bilinearInterpolate(v1, v2, v3, v4, 0.5f, 1.0f));
    EXPECT_EQ(qvec4f(2,    3,    4,    5),    bilinearInterpolate(v1, v2, v3, v4, 0.5f, 0.0f));
    EXPECT_EQ(qvec4f(1.75, 2.25, 2.75, 3.25), bilinearInterpolate(v1, v2, v3, v4, 0.5f, 0.5f));
}

TEST(mathlib, bilinearWeightsAndCoords) {
    const auto res = bilinearWeightsAndCoords(qvec2f(0.5, 0.25), qvec2i(2,2));
    
    qvec2f sum(0);
    for (int i=0; i<4; i++) {
        const float weight = res[i].second;
        const qvec2i intPos = res[i].first;
        sum += qvec2f(intPos) * weight;
    }
    EXPECT_EQ(qvec2f(0.5, 0.25), sum);
}

TEST(mathlib, bilinearWeightsAndCoords2) {
    const auto res = bilinearWeightsAndCoords(qvec2f(1.5, 0.5), qvec2i(2,2));
    
    qvec2f sum(0);
    for (int i=0; i<4; i++) {
        const float weight = res[i].second;
        const qvec2i intPos = res[i].first;
        sum += qvec2f(intPos) * weight;
    }
    EXPECT_EQ(qvec2f(1.0, 0.5), sum);
}

TEST(mathlib, pointsAlongLine) {
    const auto res = PointsAlongLine(qvec3f(1,0,0), qvec3f(3.5, 0, 0), 1.5f);

    ASSERT_EQ(2, res.size());
    ASSERT_TRUE(qv::epsilonEqual(qvec3f(1,0,0), res[0], POINT_EQUAL_EPSILON));
    ASSERT_TRUE(qv::epsilonEqual(qvec3f(2.5,0,0), res[1], POINT_EQUAL_EPSILON));
}

// FIXME: this is failing
#if 0
TEST(mathlib, RandomPointInPoly) {
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
    qvec3f avg(0);
    
    const auto randomstate = GLM_PolyRandomPoint_Setup(poly);
    
    const int N=100;
    for (int i=0; i<N; i++) {
        const qvec3f point = GLM_PolyRandomPoint(randomstate, Random(), Random(), Random());
        ASSERT_TRUE(GLM_EdgePlanes_PointInside(edgeplanes, point));
        
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

TEST(mathlib, FractionOfLine) {
    ASSERT_FLOAT_EQ(0, FractionOfLine(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(0,0,0)));
    ASSERT_FLOAT_EQ(0.5, FractionOfLine(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(0.5, 0.5, 0.5)));
    ASSERT_FLOAT_EQ(1, FractionOfLine(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(1,1,1)));
    ASSERT_FLOAT_EQ(2, FractionOfLine(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(2,2,2)));
    ASSERT_FLOAT_EQ(-1, FractionOfLine(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(-1,-1,-1)));
    
    ASSERT_FLOAT_EQ(0, FractionOfLine(qvec3f(0,0,0), qvec3f(0,0,0), qvec3f(0,0,0)));
}

TEST(mathlib, DistToLine) {
    const float epsilon = 0.001;
    
    ASSERT_TRUE(fabs(0 - DistToLine(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(0,0,0))) < epsilon);
    ASSERT_TRUE(fabs(0 - DistToLine(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(0.5, 0.5, 0.5))) < epsilon);
    ASSERT_TRUE(fabs(0 - DistToLine(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(1,1,1))) < epsilon);
    ASSERT_TRUE(fabs(0 - DistToLine(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(2,2,2))) < epsilon);
    ASSERT_TRUE(fabs(0 - DistToLine(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(-1,-1,-1))) < epsilon);
    
    ASSERT_TRUE(fabs(sqrt(2)/2 - DistToLine(qvec3f(0,0,0), qvec3f(1,1,0), qvec3f(0,1,0))) < epsilon);
    ASSERT_TRUE(fabs(sqrt(2)/2 - DistToLine(qvec3f(0,0,0), qvec3f(1,1,0), qvec3f(1,0,0))) < epsilon);
    
    ASSERT_TRUE(fabs(0.5 - DistToLine(qvec3f(10,0,0), qvec3f(10,0,100), qvec3f(9.5,0,0))) < epsilon);
}

TEST(mathlib, DistToLineSegment) {
    const float epsilon = 0.001;
    
    ASSERT_TRUE(fabs(0 - DistToLineSegment(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(0,0,0))) < epsilon);
    ASSERT_TRUE(fabs(0 - DistToLineSegment(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(0.5, 0.5, 0.5))) < epsilon);
    ASSERT_TRUE(fabs(0 - DistToLineSegment(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(1,1,1))) < epsilon);
    ASSERT_TRUE(fabs(sqrt(3) - DistToLineSegment(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(2,2,2))) < epsilon);
    ASSERT_TRUE(fabs(sqrt(3) - DistToLineSegment(qvec3f(0,0,0), qvec3f(1,1,1), qvec3f(-1,-1,-1))) < epsilon);
    
    ASSERT_TRUE(fabs(sqrt(2)/2 - DistToLineSegment(qvec3f(0,0,0), qvec3f(1,1,0), qvec3f(0,1,0))) < epsilon);
    ASSERT_TRUE(fabs(sqrt(2)/2 - DistToLineSegment(qvec3f(0,0,0), qvec3f(1,1,0), qvec3f(1,0,0))) < epsilon);
    
    ASSERT_TRUE(fabs(0.5 - DistToLineSegment(qvec3f(10,0,0), qvec3f(10,0,100), qvec3f(9.5,0,0))) < epsilon);
}

TEST(mathlib, linesOverlap_points) {
    ASSERT_TRUE(LinesOverlap({0,0,0}, {0,0,0},
                             {0,0,0}, {0,0,0}));
}

TEST(mathlib, linesOverlap_point_line) {
    ASSERT_TRUE(LinesOverlap({0,0,0}, {0,0,0},
                             {0,0,0}, {0,0,1}));
}

TEST(mathlib, linesOverlap_same) {
    ASSERT_TRUE(LinesOverlap({0,0,0}, {0,0,1},
    						 {0,0,0}, {0,0,1}));
}

TEST(mathlib, linesOverlap_same_opposite_dir) {
    ASSERT_TRUE(LinesOverlap({0,0,0}, {0,0,1},
                             {0,0,1}, {0,0,0}));
}

TEST(mathlib, linesOverlap_overlap) {
    ASSERT_TRUE(LinesOverlap({0,0,0}, {0,0,1},
                             {0,0,0.5}, {0,0,1.5}));
}

TEST(mathlib, linesOverlap_overlap_opposite_dir) {
    ASSERT_TRUE(LinesOverlap({0,0,0}, {0,0,1},
                             {0,0,1.5}, {0,0,0.5}));
}

TEST(mathlib, linesOverlap_only_tips_touching) {
    ASSERT_TRUE(LinesOverlap({0,0,0}, {0,0,1},
                        	 {0,0,1}, {0,0,2}));
}

TEST(mathlib, linesOverlap_non_colinear) {
    ASSERT_FALSE(LinesOverlap({0,0,0}, {0,0,1},
                              {5,0,0}, {5,0,1}));
}

TEST(mathlib, linesOverlap_colinear_not_touching) {
    ASSERT_FALSE(LinesOverlap({0,0,0}, {0,0,1},
                              {0,0,2}, {0,0,3}));
}

// mesh_t

TEST(mathlib, meshCreate) {
    const vector<qvec3f> poly1 {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    const vector<qvec3f> poly2 {
        { 64,0,0 },
        { 64,64,0 },
        { 128,64,0 },
        { 128,0,0 }
    };
    const vector<vector<qvec3f>> polys { poly1, poly2 };
    
    const mesh_t m = buildMesh(polys);
    ASSERT_EQ(6, m.verts.size());
    ASSERT_EQ(2, m.faces.size());
    ASSERT_EQ(polys, meshToFaces(m));
}

TEST(mathlib, meshFixTJuncs) {
    /*
     
     poly1
     
   x=0 x=64 x=128
     
     |---|--| y=64  poly2
     |   +--| y=32
     |---|--| y=0   poly3
     
     poly1 should get a vertex inserted at the +
     
     */
    const vector<qvec3f> poly1 {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    const vector<qvec3f> poly2 {
        { 64,32,0 },
        { 64,64,0 },
        { 128,64,0 },
        { 128,32,0 }
    };
    const vector<qvec3f> poly3 {
        { 64,0,0 },
        { 64,32,0 },
        { 128,32,0 },
        { 128,0,0 }
    };
    
    const vector<vector<qvec3f>> polys { poly1, poly2, poly3 };
    
    mesh_t m = buildMesh(polys);
    
    ASSERT_EQ(aabb3f(qvec3f(0,0,0), qvec3f(64,64,0)), mesh_face_bbox(m, 0));
    
    ASSERT_EQ(8, m.verts.size());
    ASSERT_EQ(3, m.faces.size());
    ASSERT_EQ(polys, meshToFaces(m));
    
    cleanupMesh(m);

    const vector<qvec3f> poly1_fixed {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,32,0 },
        { 64,0,0 }
    };
    
    const auto newFaces = meshToFaces(m);
    EXPECT_EQ(poly1_fixed, newFaces.at(0));
    EXPECT_EQ(poly2, newFaces.at(1));
    EXPECT_EQ(poly3, newFaces.at(2));
}

// qvec

TEST(mathlib, qvec_expand) {
    const qvec2f test(1,2);
    const qvec4f test2(test);
    
    EXPECT_EQ(1, test2[0]);
    EXPECT_EQ(2, test2[1]);
    EXPECT_EQ(0, test2[2]);
    EXPECT_EQ(0, test2[3]);
}

TEST(mathlib, qvec_contract) {
    const qvec4f test(1,2,0,0);
    const qvec2f test2(test);
    
    EXPECT_EQ(1, test2[0]);
    EXPECT_EQ(2, test2[1]);
}

TEST(mathlib, qvec_copy) {
    const qvec2f test(1,2);
    const qvec2f test2(test);
    
    EXPECT_EQ(1, test2[0]);
    EXPECT_EQ(2, test2[1]);
}

TEST(mathlib, qvec_constructor_0) {
    const qvec2f test;
    EXPECT_EQ(0, test[0]);
    EXPECT_EQ(0, test[1]);
}

TEST(mathlib, qvec_constructor_1) {
    const qvec2f test(42);
    EXPECT_EQ(42, test[0]);
    EXPECT_EQ(42, test[1]);
}

TEST(mathlib, qvec_constructor_fewer) {
    const qvec4f test(1,2,3);
    EXPECT_EQ(1, test[0]);
    EXPECT_EQ(2, test[1]);
    EXPECT_EQ(3, test[2]);
    EXPECT_EQ(0, test[3]);
}

TEST(mathlib, qvec_constructor_extra) {
    const qvec2f test(1,2,3);
    EXPECT_EQ(1, test[0]);
    EXPECT_EQ(2, test[1]);
}

// aabb3f

TEST(mathlib, aabb_basic) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    
    EXPECT_EQ(qvec3f(1,1,1), b1.mins());
    EXPECT_EQ(qvec3f(10,10,10), b1.maxs());
    EXPECT_EQ(qvec3f(9,9,9), b1.size());
}

TEST(mathlib, aabb_grow) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));

    EXPECT_EQ(aabb3f(qvec3f(0,0,0), qvec3f(11,11,11)), b1.grow(qvec3f(1,1,1)));
}

TEST(mathlib, aabb_unionwith) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    const aabb3f b2(qvec3f(11,11,11), qvec3f(12,12,12));
    
    EXPECT_EQ(aabb3f(qvec3f(1,1,1), qvec3f(12,12,12)), b1.unionWith(b2));
}

TEST(mathlib, aabb_expand) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    
    EXPECT_EQ(b1, b1.expand(qvec3f(1,1,1)));
    EXPECT_EQ(b1, b1.expand(qvec3f(5,5,5)));
    EXPECT_EQ(b1, b1.expand(qvec3f(10,10,10)));
    
    const aabb3f b2(qvec3f(1,1,1), qvec3f(100,10,10));
    EXPECT_EQ(b2, b1.expand(qvec3f(100,10,10)));
    
    const aabb3f b3(qvec3f(0,1,1), qvec3f(10,10,10));
    EXPECT_EQ(b3, b1.expand(qvec3f(0,1,1)));
}

TEST(mathlib, aabb_disjoint) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    
    const aabb3f yes1(qvec3f(-1,-1,-1), qvec3f(0,0,0));
    const aabb3f yes2(qvec3f(11,1,1), qvec3f(12,10,10));
    
    const aabb3f no1(qvec3f(-1,-1,-1), qvec3f(1,1,1));
    const aabb3f no2(qvec3f(10,10,10), qvec3f(10.5,10.5,10.5));
    const aabb3f no3(qvec3f(5,5,5), qvec3f(100,6,6));
    
    EXPECT_TRUE(b1.disjoint(yes1));
    EXPECT_TRUE(b1.disjoint(yes2));
    EXPECT_FALSE(b1.disjoint(no1));
    EXPECT_FALSE(b1.disjoint(no2));
    EXPECT_FALSE(b1.disjoint(no3));
    
    EXPECT_FALSE(b1.intersectWith(yes1).valid);
    EXPECT_FALSE(b1.intersectWith(yes2).valid);
    
    // these intersections are single points
    EXPECT_EQ(aabb3f::intersection_t(aabb3f(qvec3f(1,1,1), qvec3f(1,1,1))), b1.intersectWith(no1));
    EXPECT_EQ(aabb3f::intersection_t(aabb3f(qvec3f(10,10,10), qvec3f(10,10,10))), b1.intersectWith(no2));
    
    // an intersection with a volume
    EXPECT_EQ(aabb3f::intersection_t(aabb3f(qvec3f(5,5,5), qvec3f(10,6,6))), b1.intersectWith(no3));
}

TEST(mathlib, aabb_contains) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    
    const aabb3f yes1(qvec3f(1,1,1), qvec3f(2,2,2));
    const aabb3f yes2(qvec3f(9,9,9), qvec3f(10,10,10));
    
    const aabb3f no1(qvec3f(-1,1,1), qvec3f(2,2,2));
    const aabb3f no2(qvec3f(9,9,9), qvec3f(10.5,10,10));
    
    EXPECT_TRUE(b1.contains(yes1));
    EXPECT_TRUE(b1.contains(yes2));
    EXPECT_FALSE(b1.contains(no1));
    EXPECT_FALSE(b1.contains(no2));
}

TEST(mathlib, aabb_containsPoint) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    
    const qvec3f yes1(1,1,1);
    const qvec3f yes2(2,2,2);
    const qvec3f yes3(10,10,10);
    
    const qvec3f no1(0,0,0);
    const qvec3f no2(1,1,0);
    const qvec3f no3(10.1,10.1,10.1);
    
    EXPECT_TRUE(b1.containsPoint(yes1));
    EXPECT_TRUE(b1.containsPoint(yes2));
    EXPECT_TRUE(b1.containsPoint(yes3));
    EXPECT_FALSE(b1.containsPoint(no1));
    EXPECT_FALSE(b1.containsPoint(no2));
    EXPECT_FALSE(b1.containsPoint(no3));
}

TEST(mathlib, aabb_create_invalid) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(-1,-1,-1));
    const aabb3f fixed(qvec3f(1,1,1), qvec3f(1,1,1));
    
    EXPECT_EQ(fixed, b1);
    EXPECT_EQ(qvec3f(0,0,0), b1.size());
}

// octree

TEST(mathlib, octree_basic) {
    std::mt19937 engine(0);
    std::uniform_int_distribution<> dis(-4096, 4096);
    
    const qvec3f boxsize(64,64,64);
    const int N = 2000;
    
    // generate some objects
    vector<pair<aabb3f, int>> objs;
    for (int i=0; i<N; i++) {
        int x = dis(engine);
        int y = dis(engine);
        int z = dis(engine);
        qvec3f center(x,y,z);
        qvec3f mins = center - boxsize;
        qvec3f maxs = center + boxsize;
        
        aabb3f bbox(mins, maxs);
        objs.push_back(make_pair(bbox, i));
    }
    
    // build octree
    const double insert_start = I_FloatTime();
    auto octree = makeOctree(objs);
    const double insert_end = I_FloatTime();
    printf("inserting %d cubes took %f ms\n", N, 1000.0 * (insert_end - insert_start));
    
    // query for objects overlapping objs[0]'s bbox
    const double exhaustive_query_start = I_FloatTime();
    vector<vector<int>> objsTouchingObjs;
    for (int i=0; i<N; i++) {
        const aabb3f obj_iBBox = objs[i].first;
        
        vector<int> objsTouchingObj_i;
        for (int j=0; j<N; j++) {
            if (!obj_iBBox.disjoint(objs[j].first)) {
                objsTouchingObj_i.push_back(objs[j].second);
            }
        }
        objsTouchingObjs.push_back(objsTouchingObj_i);
    }
    const double exhaustive_query_end = I_FloatTime();
    printf("exhaustive query took %f ms\n", 1000.0 * (exhaustive_query_end - exhaustive_query_start));
    
    // now repeat the same query using the octree
    const double octree_query_start = I_FloatTime();
    vector<vector<int>> objsTouchingObjs_octree;
    for (int i=0; i<N; i++) {
        const aabb3f obj_iBBox = objs[i].first;
        
        vector<int> objsTouchingObj_i = octree.queryTouchingBBox(obj_iBBox);
        objsTouchingObjs_octree.push_back(objsTouchingObj_i);
    }
    const double octree_query_end = I_FloatTime();
    printf("octree query took %f ms\n", 1000.0 * (octree_query_end - octree_query_start));
    
    // compare result
    for (int i=0; i<N; i++) {
        vector<int> &objsTouchingObj_i = objsTouchingObjs[i];
        vector<int> &objsTouchingObj_i_octree = objsTouchingObjs_octree[i];
        
        std::sort(objsTouchingObj_i.begin(), objsTouchingObj_i.end());
        std::sort(objsTouchingObj_i_octree.begin(), objsTouchingObj_i_octree.end());
        EXPECT_EQ(objsTouchingObj_i, objsTouchingObj_i_octree);
    }
}

TEST(qvec, matrix2x2inv) {
    std::mt19937 engine(0);
    std::uniform_real_distribution<float> dis(-4096, 4096);
    
    qmat2x2f randMat;
    for (int i=0; i<2; i++)
        for (int j=0; j<2; j++)
            randMat.at(i,j) = dis(engine);
    
    qmat2x2f randInv = qv::inverse(randMat);
    ASSERT_FALSE(std::isnan(randInv.at(0, 0)));
    
    qmat2x2f prod = randMat * randInv;
    for (int i=0; i<2; i++) {
        for (int j=0; j<2; j++) {
            float exp = (i == j) ? 1.0f : 0.0f;
            ASSERT_TRUE(fabs(exp - prod.at(i,j)) < 0.001);
        }
    }
    
    // check non-invertible gives nan
    qmat2x2f nanMat = qv::inverse(qmat2x2f(0));
    ASSERT_TRUE(std::isnan(nanMat.at(0, 0)));
}

TEST(qvec, matrix4x4inv) {
    std::mt19937 engine(0);
    std::uniform_real_distribution<float> dis(-4096, 4096);
    
    qmat4x4f randMat;
    for (int i=0; i<4; i++)
        for (int j=0; j<4; j++)
            randMat.at(i,j) = dis(engine);
    
    qmat4x4f randInv = qv::inverse(randMat);
    ASSERT_FALSE(std::isnan(randInv.at(0, 0)));
    
    qmat4x4f prod = randMat * randInv;
    for (int i=0; i<4; i++) {
        for (int j=0; j<4; j++) {
            float exp = (i == j) ? 1.0f : 0.0f;
            ASSERT_TRUE(fabs(exp - prod.at(i,j)) < 0.001);
        }
    }
    
    // check non-invertible gives nan
    qmat4x4f nanMat = qv::inverse(qmat4x4f(0));
    ASSERT_TRUE(std::isnan(nanMat.at(0, 0)));
}

TEST(trace, clamp_texcoord_small) {
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

TEST(trace, clamp_texcoord) {
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