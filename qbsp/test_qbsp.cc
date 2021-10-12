#include "gtest/gtest.h"

#include <qbsp/qbsp.hh>
#include <qbsp/map.hh>

// FIXME: Clear global data (planes, etc) between each test

static face_t *Brush_FirstFaceWithTextureName(brush_t *brush, const char *texname)
{
    for (face_t *face = brush->faces; face; face = face->next) {
        if (map.texinfoTextureName(face->texinfo) == texname)
            return face;
    }
    return nullptr;
}

static const mapface_t *Mapbrush_FirstFaceWithTextureName(const mapbrush_t *brush, const std::string &texname)
{
    for (int i = 0; i < brush->numfaces; i++) {
        const mapface_t *face = &brush->face(i);
        if (face->texname == texname) {
            return face;
        }
    }
    return nullptr;
}

static mapentity_t LoadMap(const char *map)
{
    options.target_version = &bspver_q1;
    options.target_game = options.target_version->game;

    parser_t parser(map);

    mapentity_t worldspawn;
    // FIXME: adds the brush to the global map...
    Q_assert(ParseEntity(parser, &worldspawn));

    return worldspawn;
}

static const texvecf &GetTexvecs(const char *map, const char *texname)
{
    mapentity_t worldspawn = LoadMap(map);

    const mapbrush_t *mapbrush = &worldspawn.mapbrush(0);
    const mapface_t *mapface = Mapbrush_FirstFaceWithTextureName(mapbrush, "tech02_1");
    Q_assert(nullptr != mapface);

    return mapface->get_texvecs();
}

// https://github.com/ericwa/ericw-tools/issues/158
TEST(qbsp, testTextureIssue)
{
    const char *bufActual = R"(
    {
        "classname" "worldspawn"
        "wad" "Q.wad"
        {
            ( -104 -4 23.999998 ) ( -96.000252 -4 39.999489 ) ( -96.000252 4 39.999489 ) skip 0 0 0 1.000000 1.000000 0 0 0
            ( -135.996902 4 80.001549 ) ( -152 4 72 ) ( -104 4 23.999998 ) skip 0 -11 -45 1.000000 -1.070000 0 0 0
            ( -152 -4 72 ) ( -135.996902 -4 80.001549 ) ( -95.998451 -4 40.003094 ) skip 0 -11 -45 1.000000 -1.070000 0 0 0
            ( -96.000633 -4 40.000637 ) ( -136 -4 80.000008 ) ( -136 4 80.000008 ) skip 0 0 0 1.000000 1.000000 0 0 0
            ( -136 -4 80 ) ( -152 -4 72 ) ( -152 4 72 ) skip 0 0 0 1.000000 1.000000 0 0 0
            ( -152 -4 72.000008 ) ( -104.000168 -4 24.000172 ) ( -104.000168 4 24.000172 ) tech02_1 0 -8 0 1.000000 0.750000 0 0 0
        }
    }
    )";

    const char *bufExpected = R"(
    {
        "classname" "worldspawn"
        "wad" "Q.wad"
        {
            ( -104 -4 23.999998 ) ( -96.000252 -4 39.999489 ) ( -96.000252 4 39.999489 ) skip 0 0 0 1.000000 1.000000 0 0 0
            ( -135.996902 4 80.001549 ) ( -152 4 72 ) ( -104 4 23.999998 ) skip 0 -11 -45 1.000000 -1.070000 0 0 0
            ( -152 -4 72 ) ( -135.996902 -4 80.001549 ) ( -95.998451 -4 40.003094 ) skip 0 -11 -45 1.000000 -1.070000 0 0 0
            ( -96.000633 -4 40.000637 ) ( -136 -4 80.000008 ) ( -136 4 80.000008 ) skip 0 0 0 1.000000 1.000000 0 0 0
            ( -136 -4 80 ) ( -152 -4 72 ) ( -152 4 72 ) skip 0 0 0 1.000000 1.000000 0 0 0
            ( -152 -4 72 ) ( -104 -4 24 ) ( -104 4 24 ) tech02_1 0 -8 0 1 0.75 0 0 0
        }
    }
    )";

    const auto texvecsExpected = GetTexvecs(bufExpected, "tech02_1");
    const auto texvecsActual = GetTexvecs(bufActual, "tech02_1");

    // not going to fix #158 for now
#if 0
    for (int i=0; i<2; i++) {
        for (int j=0; j<4; j++) {
            EXPECT_DOUBLE_EQ(texvecsExpected[i][j], texvecsActual[i][j]);
        }
    }
#endif
}

TEST(qbsp, duplicatePlanes)
{
    // a brush from e1m4.map with 7 planes, only 6 unique.
    const char *mapWithDuplicatePlanes = R"(
    {
        "classname"	"worldspawn"
        {
            ( 512 120 1184 ) ( 512 104 1184 ) ( 512 8 1088 ) WBRICK1_5 0 0 0 1.000000 1.000000
            ( 1072 104 1184 ) ( 176 104 1184 ) ( 176 8 1088 ) WBRICK1_5 0 0 0 1.000000 1.000000
            ( 896 56 1184 ) ( 896 72 1184 ) ( 896 -24 1088 ) WBRICK1_5 0 0 0 1.000000 1.000000
            ( 176 88 1184 ) ( 1072 88 1184 ) ( 1072 -8 1088 ) WBRICK1_5 0 0 0 1.000000 1.000000
            ( 176 88 1184 ) ( 176 104 1184 ) ( 1072 104 1184 ) WBRICK1_5 0 0 0 1.000000 1.000000
            ( 1072 8 1088 ) ( 176 8 1088 ) ( 176 -8 1088 ) WBRICK1_5 0 0 0 1.000000 1.000000
            ( 960 8 1088 ) ( 864 104 1184 ) ( 848 104 1184 ) WBRICK1_5 0 0 0 1.000000 1.000000
        }
    }
    )";

    mapentity_t worldspawn = LoadMap(mapWithDuplicatePlanes);
    ASSERT_EQ(1, worldspawn.nummapbrushes);
    EXPECT_EQ(0, worldspawn.numbrushes);
    EXPECT_EQ(6, worldspawn.mapbrush(0).numfaces);

    brush_t *brush = LoadBrush(&worldspawn, &worldspawn.mapbrush(0), {CONTENTS_SOLID}, {}, rotation_t::none, 0);
    ASSERT_NE(nullptr, brush);
    EXPECT_EQ(6, Brush_NumFaces(brush));
    FreeBrush(brush);
}

static brush_t *load128x128x32Brush()
{
    /* 128x128x32 rectangular brush */
    const char *map = R"(
    {
        "classname" "worldspawn"
        {
            ( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) __TB_empty 0 0 0 1 1
            ( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) __TB_empty 0 0 0 1 1
            ( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) __TB_empty 0 0 0 1 1
            ( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) __TB_empty 0 0 0 1 1
            ( 64 64 16 ) ( 64 65 16 ) ( 65 64 16 ) __TB_empty 0 0 0 1 1
            ( -64 -64 -16 ) ( -63 -64 -16 ) ( -64 -63 -16 ) __TB_empty 0 0 0 1 1
        }
    }
    )";

    mapentity_t worldspawn = LoadMap(map);
    Q_assert(1 == worldspawn.nummapbrushes);

    brush_t *brush = LoadBrush(&worldspawn, &worldspawn.mapbrush(0), {CONTENTS_SOLID}, {}, rotation_t::none, 0);
    Q_assert(nullptr != brush);

    brush->contents = {CONTENTS_SOLID};

    return brush;
}

TEST(qbsp, BrushVolume)
{
    brush_t *brush = load128x128x32Brush();

    EXPECT_DOUBLE_EQ((128 * 128 * 32), BrushVolume(brush));
}

TEST(qbsp, BrushMostlyOnSide1)
{
    brush_t *brush = load128x128x32Brush();

    qvec3d plane1normal {-1, 0, 0};
    vec_t plane1dist = -100;

    EXPECT_EQ(SIDE_FRONT, BrushMostlyOnSide(brush, plane1normal, plane1dist));

    FreeBrush(brush);
}

TEST(qbsp, BrushMostlyOnSide2)
{
    brush_t *brush = load128x128x32Brush();

    qvec3d plane1normal {1, 0, 0};
    vec_t plane1dist = 100;

    EXPECT_EQ(SIDE_BACK, BrushMostlyOnSide(brush, plane1normal, plane1dist));

    FreeBrush(brush);
}

TEST(qbsp, BoundBrush)
{
    brush_t *brush = load128x128x32Brush();

    brush->bounds = {};

    EXPECT_EQ(true, BoundBrush(brush));

    EXPECT_DOUBLE_EQ(-64, brush->bounds.mins()[0]);
    EXPECT_DOUBLE_EQ(-64, brush->bounds.mins()[1]);
    EXPECT_DOUBLE_EQ(-16, brush->bounds.mins()[2]);

    EXPECT_DOUBLE_EQ(64, brush->bounds.maxs()[0]);
    EXPECT_DOUBLE_EQ(64, brush->bounds.maxs()[1]);
    EXPECT_DOUBLE_EQ(16, brush->bounds.maxs()[2]);

    FreeBrush(brush);
}

static void checkForAllCubeNormals(const brush_t *brush)
{
    const vec3_t wanted[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

    bool found[6];
    for (int i = 0; i < 6; i++) {
        found[i] = false;
    }

    for (const face_t *face = brush->faces; face; face = face->next) {
        const plane_t faceplane = Face_Plane(face);

        for (int i = 0; i < 6; i++) {
            if (VectorCompare(wanted[i], faceplane.normal, NORMAL_EPSILON)) {
                EXPECT_FALSE(found[i]);
                found[i] = true;
            }
        }
    }

    for (int i = 0; i < 6; i++) {
        EXPECT_TRUE(found[i]);
    }
}

static void checkCube(const brush_t *brush)
{
    EXPECT_EQ(6, Brush_NumFaces(brush));

    checkForAllCubeNormals(brush);

    EXPECT_EQ(contentflags_t{CONTENTS_SOLID}, brush->contents);
}

TEST(qbsp, SplitBrush)
{
    brush_t *brush = load128x128x32Brush();

    const qvec3d planenormal {-1, 0, 0};
    int planeside;
    const int planenum = FindPlane(planenormal, 0.0, &planeside);

    brush_t *front, *back;
    SplitBrush(brush, planenum, planeside, &front, &back);

    ASSERT_NE(nullptr, front);
    ASSERT_NE(nullptr, back);

    // front
    EXPECT_DOUBLE_EQ(-64, front->bounds.mins()[0]);
    EXPECT_DOUBLE_EQ(-64, front->bounds.mins()[1]);
    EXPECT_DOUBLE_EQ(-16, front->bounds.mins()[2]);

    EXPECT_DOUBLE_EQ(0, front->bounds.maxs()[0]);
    EXPECT_DOUBLE_EQ(64, front->bounds.maxs()[1]);
    EXPECT_DOUBLE_EQ(16, front->bounds.maxs()[2]);

    checkCube(front);

    // back
    EXPECT_DOUBLE_EQ(0, back->bounds.mins()[0]);
    EXPECT_DOUBLE_EQ(-64, back->bounds.mins()[1]);
    EXPECT_DOUBLE_EQ(-16, back->bounds.mins()[2]);

    EXPECT_DOUBLE_EQ(64, back->bounds.maxs()[0]);
    EXPECT_DOUBLE_EQ(64, back->bounds.maxs()[1]);
    EXPECT_DOUBLE_EQ(16, back->bounds.maxs()[2]);

    checkCube(back);

    FreeBrush(brush);
    delete front;
    delete back;
}

TEST(qbsp, SplitBrushOnSide)
{
    brush_t *brush = load128x128x32Brush();

    const qvec3d planenormal {-1, 0, 0};
    int planeside;
    const int planenum = FindPlane(planenormal, -64.0, &planeside);

    brush_t *front, *back;
    SplitBrush(brush, planenum, planeside, &front, &back);

    ASSERT_NE(nullptr, front);
    checkCube(front);

    EXPECT_EQ(nullptr, back);
}

#if 0
TEST(qbsp, MemLeaks) {
    brush_t *brush = load128x128x32Brush();
    
    const qvec3d planenormal { -1, 0, 0 };
    int planeside;
    const int planenum = FindPlane(planenormal, 0.0, &planeside);
    
    for (int i=0; i<1000000; i++) {
        brush_t *front, *back;
        SplitBrush(brush, planenum, planeside, &front, &back);
        
        FreeBrush(front);
        FreeBrush(back);
    }
}
#endif

/**
 * Test that this skip face gets auto-corrected.
 */
TEST(qbsp, InvalidTextureProjection)
{
    const char *map = R"(
    // entity 0
    {
        "classname" "worldspawn"
        // brush 0
        {
            ( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) +2butn [ 0 -1 0 0 ] [ 0 0 -1 0 ] 0 1 1
            ( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) +2butn [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
            ( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) +2butn [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
            ( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) +2butn [ -1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
            ( 64 64 64 ) ( 64 65 64 ) ( 65 64 64 ) +2butn [ 1 0 0 -0 ] [ 0 -1 0 -0 ] -0 1 1
            ( -64 -64 -0 ) ( -63 -64 -0 ) ( -64 -63 -0 ) skip [ 0 0 0 0 ] [ 0 0 0 0 ] -0 1 1
        }
    }
    )";

    mapentity_t worldspawn = LoadMap(map);
    Q_assert(1 == worldspawn.nummapbrushes);

    const mapface_t *face = &worldspawn.mapbrush(0).face(5);
    ASSERT_EQ("skip", face->texname);
    const auto texvecs = face->get_texvecs();
    EXPECT_TRUE(IsValidTextureProjection(face->plane.normal, texvecs.row(0), texvecs.row(1)));
}

/**
 * Same as above but the texture scales are 0
 */
TEST(qbsp, InvalidTextureProjection2)
{
    const char *map = R"(
    // entity 0
    {
        "classname" "worldspawn"
        // brush 0
        {
            ( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) +2butn [ 0 -1 0 0 ] [ 0 0 -1 0 ] 0 1 1
            ( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) +2butn [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
            ( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) +2butn [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
            ( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) +2butn [ -1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
            ( 64 64 64 ) ( 64 65 64 ) ( 65 64 64 ) +2butn [ 1 0 0 -0 ] [ 0 -1 0 -0 ] -0 1 1
            ( -64 -64 -0 ) ( -63 -64 -0 ) ( -64 -63 -0 ) skip [ 0 0 0 0 ] [ 0 0 0 0 ] -0 0 0
        }
    }
    )";

    mapentity_t worldspawn = LoadMap(map);
    Q_assert(1 == worldspawn.nummapbrushes);

    const mapface_t *face = &worldspawn.mapbrush(0).face(5);
    ASSERT_EQ("skip", face->texname);
    const auto texvecs = face->get_texvecs();
    EXPECT_TRUE(IsValidTextureProjection(face->plane.normal, texvecs.row(0), texvecs.row(1)));
}

/**
 * More realistic: *lava1 has tex vecs perpendicular to face
 */
TEST(qbsp, InvalidTextureProjection3)
{
    const char *map = R"(
    // entity 0
    {
        "classname" "worldspawn"
        "wad" "Q.wad"
        // brush 0
        {
            ( 512 512 64 ) ( 512 512 -0 ) ( 512 448 64 ) *04mwat1 [ 0 1 0 0 ] [ 0 0 -1 0 ] -0 1 1
            ( -0 448 -0 ) ( -0 512 -0 ) ( -0 448 64 ) *04mwat1 [ 0 -1 0 0 ] [ -0 -0 -1 0 ] -0 1 1
            ( 512 512 64 ) ( -0 512 64 ) ( 512 512 -0 ) *04mwat1 [ -1 0 0 0 ] [ 0 0 -1 0 ] -0 1 1
            ( -0 448 -0 ) ( -0 448 64 ) ( 512 448 -0 ) *lava1 [ 0 1 0 0 ] [ 0 0 -1 0 ] -0 1 1
            ( 512 512 64 ) ( 512 448 64 ) ( -0 512 64 ) *04mwat1 [ 1 0 0 0 ] [ 0 -1 0 0 ] -0 1 1
            ( -0 448 -0 ) ( 512 448 -0 ) ( -0 512 -0 ) *04mwat1 [ -1 0 0 0 ] [ -0 -1 -0 -0 ] -0 1 1
        }
    }
    )";

    mapentity_t worldspawn = LoadMap(map);
    Q_assert(1 == worldspawn.nummapbrushes);

    const mapface_t *face = &worldspawn.mapbrush(0).face(3);
    ASSERT_EQ("*lava1", face->texname);
    const auto texvecs = face->get_texvecs();
    EXPECT_TRUE(IsValidTextureProjection(qvec3f(face->plane.normal), texvecs.row(0), texvecs.row(1)));
}

TEST(mathlib, WindingArea)
{
    winding_t w { 5 };

    // poor test.. but at least checks that the colinear point is treated correctly
    w[0] = { 0, 0, 0 };
    w[1] = { 0, 32, 0}; // colinear
    w[2] = { 0, 64, 0};
    w[3] = { 64, 64, 0};
    w[4] = { 64, 0, 0};

    EXPECT_EQ(64.0f * 64.0f, w.area());
}
