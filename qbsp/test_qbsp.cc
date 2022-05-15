#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <qbsp/brush.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/map.hh>
#include <common/fs.hh>
#include <common/bsputils.hh>
#include <common/qvec.hh>
#include <testmaps.hh>

#include <subprocess.h>
#include <nanobench.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <map>

using namespace testing;

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

#include <common/bspinfo.hh>

static mbsp_t LoadTestmapRef(const std::filesystem::path &name)
{
    const char *destdir = test_quake2_maps_dir;
    if (strlen(destdir) == 0) {
        return {};
    }

    auto testmap_path = std::filesystem::path(testmaps_dir) / name;
    auto map_in_game_path = fs::path(destdir) / name;
    fs::copy(testmap_path, map_in_game_path, fs::copy_options::overwrite_existing);

    std::string map_string = map_in_game_path.generic_string();

    const char *command_line[] = {R"(C:\Users\Eric\Documents\q2tools-220\x64\Debug\4bsp.exe)",
        map_string.c_str(),
        NULL};

    struct subprocess_s subprocess;
    int result = subprocess_create(command_line, 0, &subprocess);
    if (0 != result) {
        throw std::runtime_error("error launching process");
    }

    int retcode;
    if (0 != subprocess_join(&subprocess, &retcode)) {
        throw std::runtime_error("error joining");
    }

    // re-open the .bsp and return it
    fs::path bsp_path = map_in_game_path;
    bsp_path.replace_extension("bsp");

    bspdata_t bspdata;
    LoadBSPFile(bsp_path, &bspdata);

    bspdata.version->game->init_filesystem(bsp_path, options);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    // write to .json for inspection
    serialize_bsp(bspdata, std::get<mbsp_t>(bspdata.bsp), fs::path(bsp_path).replace_extension(".bsp.json"));

    return std::get<mbsp_t>(bspdata.bsp);
}

static mbsp_t LoadTestmap(const std::filesystem::path &name, std::vector<std::string> extra_args = {})
{
    auto map_path = std::filesystem::path(testmaps_dir) / name;
    auto bsp_path = map_path;
    bsp_path.replace_extension(".bsp");

    std::vector<std::string> args{"", "-nopercent", "-noprogress", "-keepprt"};
    for (auto &arg : extra_args) {
        args.push_back(arg);
    }
    args.push_back(map_path.string());
    args.push_back(bsp_path.string());

    // run qbsp

    InitQBSP(args);
    ProcessFile();
    
    const char *destdir = "";

    // read cmake variables TEST_QUAKE_MAP_EXPORT_DIR / TEST_QUAKE2_MAP_EXPORT_DIR
    if (options.target_game->id == GAME_QUAKE_II) {
        destdir = test_quake2_maps_dir;
    } else if (options.target_game->id == GAME_QUAKE) {
        destdir = test_quake_maps_dir;
    }

    // copy .bsp to game's basedir/maps directory, for easy in-game testing
    if (strlen(destdir) > 0) {
        auto dest = fs::path(destdir) / name;
        dest.replace_extension(".bsp");
        fs::copy(options.bsp_path, dest, fs::copy_options::overwrite_existing);
    }

    // re-open the .bsp and return it
    options.bsp_path.replace_extension("bsp");
    
    bspdata_t bspdata;
    LoadBSPFile(options.bsp_path, &bspdata);

    bspdata.version->game->init_filesystem(options.bsp_path, options);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    // write to .json for inspection
    serialize_bsp(bspdata, std::get<mbsp_t>(bspdata.bsp), fs::path(options.bsp_path).replace_extension(".bsp.json"));

    return std::get<mbsp_t>(bspdata.bsp);
}

static mbsp_t LoadBsp(const std::filesystem::path &path_in)
{
    std::filesystem::path path = path_in;

    bspdata_t bspdata;
    LoadBSPFile(path, &bspdata);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    return std::get<mbsp_t>(bspdata.bsp);
}

static std::map<std::string, std::vector<const mface_t *>> MakeTextureToFaceMap(const mbsp_t &bsp)
{ 
    std::map<std::string, std::vector<const mface_t *>> result;

    for (auto &face : bsp.dfaces) {
        result[Face_TextureName(&bsp, &face)].push_back(&face);
    }

    return result;
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

/**
 * checks that options are reset across tests.
 * set two random options and check that they don't carry over.
 */
TEST(testmaps_q1, options_reset1)
{
    LoadTestmap("qbsp_simple_sealed.map", {"-transsky"});

    EXPECT_FALSE(options.forcegoodtree.value());
    EXPECT_TRUE(options.transsky.value());
}

TEST(testmaps_q1, options_reset2)
{
    LoadTestmap("qbsp_simple_sealed.map", {"-forcegoodtree"});
        
    EXPECT_TRUE(options.forcegoodtree.value());
    EXPECT_FALSE(options.transsky.value());
}

/**
 * The brushes are touching but not intersecting, so ChopBrushes shouldn't change anything.
 */
TEST(testmaps_q1, chop_no_change)
{
    LoadTestmap("qbsp_chop_no_change.map");

    // TODO: ideally we should check we get back the same brush pointers from ChopBrushes
}


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
    mbsp_t bsp = LoadTestmap("qbsp_simple_sealed2.map");

    EXPECT_EQ(map.brushes.size(), 14);

    EXPECT_EQ(bsp.dleafs.size(), 3);
    
    EXPECT_EQ(bsp.dleafs[0].contents, CONTENTS_SOLID);
    EXPECT_EQ(bsp.dleafs[1].contents, CONTENTS_EMPTY);
    EXPECT_EQ(bsp.dleafs[2].contents, CONTENTS_EMPTY);

    // L-shaped room
    // 2 ceiling + 2 floor + 6 wall faces
    EXPECT_EQ(bsp.dfaces.size(), 10);

    // get markfaces
    const qvec3d player_pos{-56, -96, 120};
    const qvec3d other_empty_leaf_pos{-71, -288, 102};
    auto *player_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos);
    auto *other_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], other_empty_leaf_pos);

    auto player_markfaces = Leaf_Markfaces(&bsp, player_leaf);
    auto other_markfaces = Leaf_Markfaces(&bsp, other_leaf);

    // other room's expected markfaces

    auto *other_floor = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-80, -272, 64), qvec3d(0, 0, 1));
    auto *other_ceil = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-80, -272, 192), qvec3d(0, 0, -1));
    auto *other_minus_x =
        BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-16, -272, 128), qvec3d(-1, 0, 0));
    auto *other_plus_x = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-128, -272, 128), qvec3d(1, 0, 0)); // +X normal wall (extends into player leaf)
    auto *other_plus_y =
        BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-64, -368, 128), qvec3d(0, 1, 0)); // back wall +Y normal

    EXPECT_THAT(other_markfaces, testing::UnorderedElementsAreArray({
        other_floor, other_ceil, other_minus_x, other_plus_x, other_plus_y
    }));
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

TEST(testmaps_q1, simple_worldspawn_detail_illusionary)
{
    const mbsp_t bsp = LoadTestmap("qbsp_simple_worldspawn_detail_illusionary.map");

    ASSERT_FALSE(map.leakfile);

    // 6 for the room
    // 1 for the button
    EXPECT_EQ(map.brushes.size(), 7);

    // 5 faces for the "button"
    // 6 faces for the room
    EXPECT_EQ(bsp.dfaces.size(), 11);

    // leaf/node counts
    EXPECT_EQ(11, bsp.dnodes.size()); // one node per face
    EXPECT_EQ(7, bsp.dleafs.size()); // shared solid leaf + 6 empty leafs inside the room

    // where the func_detail_illusionary sticks into the void
    const qvec3d illusionary_in_void{8, -40, 72};
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], illusionary_in_void)->contents);
}

TEST(testmaps_q1, simple_worldspawn_sky)
{
    const mbsp_t bsp = LoadTestmap("qbsp_simple_worldspawn_sky.map");

    ASSERT_FALSE(map.leakfile);

    // just a box with sky on the ceiling
    const auto textureToFace = MakeTextureToFaceMap(bsp);
    EXPECT_EQ(1, textureToFace.at("sky3").size());
    EXPECT_EQ(5, textureToFace.at("orangestuff8").size());

    // leaf/node counts
    // - we'd get 7 nodes if it's cut like a cube (solid outside), with 1 additional cut inside to divide sky / empty
    // - we'd get 11 if it's cut as the sky plane (1), then two open cubes (5 nodes each)
    // - can get in between values if it does some vertical cuts, then the sky plane, then other vertical cuts
    //
    // the 7 solution is better but the BSP heuristics won't help reach that one in this trivial test map
    EXPECT_THAT(bsp.dnodes.size(), AllOf(Ge(7), Le(11)));
    EXPECT_EQ(3, bsp.dleafs.size()); // shared solid leaf + empty + sky

    // check contents
    const qvec3d player_pos{-88, -64, 120};
    const double inside_sky_z = 232;

    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos)->contents);

    // way above map is solid - sky should not fill outwards
    // (otherwise, if you had sky with a floor further up above it, it's not clear where the leafs would be divided, or
    // if the floor contents would turn to sky, etc.)
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(0,0,500))->contents);

    EXPECT_EQ(CONTENTS_SKY, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], qvec3d(player_pos[0], player_pos[1], inside_sky_z))->contents);

    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d( 500,    0,    0))->contents);
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(-500,    0,    0))->contents);
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(   0,  500,    0))->contents);
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(   0, -500,    0))->contents);
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(   0,    0, -500))->contents);
}

TEST(testmaps_q1, water_detail_illusionary)
{
    const mbsp_t bsp = LoadTestmap("qbsp_water_detail_illusionary.map");

    EXPECT_FALSE(map.leakfile);

    const qvec3d inside_water_and_fence{-20, -52, 124};
    const qvec3d inside_fence{-20, -52, 172};

    EXPECT_EQ(BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_water_and_fence)->contents, CONTENTS_WATER);
    EXPECT_EQ(BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_fence)->contents, CONTENTS_EMPTY);

    const qvec3d underwater_face_pos{-40, -52, 124};
    const qvec3d above_face_pos{-40, -52, 172};

    // make sure the detail_illusionary face underwater isn't clipped away
    EXPECT_NE(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], underwater_face_pos, {-1, 0, 0}));
    EXPECT_NE(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], above_face_pos, {-1, 0, 0}));
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

TEST(testmaps_q1, detail_illusionary_intersecting)
{
    const mbsp_t bsp = LoadTestmap("qbsp_detail_illusionary_intersecting.map");

    EXPECT_FALSE(map.leakfile);

    // sides: 3*4 = 12
    // top: 3
    // bottom: 3
    EXPECT_EQ(bsp.dfaces.size(), 18);

    for (auto &face : bsp.dfaces) {
        EXPECT_STREQ("{trigger", Face_TextureName(&bsp, &face));
    }

    // top of cross
    EXPECT_EQ(1, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -50, 120), qvec3d(0, 0, 1)).size());

    // interior face that should be clipped away
    EXPECT_EQ(0, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -52, 116), qvec3d(0, -1, 0)).size());
}

TEST(testmaps_q1, detail_illusionary_noclipfaces_intersecting)
{
    const mbsp_t bsp = LoadTestmap("qbsp_detail_illusionary_noclipfaces_intersecting.map");

    EXPECT_FALSE(map.leakfile);

    for (auto &face : bsp.dfaces) {
        EXPECT_STREQ("{trigger", Face_TextureName(&bsp, &face));
    }

    // top of cross has 2 faces Z-fighting, because we disabled clipping
    EXPECT_EQ(2, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -50, 120), qvec3d(0, 0, 1)).size());

    // interior face not clipped away
    EXPECT_EQ(1, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -52, 116), qvec3d(0, -1, 0)).size());
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

/**
 * Box room with a rotating fan (just a cube). Works in a mod with hiprotate - AD, Quoth, etc.
 */
TEST(testmaps_q1, origin)
{
    const mbsp_t bsp = LoadTestmap("qbsp_origin.map");

    ASSERT_FALSE(map.leakfile);

    // 0 = world, 1 = rotate_object
    ASSERT_EQ(2, bsp.dmodels.size());

    // check that the origin brush didn't clip away any solid faces, or generate faces
    ASSERT_EQ(6, bsp.dmodels[1].numfaces);

    // FIXME: should the origin brush update the dmodel's origin too?
    ASSERT_EQ(qvec3f(0, 0, 0), bsp.dmodels[1].origin);

    // check that the origin brush updated the entity lump
    auto ents = EntData_Parse(bsp.dentdata);
    auto it = std::find_if(ents.begin(), ents.end(), 
        [](const entdict_t &dict) -> bool { return dict.get("classname") == "rotate_object"; });

    ASSERT_NE(it, ents.end());
    ASSERT_EQ("216 -216 340", it->get("origin"));
}

TEST(testmaps_q1, simple)
{
    const mbsp_t bsp = LoadTestmap("qbsp_simple.map");

    ASSERT_TRUE(map.leakfile);

}

/**
 * Lots of features in one map, more for testing in game than automated testing
 */
TEST(testmaps_q1, features)
{
    const mbsp_t bsp = LoadTestmap("qbspfeatures.map");

    ASSERT_FALSE(map.leakfile);
}

// q2 testmaps

TEST(testmaps_q2, detail) {
    const mbsp_t bsp = LoadTestmap("qbsp_q2_detail.map", {"-q2bsp"});

    EXPECT_FALSE(map.leakfile);
    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    // stats
    EXPECT_EQ(1, bsp.dmodels.size());
    // Q2 reserves leaf 0 as an invalid leaf

    // leafs:
    //  6 solid leafs outside the room (* can be more depending on when the "divider" is cut)
    //  1 empty leaf filling the room above the divider
    //  2 empty leafs + 1 solid leaf for divider
    //  1 detail leaf for button
    //  4 empty leafs around + 1 on top of button 

    std::map<int32_t, int> counts_by_contents;
    for (size_t i = 1; i < bsp.dleafs.size(); ++i) {
        ++counts_by_contents[bsp.dleafs[i].contents];
    }
    EXPECT_EQ(3, counts_by_contents.size()); // number of types


    EXPECT_EQ(1, counts_by_contents.at(Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL));
    EXPECT_EQ(8, counts_by_contents.at(0)); // empty leafs
    EXPECT_THAT(counts_by_contents.at(Q2_CONTENTS_SOLID), AllOf(Ge(7), Le(9)));

    // clusters:
    //  1 empty cluster filling the room above the divider
    //  2 empty clusters created by divider
    //  1 cluster for the part of the room with the button

    std::set<int> clusters;
    // first add the empty leafs
    for (size_t i = 1; i < bsp.dleafs.size(); ++i) {
        if (0 == bsp.dleafs[i].contents) {
            clusters.insert(bsp.dleafs[i].cluster);
        }
    }
    EXPECT_EQ(4, clusters.size());

    // various points in the main room cluster
    const qvec3d under_button{46, -64, 96}; // directly on the main floor plane
    const qvec3d inside_button{46, -64, 98};
    const qvec3d above_button{46, -64, 120};

    // side room (different cluster)
    const qvec3d side_room{-62, 76, 140};    

    // detail clips away world faces
    EXPECT_EQ(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], under_button, {0, 0, 1}));

    // check for correct contents
    auto *detail_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_button);
    EXPECT_EQ(Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL, detail_leaf->contents);

    // check for button (detail) brush
    EXPECT_EQ(1, Leaf_Brushes(&bsp, detail_leaf).size());
    EXPECT_EQ(Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL, 
                Leaf_Brushes(&bsp, detail_leaf).at(0)->contents);

    // get more leafs
    auto *empty_leaf_above_button = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], above_button);
    EXPECT_EQ(0, empty_leaf_above_button->contents);
    EXPECT_EQ(0, Leaf_Brushes(&bsp, empty_leaf_above_button).size());

    auto *empty_leaf_side_room = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], side_room);
    EXPECT_EQ(0, empty_leaf_side_room->contents);
    EXPECT_EQ(0, Leaf_Brushes(&bsp, empty_leaf_side_room).size());

    // check cluster indices
    EXPECT_EQ(empty_leaf_above_button->cluster, detail_leaf->cluster);
    EXPECT_NE(empty_leaf_side_room->contents, detail_leaf->cluster);
}

TEST(testmaps_q2, playerclip)
{
    const mbsp_t bsp = LoadTestmap("qbsp_q2_playerclip.map", {"-q2bsp"});

    EXPECT_FALSE(map.leakfile);
    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d in_playerclip{32, -136, 144};
    auto *playerclip_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_playerclip);
    EXPECT_EQ(Q2_CONTENTS_PLAYERCLIP, playerclip_leaf->contents);

    // make sure faces at these locations aren't clipped away
    const qvec3d floor_under_clip{32, -136, 96};
    const qvec3d pillar_side_in_clip1{32, -48, 144};
    const qvec3d pillar_side_in_clip2{32, -208, 144};

    EXPECT_NE(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], floor_under_clip, {0, 0, 1}));
    EXPECT_NE(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], pillar_side_in_clip1, {0, -1, 0}));
    EXPECT_NE(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], pillar_side_in_clip2, {0, 1, 0}));

    // make sure no face is generated for the playerclip brush
    const qvec3d playerclip_front_face{16, -152, 144};
    EXPECT_EQ(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], playerclip_front_face, {-1, 0, 0}));

    // check for brush
    EXPECT_EQ(1, Leaf_Brushes(&bsp, playerclip_leaf).size());
    EXPECT_EQ(Q2_CONTENTS_PLAYERCLIP, Leaf_Brushes(&bsp, playerclip_leaf).at(0)->contents);
}

TEST(testmaps_q2, areaportal)
{
    using namespace testing;
    const mbsp_t bsp = LoadTestmap("qbsp_q2_areaportal.map", {"-q2bsp"});

    EXPECT_FALSE(map.leakfile);
    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    // area 0 is a placeholder
    // areaportal 0 is a placeholder
    // 
    // the conceptual area portal has portalnum 1, and consists of two dareaportals entries with connections to area 1 and 2
    EXPECT_THAT(bsp.dareaportals, UnorderedElementsAre(dareaportal_t{0, 0}, dareaportal_t{1, 1}, dareaportal_t{1, 2}));
    EXPECT_THAT(bsp.dareas, UnorderedElementsAre(darea_t{0, 0}, darea_t{1, 1}, darea_t{1, 2}));

    // look up the leafs
    const qvec3d player_start{-88, -112, 120};
    const qvec3d other_room{128, -112, 120};
    const qvec3d areaportal_pos{32, -112, 120};
    const qvec3d void_pos{-408, -112, 120};

    auto *player_start_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_start);
    auto *other_room_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], other_room);
    auto *areaportal_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], areaportal_pos);
    auto *void_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], void_pos);

    // check leaf contents
    EXPECT_EQ(0, player_start_leaf->contents);
    EXPECT_EQ(0, other_room_leaf->contents);
    EXPECT_EQ(Q2_CONTENTS_AREAPORTAL, areaportal_leaf->contents);
    EXPECT_EQ(Q2_CONTENTS_SOLID, void_leaf->contents);

    // make sure faces at these locations aren't clipped away
    const qvec3d floor_under_areaportal{32, -136, 96};
    EXPECT_NE(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], floor_under_areaportal, {0, 0, 1}));

    // check for brushes
    EXPECT_EQ(1, Leaf_Brushes(&bsp, areaportal_leaf).size());
    EXPECT_EQ(Q2_CONTENTS_AREAPORTAL, Leaf_Brushes(&bsp, areaportal_leaf).at(0)->contents);

    EXPECT_EQ(1, Leaf_Brushes(&bsp, void_leaf).size());
    EXPECT_EQ(Q2_CONTENTS_SOLID, Leaf_Brushes(&bsp, void_leaf).at(0)->contents);

    // check leaf areas
    EXPECT_THAT((std::vector<int32_t>{1, 2}), UnorderedElementsAre(player_start_leaf->area, other_room_leaf->area));
    // the areaportal leaf itself actually gets assigned to one of the two sides' areas
    EXPECT_THAT(areaportal_leaf->area, AnyOf(1, 2));    
    EXPECT_THAT(0, void_leaf->area); // a solid leaf gets the invalid area

    // check the func_areaportal entity had its "style" set
    auto ents = EntData_Parse(bsp.dentdata);
    auto it = std::find_if(ents.begin(), ents.end(),
        [](const entdict_t &dict) { return dict.get("classname") == "func_areaportal"; });

    ASSERT_NE(it, ents.end());
    ASSERT_EQ("1", it->get("style"));
}

TEST(testmaps_q2, nodraw_light) {
    const mbsp_t bsp = LoadTestmap("qbsp_q2_nodraw_light.map", {"-q2bsp", "-includeskip"});

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d topface_center {160, -148, 208};
    auto *topface = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], topface_center, {0, 0, 1});
    ASSERT_NE(nullptr, topface);

    auto *texinfo = Face_Texinfo(&bsp, topface);
    EXPECT_STREQ(texinfo->texture.data(), "e1u1/trigger");
    EXPECT_EQ(texinfo->flags.native, Q2_SURF_LIGHT | Q2_SURF_NODRAW);
}

TEST(testmaps_q2, nodraw_detail_light) {
    const mbsp_t bsp = LoadTestmap("qbsp_q2_nodraw_detail_light.map", {"-q2bsp", "-includeskip"});

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d topface_center {160, -148, 208};
    auto *topface = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], topface_center, {0, 0, 1});
    ASSERT_NE(nullptr, topface);

    auto *texinfo = Face_Texinfo(&bsp, topface);
    EXPECT_STREQ(texinfo->texture.data(), "e1u1/trigger");
    EXPECT_EQ(texinfo->flags.native, Q2_SURF_LIGHT | Q2_SURF_NODRAW);
}

TEST(testmaps_q2, base1)
{
#if 0
    const mbsp_t bsp = LoadTestmap("base1.map", {"-q2bsp"});

    EXPECT_FALSE(map.leakfile);
    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    // bspinfo output from a compile done with
    // https://github.com/qbism/q2tools-220 at 46fd97bbe1b3657ca9e93227f89aaf0fbd3677c9.
    // only took a couple of seconds (debug build)

    //   35 models
    // 9918 planes           198360
    //10367 vertexes         124404
    // 5177 nodes            144956
    //  637 texinfos          48412
    // 7645 faces            152900
    // 5213 leafs            145964
    // 9273 leaffaces         18546
    // 7307 leafbrushes       14614
    //20143 edges             80572
    //37287 surfedges        149148
    // 1765 brushes           21180
    //15035 brushsides        60140
    //    3 areas                24
    //    3 areaportals          24
    //      lightdata             0
    //      visdata               0
    //      entdata           53623
#endif
}

TEST(testmaps_q2, base1leak)
{
    const mbsp_t bsp = LoadTestmap("base1leak.map", {"-q2bsp"});

    EXPECT_FALSE(map.leakfile);
    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    EXPECT_EQ(8, map.brushes.size());
    EXPECT_EQ(8, bsp.dbrushes.size());

    EXPECT_EQ(8, bsp.dleafs.size()); // 1 placeholder + 1 empty (room interior) + 6 solid (sides of room)

    const qvec3d in_plus_y_wall{-776, 976, -24};
    auto *plus_y_wall_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_plus_y_wall);
    EXPECT_EQ(Q2_CONTENTS_SOLID, plus_y_wall_leaf->contents);

    EXPECT_EQ(3, plus_y_wall_leaf->numleafbrushes);
}

TEST(benchmark, winding) {
    ankerl::nanobench::Bench bench;

    bench.run("std::vector<double> reserve(3*4*6)", [&] {
        std::vector<double> temp;
        temp.reserve(3 * 4 * 6);
        ankerl::nanobench::doNotOptimizeAway(temp);
    });
    bench.run("std::vector<qvec3d> reserve(4*6)", [&] {
        std::vector<qvec3d> temp;
        temp.reserve(4 * 6);
        ankerl::nanobench::doNotOptimizeAway(temp);
    });
    bench.run("std::array<double, 3*4*6>", [&] {
        std::array<double, 3 * 4 * 6> temp;
        ankerl::nanobench::doNotOptimizeAway(temp);
    });
    bench.run("std::array<qvec3d, 4*6>", [&] {
        std::array<qvec3d, 4 * 6> temp;
        ankerl::nanobench::doNotOptimizeAway(temp);
    });
    bench.run("polylib::winding_base_t<6> construct", [&] {
        polylib::winding_base_t<6> temp;
        ankerl::nanobench::doNotOptimizeAway(temp);
    });
}
