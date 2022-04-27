#include "gtest/gtest.h"

#include <qbsp/qbsp.hh>
#include <qbsp/map.hh>
#include <common/fs.hh>
#include <common/bsputils.hh>
#include <common/qvec.hh>
#include <testmaps.hh>

#include <cstring>
#include <map>

// FIXME: Clear global data (planes, etc) between each test

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

    CalculateWorldExtent();

    return worldspawn;
}

static mbsp_t LoadTestmap(const std::filesystem::path &name)
{
    auto map_path = std::filesystem::path(testmaps_dir) / name;
    auto bsp_path = map_path;
    bsp_path.replace_extension(".bsp");

    InitQBSP({"", "-nopercent", "-noprogress", "-keepprt", map_path.string(), bsp_path.string()});

    ProcessFile();

    if (strlen(test_quake_maps_dir) > 0) {
        auto dest = fs::path(test_quake_maps_dir) / name;
        dest.replace_extension(".bsp");
        fs::copy(options.bsp_path, dest, fs::copy_options::overwrite_existing);
    }

    // re-open the .bsp and return it

    options.bsp_path.replace_extension("bsp");
    
    bspdata_t bspdata;
    LoadBSPFile(options.bsp_path, &bspdata);

    bspdata.version->game->init_filesystem(options.bsp_path, options);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    return std::get<mbsp_t>(bspdata.bsp);
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
    EXPECT_EQ(0, worldspawn.brushes.size());
    EXPECT_EQ(6, worldspawn.mapbrush(0).numfaces);

    std::optional<brush_t> brush =
        LoadBrush(&worldspawn, &worldspawn.mapbrush(0), {CONTENTS_SOLID}, {}, rotation_t::none, 0);
    ASSERT_NE(std::nullopt, brush);
    EXPECT_EQ(6, brush->faces.size());
}

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
    winding_t w{5};

    // poor test.. but at least checks that the colinear point is treated correctly
    w[0] = {0, 0, 0};
    w[1] = {0, 32, 0}; // colinear
    w[2] = {0, 64, 0};
    w[3] = {64, 64, 0};
    w[4] = {64, 0, 0};

    EXPECT_EQ(64.0f * 64.0f, w.area());
}

// Q1 testmaps

TEST(testmaps_q1, simple_sealed)
{
    mbsp_t result = LoadTestmap("qbsp_simple_sealed.map");

    ASSERT_EQ(map.brushes.size(), 6);

    ASSERT_EQ(result.dleafs.size(), 2);

    ASSERT_EQ(result.dleafs[0].contents, CONTENTS_SOLID);
    ASSERT_EQ(result.dleafs[1].contents, CONTENTS_EMPTY);
    
    // just a hollow box
    ASSERT_EQ(result.dfaces.size(), 6);
}

TEST(testmaps_q1, simple_sealed2)
{
    mbsp_t result = LoadTestmap("qbsp_simple_sealed2.map");

    ASSERT_EQ(map.brushes.size(), 14);

    ASSERT_EQ(result.dleafs.size(), 3);
    
    ASSERT_EQ(result.dleafs[0].contents, CONTENTS_SOLID);
    ASSERT_EQ(result.dleafs[1].contents, CONTENTS_EMPTY);
    ASSERT_EQ(result.dleafs[2].contents, CONTENTS_EMPTY);

    // L-shaped room
    // 2 ceiling + 2 floor + 6 wall faces
    ASSERT_EQ(result.dfaces.size(), 10);
}

TEST(testmaps_q1, simple_worldspawn_worldspawn)
{
    const mbsp_t bsp = LoadTestmap("qbsp_simple_worldspawn_worldspawn.map");

    // 6 for the room
    // 1 for the button
    ASSERT_EQ(map.brushes.size(), 7);

    // 1 solid leaf
    // 5 empty leafs around the button
    ASSERT_EQ(bsp.dleafs.size(), 6);

    // 5 faces for the "button"
    // 9 faces for the room (6 + 3 extra for the floor splits)
    ASSERT_EQ(bsp.dfaces.size(), 14);

    int fan_faces = 0;
    int room_faces = 0;
    for (auto &face : bsp.dfaces) {
        const char *texname = Face_TextureName(&bsp, &face);
        if (!strcmp(texname, "orangestuff8")) {
            ++room_faces;
        } else if (!strcmp(texname, "+0fan")) {
            ++fan_faces;
        } else {
            FAIL();
        }
    }
    ASSERT_EQ(fan_faces, 5);
    ASSERT_EQ(room_faces, 9);
}

TEST(testmaps_q1, simple_worldspawn_detail_wall)
{
    const mbsp_t bsp = LoadTestmap("qbsp_simple_worldspawn_detail_wall.map");

    ASSERT_FALSE(map.leakfile);

    // 6 for the room
    // 1 for the button
    ASSERT_EQ(map.brushes.size(), 7);

    // 5 faces for the "button"
    // 6 faces for the room
    ASSERT_EQ(bsp.dfaces.size(), 11);
}

TEST(testmaps_q1, simple_worldspawn_detail)
{
    const mbsp_t bsp = LoadTestmap("qbsp_simple_worldspawn_detail.map");

    ASSERT_FALSE(map.leakfile);

    // 6 for the room
    // 1 for the button
    ASSERT_EQ(map.brushes.size(), 7);

    // 5 faces for the "button"
    // 9 faces for the room
    ASSERT_EQ(bsp.dfaces.size(), 14);
}

TEST(testmaps_q1, noclipfaces)
{
    const mbsp_t bsp = LoadTestmap("qbsp_noclipfaces.map");

    ASSERT_FALSE(map.leakfile);

    ASSERT_EQ(bsp.dfaces.size(), 2);

    // TODO: contents should be empty in hull0 because it's func_detail_illusionary

    for (auto &face : bsp.dfaces) {
        ASSERT_STREQ("{trigger", Face_TextureName(&bsp, &face));
    }
}

/**
 * Same as previous test, but the T shaped brush entity has _mirrorinside
 */
TEST(testmaps_q1, noclipfaces_mirrorinside)
{
    const mbsp_t bsp = LoadTestmap("qbsp_noclipfaces_mirrorinside.map");

    ASSERT_FALSE(map.leakfile);

    ASSERT_EQ(bsp.dfaces.size(), 4);
    
    // TODO: contents should be empty in hull0 because it's func_detail_illusionary

    for (auto &face : bsp.dfaces) {
        ASSERT_STREQ("{trigger", Face_TextureName(&bsp, &face));
    }
}

TEST(testmaps_q1, detail_doesnt_seal)
{
    const mbsp_t bsp = LoadTestmap("qbsp_detail_doesnt_seal.map");

    ASSERT_TRUE(map.leakfile);
}

TEST(testmaps_q1, detail_doesnt_remove_world_nodes)
{
    const mbsp_t bsp = LoadTestmap("qbsp_detail_doesnt_remove_world_nodes.map");

    ASSERT_FALSE(map.leakfile);

    {
        // check for a face under the start pos
        const qvec3d floor_under_start{-56, -72, 64};
        auto *floor_under_start_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], floor_under_start, {0, 0, 1});
        ASSERT_NE(nullptr, floor_under_start_face);
    }

    {
        // floor face should be clipped away by detail
        const qvec3d floor_inside_detail{64, -72, 64};
        auto *floor_inside_detail_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], floor_inside_detail, {0, 0, 1});
        ASSERT_EQ(nullptr, floor_inside_detail_face);
    }

    {
        // but the sturctural nodes/leafs should not be clipped away by detail
        const qvec3d covered_by_detail{48, -88, 128};
        auto *covered_by_detail_node = BSP_FindNodeAtPoint(&bsp, &bsp.dmodels[0], covered_by_detail, {-1, 0, 0});
        ASSERT_NE(nullptr, covered_by_detail_node);
    }
}

TEST(testmaps_q1, merge)
{
    const mbsp_t bsp = LoadTestmap("qbsp_merge.map");

    ASSERT_EQ(9, map.brushes.size());

    ASSERT_TRUE(map.leakfile);
    ASSERT_EQ(6, bsp.dfaces.size());
}

TEST(testmaps_q1, tjunc_many_sided_face)
{
    const mbsp_t bsp = LoadTestmap("qbsp_tjunc_many_sided_face.map");

    ASSERT_FALSE(map.leakfile);

    std::map<qvec3d, std::vector<const mface_t *>> faces_by_normal;
    for (auto &face : bsp.dfaces) {
        faces_by_normal[Face_Normal(&bsp, &face)].push_back(&face);
    }

    ASSERT_EQ(6, faces_by_normal.size());

    // the floor has a 0.1 texture scale, so it gets subdivided into many small faces
    EXPECT_EQ(15 * 15, (faces_by_normal.at({0, 0, 1}).size()));

    // the ceiling gets split into 2 faces because fixing T-Junctions with all of the
    // wall sections exceeds the max vertices per face limit
    EXPECT_EQ(2, (faces_by_normal.at({0, 0, -1}).size()));
}

/**
 * Because it comes second, the sbutt2 brush should "win" in clipping against the floor,
 * in both a worldspawn test case, as well as a func_wall.
 */
TEST(testmaps_q1, brush_clipping_order)
{
    const mbsp_t bsp = LoadTestmap("qbsp_brush_clipping_order.map");

    ASSERT_FALSE(map.leakfile);

    const qvec3d world_button{-8, -8, 16};
    const qvec3d func_wall_button{152, -8, 16};

    // 0 = world, 1 = func_wall
    ASSERT_EQ(2, bsp.dmodels.size());

    ASSERT_EQ(20, bsp.dfaces.size());

    ASSERT_EQ(10, bsp.dmodels[0].numfaces); // 5 faces for the sides + bottom, 5 faces for the top
    ASSERT_EQ(10, bsp.dmodels[1].numfaces); // (same on worldspawn and func_wall)

    auto *world_button_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], world_button, {0, 0, 1});
    ASSERT_NE(nullptr, world_button_face);
    ASSERT_STREQ("sbutt2", Face_TextureName(&bsp, world_button_face));

    auto *func_wall_button_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[1], func_wall_button, {0, 0, 1});
    ASSERT_NE(nullptr, func_wall_button_face);
    ASSERT_STREQ("sbutt2", Face_TextureName(&bsp, func_wall_button_face));
}
