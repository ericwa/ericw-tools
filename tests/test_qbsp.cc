#include "test_qbsp.hh"

#include <qbsp/brush.hh>
#include <qbsp/brushbsp.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/map.hh>
#include <qbsp/csg.hh>
#include <common/fs.hh>
#include <common/bsputils.hh>
#include <common/decompile.hh>
#include <common/mapfile.hh>
#include <common/prtfile.hh>
#include <common/qvec.hh>
#include <common/log.hh>
#include <testmaps.hh>

#include <algorithm>
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <tuple>
#include <map>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "test_main.hh"

// FIXME: Clear global data (planes, etc) between each test

const mapface_t *Mapbrush_FirstFaceWithTextureName(const mapbrush_t &brush, const std::string &texname)
{
    for (auto &face : brush.faces) {
        if (face.texname == texname) {
            return &face;
        }
    }
    return nullptr;
}

void CheckFaceNormal(const mbsp_t *bsp, const mface_t *face)
{
    qvec3d face_normal_from_plane = Face_Normal(bsp, face);

    auto winding = Face_Winding(bsp, face);
    winding.remove_colinear();
    if (winding.size() < 3)
        return;

    auto winding_plane = winding.plane();

    EXPECT_GT(qv::dot(face_normal_from_plane, winding_plane.normal), 0.0);
}

void CheckBsp(const mbsp_t *bsp)
{
    for (const mface_t &face : bsp->dfaces) {
        CheckFaceNormal(bsp, &face);
    }
}

mapentity_t &LoadMap(const char *map, size_t length)
{
    ::map.reset();
    qbsp_options.reset();

    qbsp_options.target_version = &bspver_q1;
    qbsp_options.target_game = qbsp_options.target_version->game;

    parser_source_location base_location{testing::UnitTest::GetInstance()->current_test_info()->name()};
    mapfile::map_file_t m = mapfile::parse(std::string_view(map, length), base_location);

    // FIXME: adds the brush to the global map...
    texture_def_issues_t issue_stats;
    mapentity_t &entity = ::map.entities.emplace_back();
    ParseEntity(m.entities.at(0), entity, issue_stats);

    CalculateWorldExtent();

    return entity;
}

mapentity_t &LoadMap(const char *map)
{
    return LoadMap(map, strlen(map));
}

mapentity_t &LoadMapPath(const std::filesystem::path &name)
{
    auto filename = std::filesystem::path(testmaps_dir) / name;
    fs::data file_data = fs::load(filename);
    return LoadMap(reinterpret_cast<const char *>(file_data->data()), file_data->size());
}

#include <common/bspinfo.hh>

std::tuple<mbsp_t, bspxentries_t, std::optional<prtfile_t>> LoadTestmap(
    const std::filesystem::path &name, std::vector<std::string> extra_args)
{
    auto map_path = std::filesystem::path(testmaps_dir) / name;
    auto bsp_path = map_path;
    bsp_path.replace_extension(".bsp");

    auto wal_metadata_path = std::filesystem::path(testmaps_dir) / "q2_wal_metadata";

    std::vector<std::string> args{""}; // the exe path, which we're ignoring in this case
    if (std::ranges::find(extra_args, "-path") == extra_args.end()) {
        extra_args.push_back("-path");
        extra_args.push_back(wal_metadata_path.string());
    }

    if (!tests_verbose) {
        args.push_back("-noverbose");
    } else {
        args.push_back("-nopercent");
        args.push_back("-loghulls");
        args.push_back("-verbose");
    }

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
    if (qbsp_options.target_game->id == GAME_QUAKE_II) {
        destdir = test_quake2_maps_dir;
    } else if (qbsp_options.target_game->id == GAME_QUAKE) {
        destdir = test_quake_maps_dir;
    } else if (qbsp_options.target_game->id == GAME_HEXEN_II) {
        destdir = test_hexen2_maps_dir;
    } else if (qbsp_options.target_game->id == GAME_HALF_LIFE) {
        destdir = test_halflife_maps_dir;
    }

    // copy .bsp to game's basedir/maps directory, for easy in-game testing
    if (strlen(destdir) > 0) {
        auto dest = fs::path(destdir) / name.filename();
        dest.replace_extension(".bsp");
        fs::copy(qbsp_options.bsp_path, dest, fs::copy_options::overwrite_existing);
        logging::print("copied from {} to {}\n", qbsp_options.bsp_path, dest);
    }

    // re-open the .bsp and return it
    qbsp_options.bsp_path.replace_extension("bsp");

    bspdata_t bspdata;
    LoadBSPFile(qbsp_options.bsp_path, &bspdata);

    bspdata.version->game->init_filesystem(qbsp_options.bsp_path, qbsp_options);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    CheckBsp(&std::get<mbsp_t>(bspdata.bsp));

    // write to .json for inspection
    serialize_bsp(
        bspdata, std::get<mbsp_t>(bspdata.bsp), fs::path(qbsp_options.bsp_path).replace_extension(".bsp.json"));

    std::optional<prtfile_t> prtfile;
    if (const auto prtpath = fs::path(bsp_path).replace_extension(".prt"); fs::exists(prtpath)) {
        prtfile = {LoadPrtFile(prtpath, bspdata.loadversion)};
    }

    // decompile .bsp hulls
    if (qbsp_options.target_game->id == GAME_QUAKE) {
        fs::path decompiled_map_path = qbsp_options.bsp_path;
        decompiled_map_path.replace_extension("");
        decompiled_map_path.replace_filename(decompiled_map_path.stem().string() + "-decompiled-hull1");
        decompiled_map_path.replace_extension(".map");

        std::ofstream f(decompiled_map_path);

        if (!f)
            Error("couldn't open {} for writing\n", decompiled_map_path);

        decomp_options options;
        options.hullnum = 1;

        DecompileBSP(&std::get<mbsp_t>(bspdata.bsp), options, f);
    }

    return std::make_tuple(
        std::move(std::get<mbsp_t>(bspdata.bsp)), std::move(bspdata.bspx.entries), std::move(prtfile));
}

std::tuple<mbsp_t, bspxentries_t, std::optional<prtfile_t>> LoadTestmapQ2(
    const std::filesystem::path &name, std::vector<std::string> extra_args)
{
#if 0
    return LoadTestmapRef(name);
#else
    extra_args.insert(extra_args.begin(), "-q2bsp");
    return LoadTestmap(name, extra_args);
#endif
}

std::tuple<mbsp_t, bspxentries_t, std::optional<prtfile_t>> LoadTestmapQ1(
    const std::filesystem::path &name, std::vector<std::string> extra_args)
{
#if 0
    return LoadTestmapRefQ1(name);
#else
    return LoadTestmap(name, extra_args);
#endif
}

void CheckFilled(const mbsp_t &bsp, hull_index_t hullnum)
{
    int32_t contents = BSP_FindContentsAtPoint(&bsp, hullnum, &bsp.dmodels[0], qvec3d{8192, 8192, 8192});

    if (bsp.loadversion->game->id == GAME_QUAKE_II) {
        EXPECT_EQ(contents, Q2_CONTENTS_SOLID);
    } else {
        EXPECT_EQ(contents, CONTENTS_SOLID);
    }
}

void CheckFilled(const mbsp_t &bsp)
{
    if (bsp.loadversion->game->id == GAME_QUAKE_II) {
        CheckFilled(bsp, 0);
    } else {
        auto hullsizes = bsp.loadversion->game->get_hull_sizes();
        for (int i = 0; i < hullsizes.size(); ++i) {
            CheckFilled(bsp, i);
        }
    }
}

#if 0
mbsp_t LoadBsp(const std::filesystem::path &path_in)
{
    std::filesystem::path path = path_in;

    bspdata_t bspdata;
    LoadBSPFile(path, &bspdata);

    ConvertBSPFormat(&bspdata, &bspver_generic);

    return std::get<mbsp_t>(bspdata.bsp);
}
#endif

std::map<std::string, std::vector<const mface_t *>> MakeTextureToFaceMap(const mbsp_t &bsp)
{
    std::map<std::string, std::vector<const mface_t *>> result;

    for (auto &face : bsp.dfaces) {
        result[Face_TextureName(&bsp, &face)].push_back(&face);
    }

    return result;
}

const texvecf &GetTexvecs(const char *map, const char *texname)
{
    mapentity_t &worldspawn = LoadMap(map);

    const mapbrush_t &mapbrush = worldspawn.mapbrushes.front();
    const mapface_t *mapface = Mapbrush_FirstFaceWithTextureName(mapbrush, "tech02_1");
    Q_assert(nullptr != mapface);

    return mapface->get_texvecs();
}

std::vector<std::string> TexNames(const mbsp_t &bsp, std::vector<const mface_t *> faces)
{
    std::vector<std::string> result;
    for (auto &face : faces) {
        result.push_back(Face_TextureName(&bsp, face));
    }
    return result;
}

std::vector<const mface_t *> FacesWithTextureName(const mbsp_t &bsp, const std::string &name)
{
    std::vector<const mface_t *> result;
    for (auto &face : bsp.dfaces) {
        if (Face_TextureName(&bsp, &face) == name) {
            result.push_back(&face);
        }
    }
    return result;
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
            EXPECT_EQ(doctest::Approx(texvecsExpected[i][j]), texvecsActual[i][j]);
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

    mapentity_t &worldspawn = LoadMap(mapWithDuplicatePlanes);
    ASSERT_EQ(1, worldspawn.mapbrushes.size());
    EXPECT_EQ(6, worldspawn.mapbrushes.front().faces.size());

    auto *game = bspver_q1.game;

    auto brush = LoadBrush(
        worldspawn, worldspawn.mapbrushes.front(), game->create_contents_from_native(CONTENTS_SOLID), 0, std::nullopt);
    EXPECT_EQ(6, brush->sides.size());
}

TEST(qbsp, emptyBrush)
{
    SCOPED_TRACE("the empty brush should be discarded");
    const char *map_with_empty_brush = R"(
// entity 0
{
"mapversion" "220"
"classname" "worldspawn"
// brush 0
{
( 80 -64 -16 ) ( 80 -63 -16 ) ( 80 -64 -15 ) __TB_empty [ 0 -1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 80 -64 -16 ) ( 80 -64 -15 ) ( 81 -64 -16 ) __TB_empty [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 80 -64 -16 ) ( 81 -64 -16 ) ( 80 -63 -16 ) __TB_empty [ -1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 208 64 16 ) ( 208 65 16 ) ( 209 64 16 ) __TB_empty [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 208 64 16 ) ( 209 64 16 ) ( 208 64 17 ) __TB_empty [ -1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 208 64 16 ) ( 208 64 17 ) ( 208 65 16 ) __TB_empty [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
}
{
}
// brush 1
{
( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) __TB_empty [ 0 -1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) __TB_empty [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -64 -64 -16 ) ( -63 -64 -16 ) ( -64 -63 -16 ) __TB_empty [ -1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 64 64 16 ) ( 64 65 16 ) ( 65 64 16 ) __TB_empty [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) __TB_empty [ -1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) __TB_empty [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
}
}
    )";

    mapentity_t &worldspawn = LoadMap(map_with_empty_brush);
    ASSERT_EQ(2, worldspawn.mapbrushes.size());
    ASSERT_EQ(6, worldspawn.mapbrushes[0].faces.size());
    ASSERT_EQ(6, worldspawn.mapbrushes[1].faces.size());
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

    mapfile::map_file_t m;
    parser_t p(map, parser_source_location());
    m.parse(p);

    ASSERT_EQ(1, m.entities[0].brushes.size());

    const auto *face = &m.entities[0].brushes.front().faces[5];
    ASSERT_EQ("skip", face->texture);

    EXPECT_TRUE(face->is_valid_texture_projection());
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

    mapfile::map_file_t m;
    parser_t p(map, parser_source_location());
    m.parse(p);

    ASSERT_EQ(1, m.entities[0].brushes.size());

    const auto *face = &m.entities[0].brushes.front().faces[5];
    ASSERT_EQ("skip", face->texture);

    EXPECT_TRUE(face->is_valid_texture_projection());
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

    mapfile::map_file_t m;
    parser_t p(map, parser_source_location());
    m.parse(p);

    ASSERT_EQ(1, m.entities[0].brushes.size());

    const auto *face = &m.entities[0].brushes.front().faces[3];
    ASSERT_EQ("*lava1", face->texture);

    EXPECT_TRUE(face->is_valid_texture_projection());
}

TEST(winding, WindingArea)
{
    winding_t w(5);

    // poor test.. but at least checks that the colinear point is treated correctly
    w[0] = {0, 0, 0};
    w[1] = {0, 32, 0}; // colinear
    w[2] = {0, 64, 0};
    w[3] = {64, 64, 0};
    w[4] = {64, 0, 0};

    EXPECT_EQ(64.0f * 64.0f, w.area());
}

/**
 * checks that options are reset across tests.
 * set two random options and check that they don't carry over.
 */
TEST(testmapsQ1, optionsReset1)
{
    LoadTestmap("qbsp_simple_sealed.map", {"-noskip"});

    EXPECT_FALSE(qbsp_options.forcegoodtree.value());
    EXPECT_TRUE(qbsp_options.noskip.value());
}

TEST(testmapsQ1, optionsReset2)
{
    LoadTestmap("qbsp_simple_sealed.map", {"-forcegoodtree"});

    EXPECT_TRUE(qbsp_options.forcegoodtree.value());
    EXPECT_FALSE(qbsp_options.noskip.value());
}

/**
 * The brushes are touching but not intersecting, so ChopBrushes shouldn't change anything.
 */
TEST(testmapsQ1, chopNoChange)
{
    LoadTestmapQ1("qbsp_chop_no_change.map");

    // TODO: ideally we should check we get back the same brush pointers from ChopBrushes
}

TEST(testmapsQ1, simpleSealed)
{
    const std::vector<std::string> quake_maps{"qbsp_simple_sealed.map", "qbsp_simple_sealed_rotated.map"};

    for (const auto &mapname : quake_maps) {
        SCOPED_TRACE(fmt::format("testing {}", mapname));

        const auto [bsp, bspx, prt] = LoadTestmapQ1(mapname);

        ASSERT_EQ(bsp.dleafs.size(), 2);

        ASSERT_EQ(bsp.dleafs[0].contents, CONTENTS_SOLID);
        ASSERT_EQ(bsp.dleafs[1].contents, CONTENTS_EMPTY);

        // just a hollow box
        ASSERT_EQ(bsp.dfaces.size(), 6);

        // no bspx lumps
        EXPECT_TRUE(bspx.empty());

        // check markfaces
        EXPECT_EQ(bsp.dleafs[0].nummarksurfaces, 0);
        EXPECT_EQ(bsp.dleafs[0].firstmarksurface, 0);

        EXPECT_EQ(bsp.dleafs[1].nummarksurfaces, 6);
        EXPECT_EQ(bsp.dleafs[1].firstmarksurface, 0);
        EXPECT_THAT(bsp.dleaffaces, testing::UnorderedElementsAre(0, 1, 2, 3, 4, 5));
    }
}

TEST(testmapsQ1, simpleSealed2)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_sealed2.map");

    ASSERT_EQ(bsp.dleafs.size(), 3);

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
    auto *other_minus_x = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-16, -272, 128), qvec3d(-1, 0, 0));
    auto *other_plus_x = BSP_FindFaceAtPoint(
        &bsp, &bsp.dmodels[0], qvec3d(-128, -272, 128), qvec3d(1, 0, 0)); // +X normal wall (extends into player leaf)
    auto *other_plus_y =
        BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-64, -368, 128), qvec3d(0, 1, 0)); // back wall +Y normal

    EXPECT_THAT(other_markfaces,
        testing::UnorderedElementsAre(other_floor, other_ceil, other_minus_x, other_plus_x, other_plus_y));
}

TEST(testmapsQ1, q1FuncIllusionaryVisblocker)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_func_illusionary_visblocker.map", {});

    EXPECT_EQ(prt->portalleafs, 3);
    EXPECT_EQ(prt->portals.size(), 0);
}

TEST(testmapsQ1, q1FuncIllusionaryVisblockerInteractions)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_func_illusionary_visblocker_interactions.map", {});

    {
        SCOPED_TRACE("func_illusionary_visblocker and func_detail_illusionary");
        SCOPED_TRACE("should have 2 faces between");

        EXPECT_EQ(2, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], {-8, 16, 104}).size());
    }

    {
        SCOPED_TRACE("func_illusionary_visblocker and func_detail_illusionary (mirrorinside 1)");
        SCOPED_TRACE("should have 2 faces between");

        EXPECT_EQ(2, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], {136, 16, 104}).size());
    }

    {
        SCOPED_TRACE("func_illusionary_visblocker (mirrorinside 0) and func_detail_illusionary");
        SCOPED_TRACE("should have 1 or 2 faces between");

        EXPECT_THAT(BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], {280, 16, 104}).size(),
            testing::AllOf(testing::Ge(1), testing::Le(2)));

        // make sure mirrorinside 0 works
        EXPECT_EQ(1, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], {280, -48, 104}).size());
    }

    {
        SCOPED_TRACE("func_illusionary_visblocker (mirrorinside 0) and func_detail_illusionary (mirrorinside 1)");
        SCOPED_TRACE("should have 1 or 2 faces between");

        EXPECT_THAT(BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], {424, 16, 104}).size(),
            testing::AllOf(testing::Ge(1), testing::Le(2)));
    }
}

TEST(testmapsQ1, simpleWorldspawnWorldspawn)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_worldspawn_worldspawn.map", {"-tjunc", "rotate"});

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

TEST(testmapsQ1, simpleWorldspawnDetailWall)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_worldspawn_detail_wall.map");

    EXPECT_TRUE(prt.has_value());

    // 5 faces for the "button"
    // 6 faces for the room
    EXPECT_EQ(bsp.dfaces.size(), 11);

    const qvec3d button_pos = {16, -48, 104};
    auto *button_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], button_pos);

    EXPECT_EQ(button_leaf->contents, CONTENTS_SOLID);
    EXPECT_EQ(button_leaf, &bsp.dleafs[0]); // should be using shared solid leaf because it's func_detail_wall
}

TEST(testmapsQ1, simpleWorldspawnDetail)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_worldspawn_detail.map", {"-tjunc", "rotate"});

    ASSERT_TRUE(prt.has_value());

    // 5 faces for the "button"
    // 9 faces for the room
    ASSERT_EQ(bsp.dfaces.size(), 14);

    // 6 for the box room
    // 5 for the "button"
    EXPECT_EQ(bsp.dnodes.size(), 11);

    // this is how many we get with ericw-tools-v0.18.1-32-g6660c5f-win64
    EXPECT_LE(bsp.dclipnodes.size(), 22);
}

TEST(testmapsQ1, simpleWorldspawnDetailIllusionary)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_worldspawn_detail_illusionary.map");

    ASSERT_TRUE(prt.has_value());

    // 5 faces for the "button"
    // 6 faces for the room
    EXPECT_EQ(bsp.dfaces.size(), 11);

    // leaf/node counts
    EXPECT_EQ(11, bsp.dnodes.size()); // one node per face
    EXPECT_EQ(7, bsp.dleafs.size()); // shared solid leaf + 6 empty leafs inside the room

    // where the func_detail_illusionary sticks into the void
    const qvec3d illusionary_in_void{8, -40, 72};
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], illusionary_in_void)->contents);

    EXPECT_EQ(prt->portals.size(), 0);
    EXPECT_EQ(prt->portalleafs, 1);
}

TEST(testmapsQ1, simpleWorldspawnSky)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_worldspawn_sky.map");

    ASSERT_TRUE(prt.has_value());

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
    EXPECT_GE(bsp.dnodes.size(), 7);
    EXPECT_LE(bsp.dnodes.size(), 11);
    EXPECT_EQ(3, bsp.dleafs.size()); // shared solid leaf + empty + sky

    // check contents
    const qvec3d player_pos{-88, -64, 120};
    const double inside_sky_z = 232;

    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos)->contents);

    // way above map is solid - sky should not fill outwards
    // (otherwise, if you had sky with a floor further up above it, it's not clear where the leafs would be divided, or
    // if the floor contents would turn to sky, etc.)
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(0, 0, 500))->contents);

    EXPECT_EQ(CONTENTS_SKY,
        BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], qvec3d(player_pos[0], player_pos[1], inside_sky_z))->contents);

    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(500, 0, 0))->contents);
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(-500, 0, 0))->contents);
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(0, 500, 0))->contents);
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(0, -500, 0))->contents);
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(0, 0, -500))->contents);

    EXPECT_EQ(prt->portals.size(), 0);
    // FIXME: unsure what the expected number of visclusters is, does sky get one?

    EXPECT_EQ(12, bsp.dclipnodes.size());
}

TEST(testmapsQ1, waterDetailIllusionary)
{
    static const std::string basic_mapname = "qbsp_water_detail_illusionary.map";
    static const std::string mirrorinside_mapname = "qbsp_water_detail_illusionary_mirrorinside.map";

    for (const auto &mapname : {basic_mapname, mirrorinside_mapname}) {
        SCOPED_TRACE(fmt::format("testing {}", mapname));

        const auto [bsp, bspx, prt] = LoadTestmapQ1(mapname);

        ASSERT_TRUE(prt.has_value());

        const qvec3d inside_water_and_fence{-20, -52, 124};
        const qvec3d inside_fence{-20, -52, 172};

        EXPECT_EQ(BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_water_and_fence)->contents, CONTENTS_WATER);
        EXPECT_EQ(BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_fence)->contents, CONTENTS_EMPTY);

        const qvec3d underwater_face_pos{-40, -52, 124};
        const qvec3d above_face_pos{-40, -52, 172};

        // make sure the detail_illusionary face underwater isn't clipped away
        auto *underwater_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], underwater_face_pos, {-1, 0, 0});
        auto *underwater_face_inner = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], underwater_face_pos, {1, 0, 0});

        auto *above_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], above_face_pos, {-1, 0, 0});
        auto *above_face_inner = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], above_face_pos, {1, 0, 0});

        ASSERT_NE(nullptr, underwater_face);
        ASSERT_NE(nullptr, above_face);

        EXPECT_EQ(std::string("{trigger"), Face_TextureName(&bsp, underwater_face));
        EXPECT_EQ(std::string("{trigger"), Face_TextureName(&bsp, above_face));

        if (mapname == mirrorinside_mapname) {
            ASSERT_NE(underwater_face_inner, nullptr);
            ASSERT_NE(above_face_inner, nullptr);

            EXPECT_EQ(std::string("{trigger"), Face_TextureName(&bsp, underwater_face_inner));
            EXPECT_EQ(std::string("{trigger"), Face_TextureName(&bsp, above_face_inner));
        } else {
            EXPECT_EQ(underwater_face_inner, nullptr);
            EXPECT_EQ(above_face_inner, nullptr);
        }
    }
}

TEST(testmapsQ1, bmodelMirrorinsideWithLiquid)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_bmodel_mirrorinside_with_liquid.map");

    ASSERT_TRUE(prt.has_value());

    const qvec3d model1_fenceface{-16, -56, 168};
    const qvec3d model2_waterface{-16, -120, 168};

    EXPECT_EQ(2, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[1], model1_fenceface).size());
    EXPECT_EQ(2, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[2], model2_waterface).size());

    // both bmodels should be CONTENTS_SOLID in all hulls
    for (int model_idx = 1; model_idx <= 2; ++model_idx) {
        for (int hull = 0; hull <= 2; ++hull) {
            auto &model = bsp.dmodels[model_idx];

            SCOPED_TRACE(fmt::format("model: {} hull: {}", model_idx, hull));
            EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, {hull}, &model, (model.mins + model.maxs) / 2));
        }
    }
}

TEST(testmapsQ1, bmodelLiquid)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_bmodel_liquid.map", {"-bmodelcontents"});
    ASSERT_TRUE(prt.has_value());

    // nonsolid brushes don't show up in clipping hulls. so 6 for the box room in hull1, and 6 for hull2.
    ASSERT_EQ(12, bsp.dclipnodes.size());

    const auto inside_water = qvec3d{8, -120, 184};
    EXPECT_EQ(CONTENTS_WATER, BSP_FindContentsAtPoint(&bsp, {0}, &bsp.dmodels[1], inside_water));

    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, {1}, &bsp.dmodels[1], inside_water));
    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, {2}, &bsp.dmodels[1], inside_water));
}

TEST(testmapsQ1, liquidMirrorinsideOff)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_liquid_mirrorinside_off.map");
    ASSERT_TRUE(prt.has_value());

    // normally there would be 2 faces, but with _mirrorinside 0 we should get only the upwards-pointing one
    EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels.at(0), {-52, -56, 8}, {0, 0, 1}));
    EXPECT_FALSE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels.at(0), {-52, -56, 8}, {0, 0, -1}));
}

TEST(testmapsQ1, noclipfaces)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_noclipfaces.map");

    ASSERT_TRUE(prt.has_value());

    ASSERT_EQ(bsp.dfaces.size(), 2);

    // TODO: contents should be empty in hull0 because it's func_detail_illusionary

    for (auto &face : bsp.dfaces) {
        ASSERT_EQ(std::string("{trigger"), Face_TextureName(&bsp, &face));
    }

    EXPECT_EQ(prt->portals.size(), 0);
    EXPECT_EQ(prt->portalleafs, 1);
}

/**
 * _noclipfaces 1 detail_fence meeting a _noclipfaces 0 one.
 *
 * Currently, to simplify the implementation, we're treating that the same as if both had _noclipfaces 1
 */
TEST(testmapsQ1, noclipfacesJunction)
{
    const std::vector<std::string> maps{"qbsp_noclipfaces_junction.map", "q2_noclipfaces_junction.map"};

    for (const auto &map : maps) {
        const bool q2 = (map.find("q2") == 0);

        SCOPED_TRACE(map);

        const auto [bsp, bspx, prt] = q2 ? LoadTestmapQ2(map) : LoadTestmapQ1(map);

        EXPECT_EQ(bsp.dfaces.size(), 12);

        const qvec3d portal_pos{96, 56, 32};

        auto *pos_x = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], portal_pos, {1, 0, 0});
        auto *neg_x = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], portal_pos, {-1, 0, 0});

        ASSERT_NE(pos_x, nullptr);
        ASSERT_NE(neg_x, nullptr);

        if (q2) {
            EXPECT_EQ(std::string("e1u1/wndow1_2"), Face_TextureName(&bsp, pos_x));
            EXPECT_EQ(std::string("e1u1/window1"), Face_TextureName(&bsp, neg_x));
        } else {
            EXPECT_EQ(std::string("{trigger"), Face_TextureName(&bsp, pos_x));
            EXPECT_EQ(std::string("blood1"), Face_TextureName(&bsp, neg_x));
        }
    }
}

/**
 * Same as previous test, but the T shaped brush entity has _mirrorinside
 */
TEST(testmapsQ1, noclipfacesMirrorinside)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_noclipfaces_mirrorinside.map");

    ASSERT_TRUE(prt.has_value());

    ASSERT_EQ(bsp.dfaces.size(), 4);

    // TODO: contents should be empty in hull0 because it's func_detail_illusionary

    for (auto &face : bsp.dfaces) {
        ASSERT_EQ(std::string("{trigger"), Face_TextureName(&bsp, &face));
    }

    EXPECT_EQ(prt->portals.size(), 0);
    EXPECT_EQ(prt->portalleafs, 1);
}

TEST(testmapsQ1, detailIllusionaryIntersecting)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_detail_illusionary_intersecting.map", {"-tjunc", "rotate"});

    ASSERT_TRUE(prt.has_value());

    // sides: 3*4 = 12
    // top: 3 (4 with new tjunc code that prefers more faces over 0-area tris)
    // bottom: 3 (4 with new tjunc code that prefers more faces over 0-area tris)
    EXPECT_GE(bsp.dfaces.size(), 18);
    EXPECT_LE(bsp.dfaces.size(), 20);

    for (auto &face : bsp.dfaces) {
        EXPECT_EQ(std::string("{trigger"), Face_TextureName(&bsp, &face));
    }

    // top of cross
    EXPECT_EQ(1, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -50, 120), qvec3d(0, 0, 1)).size());

    // interior face that should be clipped away
    EXPECT_EQ(0, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -52, 116), qvec3d(0, -1, 0)).size());

    EXPECT_EQ(prt->portals.size(), 0);
    EXPECT_EQ(prt->portalleafs, 1);
}

TEST(testmapsQ1, detailIllusionaryNoclipfacesIntersecting)
{
    const auto [bsp, bspx, prt] =
        LoadTestmapQ1("qbsp_detail_illusionary_noclipfaces_intersecting.map", {"-tjunc", "rotate"});

    ASSERT_TRUE(prt.has_value());

    for (auto &face : bsp.dfaces) {
        EXPECT_EQ(std::string("{trigger"), Face_TextureName(&bsp, &face));
    }

    // top of cross has 2 faces Z-fighting, because we disabled clipping
    // (with qbsp3 method, there won't ever be z-fighting since we only ever generate 1 face per portal)
    size_t faces_at_top = BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -50, 120), qvec3d(0, 0, 1)).size();
    EXPECT_GE(faces_at_top, 1);
    EXPECT_LE(faces_at_top, 2);

    // interior face not clipped away
    EXPECT_EQ(1, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -52, 116), qvec3d(0, -1, 0)).size());

    EXPECT_EQ(prt->portals.size(), 0);
    EXPECT_EQ(prt->portalleafs, 1);
}

TEST(testmapsQ1, detailNonSealing)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_detail_non_sealing.map");

    EXPECT_FALSE(prt.has_value());
}

TEST(testmapsQ1, sealingContents)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_sealing_contents.map");

    EXPECT_TRUE(prt.has_value());
}

TEST(testmapsQ1, detailTouchingWater)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_detail_touching_water.map");

    EXPECT_TRUE(prt.has_value());
}

TEST(testmapsQ1, detailDoesntRemoveWorldNodes)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_detail_doesnt_remove_world_nodes.map");

    ASSERT_TRUE(prt.has_value());

    {
        // check for a face under the start pos
        const qvec3d floor_under_start{-56, -72, 64};
        auto *floor_under_start_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], floor_under_start, {0, 0, 1});
        EXPECT_NE(nullptr, floor_under_start_face);
    }

    {
        // floor face should be clipped away by detail
        const qvec3d floor_inside_detail{64, -72, 64};
        auto *floor_inside_detail_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], floor_inside_detail, {0, 0, 1});
        EXPECT_EQ(nullptr, floor_inside_detail_face);
    }

    // make sure the detail face exists
    EXPECT_NE(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {32, -72, 136}, {-1, 0, 0}));

    {
        // but the sturctural nodes/leafs should not be clipped away by detail
        const qvec3d covered_by_detail{48, -88, 128};
        auto *covered_by_detail_node = BSP_FindNodeAtPoint(&bsp, &bsp.dmodels[0], covered_by_detail, {-1, 0, 0});
        EXPECT_NE(nullptr, covered_by_detail_node);
    }
}

TEST(testmapsQ1, merge)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_merge.map");

    ASSERT_FALSE(prt.has_value());
    ASSERT_GE(bsp.dfaces.size(), 6);

    // BrushBSP does a split through the middle first to keep the BSP balanced, which prevents
    // two of the side face from being merged
    ASSERT_LE(bsp.dfaces.size(), 8);

    const auto exp_bounds = aabb3d{{48, 0, 96}, {224, 96, 96}};

    auto *top_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {48, 0, 96}, {0, 0, 1});
    const auto top_winding = Face_Winding(&bsp, top_face);

    EXPECT_EQ(top_winding.bounds().mins(), exp_bounds.mins());
    EXPECT_EQ(top_winding.bounds().maxs(), exp_bounds.maxs());
}

TEST(testmapsQ1, tjuncManySidedFace)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_tjunc_many_sided_face.map", {"-tjunc", "rotate"});

    ASSERT_TRUE(prt.has_value());

    std::map<qvec3d, std::vector<const mface_t *>> faces_by_normal;
    for (auto &face : bsp.dfaces) {
        faces_by_normal[Face_Normal(&bsp, &face)].push_back(&face);
    }

    ASSERT_EQ(6, faces_by_normal.size());

    const std::vector<const mface_t *> &floor_faces = faces_by_normal.at({0, 0, 1});

    // the floor has a 0.1 texture scale, so it gets subdivided into many small faces
    EXPECT_EQ(15 * 15, floor_faces.size());
    for (auto *face : floor_faces) {
        // these should all be <= 6 sided
        EXPECT_LE(face->numedges, 6);
    }

    // the ceiling gets split into 2 faces because fixing T-Junctions with all of the
    // wall sections exceeds the max vertices per face limit
    const std::vector<const mface_t *> &ceiling_faces = faces_by_normal.at({0, 0, -1});
    ASSERT_EQ(2, ceiling_faces.size());

    for (auto *face : ceiling_faces) {
        // these should all be <= 64 sided
        EXPECT_LE(face->numedges, 64);
    }

    // ceiling faces: one is 0 area (it's just repairing a bunch of tjuncs)
    auto ceiling_winding0 = Face_Winding(&bsp, ceiling_faces[0]);
    auto ceiling_winding1 = Face_Winding(&bsp, ceiling_faces[1]);

    float w0_area = ceiling_winding0.area();
    float w1_area = ceiling_winding1.area();

    if (w0_area > w1_area) {
        EXPECT_EQ(320 * 320, w0_area);
        EXPECT_EQ(0, w1_area);
    } else {
        EXPECT_EQ(0, w0_area);
        EXPECT_EQ(320 * 320, w1_area);
    }
}

TEST(testmapsQ1, tjuncManySidedFaceMaxedges0)
{
    // same as above, but -maxedges 0 allows the ceiling to be >64 sides so it can be just 1 face
    const auto [bsp, bspx, prt] =
        LoadTestmapQ1("qbsp_tjunc_many_sided_face.map", {"-tjunc", "rotate", "-maxedges", "0"});

    std::map<qvec3d, std::vector<const mface_t *>> faces_by_normal;
    for (auto &face : bsp.dfaces) {
        faces_by_normal[Face_Normal(&bsp, &face)].push_back(&face);
    }

    const std::vector<const mface_t *> &ceiling_faces = faces_by_normal.at({0, 0, -1});
    ASSERT_EQ(1, ceiling_faces.size());
    EXPECT_GT(ceiling_faces[0]->numedges, 64);
}

TEST(testmapsQ1, tjuncManySidedFaceSky)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_tjunc_many_sided_sky.map", {"-tjunc", "rotate"});

    for (auto &face : bsp.dfaces) {
        EXPECT_LE(face.numedges, 64);
    }
}

TEST(testmapsQ1, tjuncManySidedFaceSkyWithDefaultTjuncMode)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_tjunc_many_sided_sky.map", {});

    for (auto &face : bsp.dfaces) {
        EXPECT_LE(face.numedges, 64);
    }
}

TEST(testmapsQ1, manySidedFace)
{
    // FIXME: 360 sided cylinder is really slow to compile
    GTEST_SKIP();

    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_many_sided_face.map", {});

    for (auto &face : bsp.dfaces) {
        EXPECT_LE(face.numedges, 64);
    }
}

TEST(testmapsQ1, tjuncAngledFace)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_tjunc_angled_face.map");
    CheckFilled(bsp);

    auto faces = FacesWithTextureName(bsp, "bolt6");
    ASSERT_EQ(faces.size(), 1);

    auto *bolt6_face = faces.at(0);
    EXPECT_EQ(bolt6_face->numedges, 5);
}

/**
 * Because it comes second, the sbutt2 brush should "win" in clipping against the floor,
 * in both a worldspawn test case, as well as a func_wall.
 */
TEST(testmapsQ1, brushClippingOrder)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_brush_clipping_order.map", {"-tjunc", "rotate"});

    ASSERT_TRUE(prt.has_value());

    const qvec3d world_button{-8, -8, 16};
    const qvec3d func_wall_button{152, -8, 16};

    // 0 = world, 1 = func_wall
    ASSERT_EQ(2, bsp.dmodels.size());

    ASSERT_EQ(20, bsp.dfaces.size());

    ASSERT_EQ(10, bsp.dmodels[0].numfaces); // 5 faces for the sides + bottom, 5 faces for the top
    ASSERT_EQ(10, bsp.dmodels[1].numfaces); // (same on worldspawn and func_wall)

    auto *world_button_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], world_button, {0, 0, 1});
    ASSERT_NE(nullptr, world_button_face);
    ASSERT_EQ(std::string("sbutt2"), Face_TextureName(&bsp, world_button_face));

    auto *func_wall_button_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[1], func_wall_button, {0, 0, 1});
    ASSERT_NE(nullptr, func_wall_button_face);
    ASSERT_EQ(std::string("sbutt2"), Face_TextureName(&bsp, func_wall_button_face));
}

/**
 * Box room with a rotating fan (just a cube). Works in a mod with hiprotate - AD, Quoth, etc.
 */
TEST(testmapsQ1, origin)
{
    const std::vector<std::string> maps{
        "qbsp_origin.map",
        "qbsp_hiprotate.map" // same, but uses info_rotate instead of an origin brush
    };

    for (const auto &map : maps) {
        SCOPED_TRACE(map);

        const auto [bsp, bspx, prt] = LoadTestmapQ1(map);

        ASSERT_TRUE(prt.has_value());

        // 0 = world, 1 = rotate_object
        ASSERT_EQ(2, bsp.dmodels.size());

        // check that the origin brush didn't clip away any solid faces, or generate faces
        ASSERT_EQ(6, bsp.dmodels[1].numfaces);

        // FIXME: should the origin brush update the dmodel's origin too?
        ASSERT_EQ(qvec3f(0, 0, 0), bsp.dmodels[1].origin);

        // check that the origin brush updated the entity lump
        auto ents = EntData_Parse(bsp);
        auto it = std::find_if(ents.begin(), ents.end(),
            [](const entdict_t &dict) -> bool { return dict.get("classname") == "rotate_object"; });

        ASSERT_NE(it, ents.end());
        EXPECT_EQ(it->get("origin"), "216 -216 340");
    }
}

TEST(testmapsQ1, simple)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple.map");

    ASSERT_FALSE(prt.has_value());
}

/**
 * Just a solid cuboid
 */
TEST(testmapsQ1, cube)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_cube.map");

    ASSERT_FALSE(prt.has_value());

    const aabb3f cube_bounds{{32, -240, 80}, {80, -144, 112}};

    EXPECT_EQ(bsp.dedges.size(), 13); // index 0 is reserved, and the cube has 12 edges

    ASSERT_EQ(7, bsp.dleafs.size());

    // check the solid leaf
    auto &solid_leaf = bsp.dleafs[0];
    EXPECT_EQ(solid_leaf.mins, qvec3f(0, 0, 0));
    EXPECT_EQ(solid_leaf.maxs, qvec3f(0, 0, 0));

    // check the empty leafs
    for (int i = 1; i < 7; ++i) {
        SCOPED_TRACE(fmt::format("leaf {}", i));

        auto &leaf = bsp.dleafs[i];
        EXPECT_EQ(CONTENTS_EMPTY, leaf.contents);

        EXPECT_EQ(1, leaf.nummarksurfaces);
    }

    ASSERT_EQ(6, bsp.dfaces.size());

    // node bounds
    auto cube_bounds_grown = cube_bounds.grow(24);

    auto &headnode = bsp.dnodes[bsp.dmodels[0].headnode[0]];
    EXPECT_EQ(cube_bounds_grown.mins(), headnode.mins);
    EXPECT_EQ(cube_bounds_grown.maxs(), headnode.maxs);

    // model bounds are shrunk by 1 unit on each side for some reason
    EXPECT_EQ(cube_bounds.grow(-1).mins(), bsp.dmodels[0].mins);
    EXPECT_EQ(cube_bounds.grow(-1).maxs(), bsp.dmodels[0].maxs);

    EXPECT_EQ(6, bsp.dnodes.size());

    EXPECT_EQ(12, bsp.dclipnodes.size());
}

TEST(testmapsQ1, cubeCaseInsensitive)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_cube_case_insensitive.map");

    ASSERT_EQ(6, bsp.dfaces.size());
    for (const auto &dface : bsp.dfaces) {
        // the case from the .wad is used, not the case from the .map
        ASSERT_EQ(Face_TextureNameView(&bsp, &dface), "orangestuff8");
    }
}

/**
 * Two solid cuboids touching along one edge
 */
TEST(testmapsQ1, cubes)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_cubes.map");

    // 1 + 12 for cube A + 13 for cube B.
    // for the "four way" vertical edge, two of the faces can share an edge on cube A, but this blocks any further
    // sharing on that edge in cube B.
    EXPECT_EQ(bsp.dedges.size(), 26);
}

class ClipFuncWallTest : public testing::TestWithParam<std::string>
{
};

INSTANTIATE_TEST_SUITE_P(
    ClipFuncWallCases, ClipFuncWallTest, testing::Values("q1_clip_func_wall.map", "q1_clip_and_solid_func_wall.map"));

/**
 * Ensure submodels that are all "clip" get bounds set correctly
 */
TEST_P(ClipFuncWallTest, testBounds)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1(GetParam());

    ASSERT_TRUE(prt.has_value());

    const aabb3f cube_bounds{{64, 64, 48}, {128, 128, 80}};

    ASSERT_EQ(2, bsp.dmodels.size());

    // node bounds
    auto &headnode = bsp.dnodes[bsp.dmodels[1].headnode[0]];
    EXPECT_EQ(cube_bounds.grow(24).mins(), headnode.mins);
    EXPECT_EQ(cube_bounds.grow(24).maxs(), headnode.maxs);

    // model bounds are shrunk by 1 unit on each side for some reason
    EXPECT_EQ(cube_bounds.grow(-1).mins(), bsp.dmodels[1].mins);
    EXPECT_EQ(cube_bounds.grow(-1).maxs(), bsp.dmodels[1].maxs);
}

/**
 * Lots of features in one map, more for testing in game than automated testing
 */
TEST(testmapsQ1, features)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbspfeatures.map");

    ASSERT_TRUE(prt.has_value());

    EXPECT_EQ(bsp.loadversion, &bspver_q1);
}

TEST(testmapsQ1, detailWallTjuncs)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_detail_wall.map");

    ASSERT_TRUE(prt.has_value());
    EXPECT_EQ(bsp.loadversion, &bspver_q1);

    const auto behind_pillar = qvec3d(-160, -140, 120);
    auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], behind_pillar, qvec3d(1, 0, 0));
    ASSERT_TRUE(face);

    SCOPED_TRACE("func_detail_wall should not generate extra tjunctions on structural faces");
    auto w = Face_Winding(&bsp, face);
    EXPECT_EQ(w.size(), 5);
}

TEST(testmapsQ1, detailWallIntersectingDetail)
{
    GTEST_SKIP();

    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_detail_wall_intersecting_detail.map");

    const auto *left_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-152, -192, 160}, {1, 0, 0});
    const auto *under_detail_wall_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-152, -176, 160}, {1, 0, 0});
    const auto *right_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-152, -152, 160}, {1, 0, 0});

    EXPECT_NE(left_face, nullptr);
    EXPECT_NE(under_detail_wall_face, nullptr);
    EXPECT_NE(right_face, nullptr);

    EXPECT_EQ(left_face, under_detail_wall_face);
    EXPECT_EQ(left_face, right_face);
}

bool PortalMatcher(const prtfile_winding_t &a, const prtfile_winding_t &b)
{
    return a.undirectional_equal(b);
}

TEST(testmapsQ1, qbspFuncDetailVariousTypes)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_func_detail.map");

    ASSERT_TRUE(prt.has_value());
    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    EXPECT_EQ(1, bsp.dmodels.size());

    const qvec3d in_func_detail{56, -56, 120};
    const qvec3d in_func_detail_wall{56, -136, 120};
    const qvec3d in_func_detail_illusionary{56, -216, 120};
    const qvec3d in_func_detail_illusionary_mirrorinside{56, -296, 120};

    // const double floor_z = 96;

    // detail clips away world faces, others don't
    EXPECT_EQ(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], in_func_detail - qvec3d(0, 0, 24), {0, 0, 1}));
    EXPECT_NE(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], in_func_detail_wall - qvec3d(0, 0, 24), {0, 0, 1}));
    EXPECT_NE(
        nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], in_func_detail_illusionary - qvec3d(0, 0, 24), {0, 0, 1}));
    EXPECT_NE(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0],
                           in_func_detail_illusionary_mirrorinside - qvec3d(0, 0, 24), {0, 0, 1}));

    // check for correct contents
    auto *detail_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_func_detail);
    auto *detail_wall_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_func_detail_wall);
    auto *detail_illusionary_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_func_detail_illusionary);
    auto *detail_illusionary_mirrorinside_leaf =
        BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_func_detail_illusionary_mirrorinside);

    EXPECT_EQ(CONTENTS_SOLID, detail_leaf->contents);
    EXPECT_EQ(CONTENTS_SOLID, detail_wall_leaf->contents);
    EXPECT_EQ(CONTENTS_EMPTY, detail_illusionary_leaf->contents);
    EXPECT_EQ(CONTENTS_EMPTY, detail_illusionary_mirrorinside_leaf->contents);

    // portals

    ASSERT_EQ(2, prt->portals.size());

    const auto p0 = prtfile_winding_t{{-160, -8, 352}, {56, -8, 352}, {56, -8, 96}, {-160, -8, 96}};
    const auto p1 = p0.translate({232, 0, 0});

    EXPECT_TRUE(((PortalMatcher(prt->portals[0].winding, p0) && PortalMatcher(prt->portals[1].winding, p1)) ||
                 (PortalMatcher(prt->portals[0].winding, p1) && PortalMatcher(prt->portals[1].winding, p0))));

    EXPECT_EQ(prt->portalleafs, 3);
    EXPECT_GT(prt->portalleafs_real, 3);
}

TEST(testmapsQ1, detailFence)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_detail_fence.map");

    ASSERT_TRUE(prt.has_value());
    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    const auto in_detail_fence = qvec3d(120, -72, 104);
    auto extflags = LoadExtendedContentFlags(bsp.file, &bsp);

    EXPECT_EQ(bsp.dleafs.size(), extflags.size());

    const auto *detail_fence_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_detail_fence);
    int leafnum = BSP_GetLeafNum(&bsp, detail_fence_leaf);

    // check the extended contents
    EXPECT_EQ(detail_fence_leaf->contents, CONTENTS_SOLID);
    // due to FixupDetailFence, we move the marksurfaces out to a neighbour that will actually render them
    EXPECT_EQ(detail_fence_leaf->nummarksurfaces, 0);

    contentflags_t detail_fence_leaf_flags = extflags[leafnum];
    EXPECT_EQ(detail_fence_leaf_flags.flags,
        EWT_VISCONTENTS_WINDOW | EWT_CFLAG_DETAIL | EWT_CFLAG_TRANSLUCENT | EWT_CFLAG_MIRROR_INSIDE_SET);

    // grab a random face inside the detail_fence - we should find it inside the player start leaf's markfaces list
    const auto back_of_pillar_pos = qvec3d(176, -32, 120);
    auto *back_of_pillar_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], back_of_pillar_pos);

    // check the player start leaf
    const auto player_start_pos = qvec3d(-56, -96, 120);
    const auto *player_start_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_start_pos);
    EXPECT_EQ(player_start_leaf->contents, CONTENTS_EMPTY);
    auto markfaces = Leaf_Markfaces(&bsp, player_start_leaf);
    EXPECT_THAT(markfaces, testing::Contains(back_of_pillar_face));

    // check the cubby off to the side - it _shouldn't_ have got the back_of_pillar_face added to its marksurfaces
    // (make sure the flood fill in FixupDetailFence() isn't propagating them excessively)
    const auto cubby_pos = qvec3d(-176, -288, 96);
    const auto *cubby_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], cubby_pos);
    EXPECT_EQ(cubby_leaf->contents, CONTENTS_EMPTY);
    auto cubby_leaf_markfaces = Leaf_Markfaces(&bsp, cubby_leaf);
    EXPECT_THAT(cubby_leaf_markfaces, testing::Not(testing::Contains(back_of_pillar_face)));
}

TEST(testmapsQ1, detailFenceWithoutExtendedContents)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_detail_fence.map", {"-noextendedcontentflags"});

    const auto in_detail_fence = qvec3d(120, -72, 104);

    // the file doesn't exist, but we still get back an emulated version
    auto extflags = LoadExtendedContentFlags(bsp.file, &bsp);
    EXPECT_EQ(bsp.dleafs.size(), extflags.size());

    const auto *detail_fence_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_detail_fence);
    int leafnum = BSP_GetLeafNum(&bsp, detail_fence_leaf);

    // check the basic and extended contents
    EXPECT_EQ(detail_fence_leaf->contents, CONTENTS_SOLID);

    contentflags_t detail_fence_leaf_flags = extflags[leafnum];
    EXPECT_EQ(detail_fence_leaf_flags.flags, EWT_VISCONTENTS_SOLID);
}

TEST(testmapsQ1, angledBrush)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_angled_brush.map");

    ASSERT_TRUE(prt.has_value());
    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    EXPECT_EQ(1, bsp.dmodels.size());
    // tilted cuboid floating in a box room, so shared solid leaf + 6 empty leafs around the cube
    EXPECT_EQ(6 + 1, bsp.dleafs.size());
}

TEST(testmapsQ1, sealingPointEntityOnOutside)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_sealing_point_entity_on_outside.map");

    ASSERT_TRUE(prt.has_value());
}

TEST(testmapsQ1, sealingHull1Onnode)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_sealing_hull1_onnode.map");

    const auto player_start_pos = qvec3d(-192, 132, 56);

    SCOPED_TRACE("hull0 is empty at the player start");
    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], player_start_pos));

    SCOPED_TRACE("hull1/2 are empty just above the player start");
    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], player_start_pos + qvec3d(0, 0, 1)));
    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], player_start_pos + qvec3d(0, 0, 1)));

    SCOPED_TRACE("hull0/1/2 are solid in the void");
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], player_start_pos + qvec3d(0, 0, 1000)));
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], player_start_pos + qvec3d(0, 0, 1000)));
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], player_start_pos + qvec3d(0, 0, 1000)));
}

TEST(testmapsQ1, hullsFlag)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_hulls.map");

    ASSERT_EQ(3, bsp.dmodels.size()); // world and 2 func_wall's

    {
        const auto in_bmodel_pos = qvec3d(-152, -168, 168);

        // the func_wall has _hulls is set to 5 = 0b101, so generate hulls 0 and 2 (blocks shambler and line traces but
        // player can walk through)

        EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[1], in_bmodel_pos));
        EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[1], in_bmodel_pos));
        EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[1], in_bmodel_pos));

        EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[1], in_bmodel_pos + qvec3d(8, 0, 0)));
    }

    {
        // the second one has _hulls 6 = 0b110, so generate hulls 1 and 2 (blocks player + shambler, but no visual
        // faces and point-size hull traces can pass through)

        const auto in_bmodel_pos2 = qvec3d(-152, 24, 168);

        EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[2], in_bmodel_pos2));
        EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[2], in_bmodel_pos2));
        EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[2], in_bmodel_pos2));

        EXPECT_FALSE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[2], in_bmodel_pos2 + qvec3d(8, 0, 0)));
    }
}

TEST(testmapsQ1, 0125UnitFaces)
{
    GTEST_SKIP();

    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_0125unit_faces.map");

    EXPECT_EQ(bsp.loadversion, &bspver_q1);
    EXPECT_EQ(2, bsp.dfaces.size());
}

TEST(testmapsQ1, mountain)
{
    GTEST_SKIP();

    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_mountain.map");

    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);
    EXPECT_TRUE(prt);
    CheckFilled(bsp);
}

/**
 * Q1 sealing test:
 * - hull0 can use Q2 method (fill inside)
 * - hull1+ can't, because it would cause areas containing no entities but connected by a thin gap to the
 *   rest of the world to get sealed off as solid.
 **/
TEST(testmapsQ1, sealing)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_sealing.map");

    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    const qvec3d in_start_room{-192, 144, 104};
    const qvec3d in_emptyroom{-168, 544, 104};
    const qvec3d in_void{-16, -800, 56};
    const qvec3d connected_by_thin_gap{72, 136, 104};

    // check leaf contents in hull 0
    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_start_room)->contents);
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_emptyroom)
                                  ->contents); // can get sealed, since there are no entities
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_void)->contents);
    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], connected_by_thin_gap)->contents);

    // check leaf contents in hull 1
    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], in_start_room));
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], in_emptyroom));
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], in_void));
    // ideally this wouldn't get sealed, but we need to do the "inside filling" for compatibility with complex
    // maps using e.g. obj2map geometry, otherwise the clipnodes count explodes
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], connected_by_thin_gap));

    // check leaf contents in hull 2
    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], in_start_room));
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], in_emptyroom));
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], in_void));
    EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], connected_by_thin_gap));

    EXPECT_EQ(prt->portals.size(), 2);
    EXPECT_EQ(prt->portalleafs, 3); // 2 connected rooms + gap (other room is filled in with solid)
    EXPECT_EQ(prt->portalleafs_real, 3); // no detail, so same as above
}

TEST(testmapsQ1, csg)
{
    auto *game = bspver_q1.game;

    auto &entity = LoadMapPath("q1_csg.map");

    ASSERT_EQ(entity.mapbrushes.size(), 2);

    bspbrush_t::container bspbrushes;
    for (int i = 0; i < 2; ++i) {
        auto b =
            LoadBrush(entity, entity.mapbrushes[i], game->create_contents_from_native(CONTENTS_SOLID), 0, std::nullopt);

        EXPECT_EQ(6, b->sides.size());

        bspbrushes.push_back(bspbrush_t::make_ptr(std::move(*b)));
    }

    auto csged = CSGFaces(bspbrushes);
    EXPECT_EQ(2, csged.size());

    for (int i = 0; i < 2; ++i) {
        EXPECT_EQ(5, csged[i]->sides.size());
    }
}

/**
 * Test for WAD internal textures
 **/
TEST(testmapsQ1, wadInternal)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple.map");

    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    EXPECT_EQ(bsp.dtex.textures.size(), 4);
    // skip is only here because of the water
    EXPECT_EQ(bsp.dtex.textures[0].name, "skip");

    EXPECT_EQ(bsp.dtex.textures[1].name, "orangestuff8");
    EXPECT_EQ(bsp.dtex.textures[2].name, "*zwater1");
    EXPECT_EQ(bsp.dtex.textures[3].name, "brown_brick");

    EXPECT_FALSE(bsp.dtex.textures[1].data.empty());
    EXPECT_FALSE(bsp.dtex.textures[2].data.empty());
    EXPECT_FALSE(bsp.dtex.textures[3].data.empty());

    EXPECT_TRUE(img::load_mip("orangestuff8", bsp.dtex.textures[1].data, false, bsp.loadversion->game));
    EXPECT_TRUE(img::load_mip("*zwater1", bsp.dtex.textures[2].data, false, bsp.loadversion->game));
    EXPECT_TRUE(img::load_mip("brown_brick", bsp.dtex.textures[3].data, false, bsp.loadversion->game));
}

/**
 * Test for WAD internal textures
 **/
TEST(testmapsQ1, wadExternal)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple.map", {"-xwadpath", std::string(testmaps_dir)});

    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    EXPECT_EQ(bsp.dtex.textures.size(), 4);
    // skip is only here because of the water
    EXPECT_EQ(bsp.dtex.textures[0].name, "skip");

    EXPECT_EQ(bsp.dtex.textures[1].name, "orangestuff8");
    EXPECT_EQ(bsp.dtex.textures[2].name, "*zwater1");
    EXPECT_EQ(bsp.dtex.textures[3].name, "brown_brick");

    EXPECT_EQ(bsp.dtex.textures[1].data.size(), sizeof(dmiptex_t));
    EXPECT_EQ(bsp.dtex.textures[2].data.size(), sizeof(dmiptex_t));
    EXPECT_EQ(bsp.dtex.textures[3].data.size(), sizeof(dmiptex_t));
}

TEST(testmapsQ1, looseTextures)
{
    SCOPED_TRACE("loose textures are only loaded when -notex is in use");

    auto q1_loose_textures_path = std::filesystem::path(testmaps_dir) / "q1_loose_textures";

    const auto [bsp, bspx, prt] =
        LoadTestmapQ1("q1_loose_textures.map", {"-path", q1_loose_textures_path.string(), "-notex"});

    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    // FIXME: we shouldn't really write out skip
    const miptex_t &skip = bsp.dtex.textures[0];
    EXPECT_EQ(skip.name, "");
    EXPECT_TRUE(skip.null_texture);
    EXPECT_EQ(skip.width, 0);
    EXPECT_EQ(skip.height, 0);
    EXPECT_EQ(skip.data.size(), 0);

    const miptex_t &floor_purple_c = bsp.dtex.textures[1];
    EXPECT_EQ(floor_purple_c.name, "floor_purple_c");
    EXPECT_FALSE(floor_purple_c.null_texture);
    EXPECT_EQ(floor_purple_c.width, 64);
    EXPECT_EQ(floor_purple_c.height, 64);
    EXPECT_EQ(floor_purple_c.data.size(), sizeof(dmiptex_t));
    EXPECT_THAT(floor_purple_c.offsets, testing::ElementsAre(0, 0, 0, 0));

    const miptex_t &wall_tan_a = bsp.dtex.textures[2];
    EXPECT_EQ(wall_tan_a.name, "wall_tan_a");
    EXPECT_FALSE(wall_tan_a.null_texture);
    EXPECT_EQ(wall_tan_a.width, 64);
    EXPECT_EQ(wall_tan_a.height, 64);
    EXPECT_EQ(wall_tan_a.data.size(), sizeof(dmiptex_t));
    EXPECT_THAT(wall_tan_a.offsets, testing::ElementsAre(0, 0, 0, 0));
}

TEST(testmapsQ1, looseTexturesIgnored)
{
    SCOPED_TRACE("q1 should only load textures from .wad's. loose textures should not be included.");

    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_loose_textures_ignored/q1_loose_textures_ignored.map");

    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    ASSERT_EQ(bsp.dtex.textures.size(), 4);

    // FIXME: we shouldn't really write out skip
    const miptex_t &skip = bsp.dtex.textures[0];
    EXPECT_EQ(skip.name, "skip");
    EXPECT_FALSE(skip.null_texture);
    EXPECT_EQ(skip.width, 64);
    EXPECT_EQ(skip.height, 64);
    EXPECT_GT(skip.data.size(), sizeof(dmiptex_t));

    // the .map directory contains a "orangestuff8.png" which is 16x16.
    // make sure it's not picked up (https://github.com/ericwa/ericw-tools/issues/404).
    const miptex_t &orangestuff8 = bsp.dtex.textures[1];
    EXPECT_EQ(orangestuff8.name, "orangestuff8");
    EXPECT_FALSE(orangestuff8.null_texture);
    EXPECT_EQ(orangestuff8.width, 64);
    EXPECT_EQ(orangestuff8.height, 64);
    EXPECT_GT(orangestuff8.data.size(), sizeof(dmiptex_t));

    const miptex_t &zwater1 = bsp.dtex.textures[2];
    EXPECT_EQ(zwater1.name, "*zwater1");
    EXPECT_FALSE(zwater1.null_texture);
    EXPECT_EQ(zwater1.width, 64);
    EXPECT_EQ(zwater1.height, 64);
    EXPECT_GT(zwater1.data.size(), sizeof(dmiptex_t));

    const miptex_t &brown_brick = bsp.dtex.textures[3];
    EXPECT_EQ(brown_brick.name, "brown_brick");
    EXPECT_FALSE(brown_brick.null_texture);
    EXPECT_EQ(brown_brick.width, 128);
    EXPECT_EQ(brown_brick.height, 128);
    EXPECT_GT(brown_brick.data.size(), sizeof(dmiptex_t));
}

/**
 * Test that we automatically try to load X.wad when compiling X.map
 **/
TEST(testmapsQ1, wadMapname)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_wad_mapname.map");

    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    EXPECT_EQ(bsp.dtex.textures.size(), 2);
    EXPECT_EQ(bsp.dtex.textures[0].name, ""); // skip
    EXPECT_EQ(bsp.dtex.textures[0].data.size(), 0); // no texture data
    EXPECT_TRUE(bsp.dtex.textures[0].null_texture); // no texture data

    EXPECT_EQ(bsp.dtex.textures[1].name, "{trigger");
    EXPECT_GT(bsp.dtex.textures[1].data.size(), sizeof(dmiptex_t));
}

TEST(testmapsQ1, mergeMaps)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_merge_maps_base.map", {"-add", "q1_merge_maps_addition.map"});

    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    // check brushwork from the two maps is merged
    ASSERT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {5, 0, 16}, {0, 0, 1}));
    ASSERT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-5, 0, 16}, {0, 0, 1}));

    // check that the worldspawn keys from the base map are used
    auto ents = EntData_Parse(bsp);
    ASSERT_EQ(ents.size(), 3); // worldspawn, info_player_start, func_wall

    ASSERT_EQ(ents[0].get("classname"), "worldspawn");
    EXPECT_EQ(ents[0].get("message"), "merge maps base");

    // check info_player_start
    auto it = std::find_if(ents.begin(), ents.end(),
        [](const entdict_t &dict) -> bool { return dict.get("classname") == "info_player_start"; });
    ASSERT_NE(it, ents.end());

    // check func_wall entity from addition map is included
    it = std::find_if(
        ents.begin(), ents.end(), [](const entdict_t &dict) -> bool { return dict.get("classname") == "func_wall"; });
    ASSERT_NE(it, ents.end());
}

/**
 * Tests that hollow obj2map style geometry (tetrahedrons) get filled in, in all hulls.
 */
TEST(testmapsQ1, rocks)
{
    GTEST_SKIP();

    constexpr auto *q1_rocks_structural_cube = "q1_rocks_structural_cube.map";

    const auto mapnames = {
        "q1_rocks.map", // box room with a func_detail "mountain" of tetrahedrons with a hollow inside
        "q1_rocks_merged.map", // same as above but the mountain has been merged in the .map file into 1 brush
        "q1_rocks_structural.map", // same as q1_rocks.map but without the use of func_detail
        "q1_rocks_structural_merged.map",
        q1_rocks_structural_cube // simpler version where the mountain is just a cube
    };
    for (auto *mapname : mapnames) {
        SCOPED_TRACE(mapname);

        const auto [bsp, bspx, prt] = LoadTestmapQ1(mapname);

        EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

        const qvec3d point{48, 320, 88};

        EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], point));
        EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], point));
        EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], point));

        for (int i = 1; i <= 2; ++i) {
            SCOPED_TRACE(fmt::format("hull {}", i));

            const auto clipnodes = CountClipnodeLeafsByContentType(bsp, i);

            ASSERT_EQ(clipnodes.size(), 2);
            ASSERT_NE(clipnodes.find(CONTENTS_SOLID), clipnodes.end());
            ASSERT_NE(clipnodes.find(CONTENTS_EMPTY), clipnodes.end());

            // 6 for the walls of the box, and 1 for the rock structure, which is convex
            EXPECT_EQ(clipnodes.at(CONTENTS_SOLID), 7);

            if (std::string(q1_rocks_structural_cube) == mapname) {
                EXPECT_EQ((5 + 6), CountClipnodeNodes(bsp, i));
            }
        }

        // for completion's sake, check the nodes
        if (std::string(q1_rocks_structural_cube) == mapname) {
            EXPECT_EQ((5 + 6), bsp.dnodes.size());
        }
    }
}

static void CountClipnodeLeafsByContentType_r(const mbsp_t &bsp, int clipnode, std::map<int, int> &result)
{
    if (clipnode < 0) {
        // we're in a leaf node and `clipnode` is actually the content type
        ++result[clipnode];
        return;
    }

    auto &node = bsp.dclipnodes.at(clipnode);
    CountClipnodeLeafsByContentType_r(bsp, node.children[0], result);
    CountClipnodeLeafsByContentType_r(bsp, node.children[1], result);
}

std::map<int, int> CountClipnodeLeafsByContentType(const mbsp_t &bsp, int hullnum)
{
    Q_assert(hullnum > 0);

    int headnode = bsp.dmodels.at(0).headnode.at(hullnum);
    std::map<int, int> result;
    CountClipnodeLeafsByContentType_r(bsp, headnode, result);

    return result;
}

static int CountClipnodeNodes_r(const mbsp_t &bsp, int clipnode)
{
    if (clipnode < 0) {
        // we're in a leaf node and `clipnode` is actually the content type
        return 0;
    }

    auto &node = bsp.dclipnodes.at(clipnode);
    return 1 + CountClipnodeNodes_r(bsp, node.children[0]) + CountClipnodeNodes_r(bsp, node.children[1]);
}

/**
 * Count the non-leaf clipnodes of the worldmodel for the given hull's decision tree.
 */
int CountClipnodeNodes(const mbsp_t &bsp, int hullnum)
{
    Q_assert(hullnum > 0);

    int headnode = bsp.dmodels.at(0).headnode.at(hullnum);
    return CountClipnodeNodes_r(bsp, headnode);
}

TEST(testmapsQ1, hullExpansionBasic)
{
    // this has a func_wall with a triangular prism (5 sides):
    //
    //  ^
    //  |    ^-------\   this end is sheared upwards a bit
    // +Z   /_\_______\
    //  |
    //  ---- +Y -------->
    //
    // The way the BRUSHLIST bspx lump makes the AABB of the brush implicit
    // makes it hard to come up with examples for testing that the "cap" planes
    // are being inserted.
    //
    // this one is completely broken if you try to walk on the top edge of the prism,
    // and the cap planes are disabled (e.g. return at the start of AddBrushBevels)

    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_hull_expansion.map", {"-wrbrushes"});

    const bspxbrushes lump = deserialize<bspxbrushes>(bspx.at("BRUSHLIST"));
    ASSERT_EQ(lump.models.size(), 2); // world + 1x func_wall

    const auto &funcwall = lump.models.at(1);
    ASSERT_EQ(funcwall.brushes.size(), 1);

    const auto &prism = funcwall.brushes.at(0);
    ASSERT_GE(prism.faces.size(), 3); // 2 non-axial faces, the sloped sides, plus the cap

    const qplane3d prism_top_cap_plane =
        qplane3d::from_points({qvec3d(-49.25, -64, 29.5), qvec3d(-62.75, -64, 29.5), qvec3d(-56, 800, 83.5)});

    // conver to qplane3d's
    std::vector<qplane3d> prism_planes;
    for (auto &prism_face : prism.faces) {
        prism_planes.push_back(qplane3d(prism_face.normal, prism_face.dist));
    }

    // check for presence to top cap
    using namespace testing;
    EXPECT_THAT(prism_planes,
        Contains(Truly([=](const qplane3d &inp) -> bool { return qv::epsilonEqual(prism_top_cap_plane, inp); })));
}

/**
 * Tests a bad hull expansion
 */
TEST(testmapsQ1, hullExpansionLip)
{
    GTEST_SKIP();

    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_hull_expansion_lip.map");

    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    const qvec3d point{174, 308, 42};
    EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], point));

    for (int i = 1; i <= 2; ++i) {
        SCOPED_TRACE(fmt::format("hull {}", i));

        const auto clipnodes = CountClipnodeLeafsByContentType(bsp, i);

        ASSERT_EQ(clipnodes.size(), 2);
        ASSERT_NE(clipnodes.find(CONTENTS_SOLID), clipnodes.end());
        ASSERT_NE(clipnodes.find(CONTENTS_EMPTY), clipnodes.end());

        // room shaped like:
        //
        // |\    /|
        // | \__/ |
        // |______|
        //
        // 6 solid leafs for the walls/floor, 3 for the empty regions inside
        EXPECT_EQ(clipnodes.at(CONTENTS_SOLID), 6);
        EXPECT_EQ(clipnodes.at(CONTENTS_EMPTY), 3);

        // 6 walls + 2 floors
        EXPECT_EQ(CountClipnodeNodes(bsp, i), 8);
    }
}

TEST(testmapsQ1, hull1ContentTypes)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_hull1_content_types.map");

    EXPECT_EQ(GAME_QUAKE, bsp.loadversion->game->id);

    enum leaf
    {
        shared_leaf_0,
        new_leaf
    };

    struct expected_types_t
    {
        int hull0_contenttype;
        leaf hull0_leaf;

        int hull1_contenttype;
    };

    const std::vector<std::tuple<qvec3d, expected_types_t>> expected{
        // box center,   hull0 contents,  hull0 leaf,    hull1 contents
        {{0, 0, 0}, {CONTENTS_SOLID, shared_leaf_0, CONTENTS_SOLID}},
        {{64, 0, 0}, {CONTENTS_WATER, new_leaf, CONTENTS_EMPTY}}, // liquids are absent in hull1
        {{128, 0, 0}, {CONTENTS_SLIME, new_leaf, CONTENTS_EMPTY}},
        {{192, 0, 0}, {CONTENTS_LAVA, new_leaf, CONTENTS_EMPTY}},
        {{256, 0, 0}, {CONTENTS_SKY, new_leaf, CONTENTS_SOLID}}, // sky is solid in hull1
        {{320, 0, 0}, {CONTENTS_SOLID, shared_leaf_0, CONTENTS_SOLID}}, // func_detail is solid in hull1
        {{384, 0, 0}, {CONTENTS_SOLID, new_leaf, CONTENTS_SOLID}}, // func_detail_fence is solid in hull1. uses a new
        // leaf in hull0 because it can be seen through
        {{384, -64, 0},
            {CONTENTS_SOLID, new_leaf, CONTENTS_SOLID}}, // func_detail_fence + _mirrorinside is solid in hull1
        {{448, 0, 0}, {CONTENTS_EMPTY, new_leaf, CONTENTS_EMPTY}}, // func_detail_illusionary is empty in hull1
        {{448, -64, 0},
            {CONTENTS_EMPTY, new_leaf, CONTENTS_EMPTY}}, // func_detail_illusionary + _mirrorinside is empty in hull1
        {{512, 0, 0}, {CONTENTS_SOLID, shared_leaf_0, CONTENTS_SOLID}}, // func_detail_wall is solid in hull1
        {{576, 0, 0}, {CONTENTS_EMPTY, new_leaf, CONTENTS_SOLID}}, // clip is empty in hull0, solid in hull1
    };

    for (const auto &[point, expected_types] : expected) {
        std::string message = qv::to_string(point);
        SCOPED_TRACE(message);

        // hull 0
        auto *hull0_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], point);

        EXPECT_EQ(expected_types.hull0_contenttype, hull0_leaf->contents);
        ptrdiff_t hull0_leaf_index = hull0_leaf - &bsp.dleafs[0];

        if (expected_types.hull0_leaf == shared_leaf_0) {
            EXPECT_EQ(hull0_leaf_index, 0);
        } else {
            EXPECT_NE(hull0_leaf_index, 0);
        }

        // hull 1
        EXPECT_EQ(expected_types.hull1_contenttype, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], point));
    }
}

TEST(qbsp, BrushFromBounds)
{
    map.reset();
    qbsp_options.reset();
    qbsp_options.worldextent.set_value(1024, settings::source::COMMANDLINE);

    auto brush = BrushFromBounds({{2, 2, 2}, {32, 32, 32}});

    EXPECT_EQ(brush->sides.size(), 6);

    const auto top_winding = winding_t{{2, 2, 32}, {2, 32, 32}, {32, 32, 32}, {32, 2, 32}};
    const auto bottom_winding = winding_t{{32, 2, 2}, {32, 32, 2}, {2, 32, 2}, {2, 2, 2}};

    int found = 0;

    for (auto &side : brush->sides) {
        EXPECT_TRUE(side.w);

        if (side.w.directional_equal(top_winding)) {
            found++;
            auto &plane = side.get_plane();
            EXPECT_EQ(plane.get_normal(), qvec3d(0, 0, 1));
            EXPECT_EQ(plane.get_dist(), 32);
        }

        if (side.w.directional_equal(bottom_winding)) {
            found++;
            auto plane = side.get_plane();
            EXPECT_EQ(plane.get_normal(), qvec3d(0, 0, -1));
            EXPECT_EQ(plane.get_dist(), -2);
        }
    }
    EXPECT_EQ(found, 2);
}

// FIXME: failing because water tjuncs with walls
TEST(qbspQ1, waterSubdivisionWithLitWaterOff)
{
    GTEST_SKIP();

    SCOPED_TRACE("-litwater 0 should suppress water subdivision");

    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_water_subdivision.map", {"-litwater", "0"});

    auto faces = FacesWithTextureName(bsp, "*swater5");
    EXPECT_EQ(2, faces.size());

    for (auto *face : faces) {
        auto *texinfo = BSP_GetTexinfo(&bsp, face->texinfo);
        EXPECT_EQ(texinfo->flags.native_q1, TEX_SPECIAL);
    }
}

TEST(qbspQ1, waterSubdivisionWithDefaults)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_water_subdivision.map");

    auto faces = FacesWithTextureName(bsp, "*swater5");
    EXPECT_GT(faces.size(), 2);

    for (auto *face : faces) {
        auto *texinfo = BSP_GetTexinfo(&bsp, face->texinfo);
        EXPECT_EQ(texinfo->flags.native_q1, 0);
    }
}

TEST(qbspQ1, texturesSearchRelativeToCurrentDirectory)
{
    // QuArK runs the compilers like this:
    //
    // working directory: "c:\quake\tmpquark"
    // command line:      "maps\something.map"
    // worldspawn key:    "wad" "gfx/quark.wad"
    // wad located in:    "c:\quake\tmpquark\gfx\quark.wad"

    auto target_gfx_dir = fs::current_path() / "gfx";

    fs::create_directory(target_gfx_dir);

    try {
        fs::copy(std::filesystem::path(testmaps_dir) / "deprecated" / "free_wad.wad", target_gfx_dir);
    } catch (const fs::filesystem_error &e) {
        logging::print("{}\n", e.what());
    }

    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_cwd_relative_wad.map");
    ASSERT_EQ(2, bsp.dtex.textures.size());
    // FIXME: we shouldn't really be writing skip
    EXPECT_EQ("", bsp.dtex.textures[0].name);

    // make sure the texture was written
    EXPECT_EQ("orangestuff8", bsp.dtex.textures[1].name);
    EXPECT_EQ(64, bsp.dtex.textures[1].width);
    EXPECT_EQ(64, bsp.dtex.textures[1].height);
    EXPECT_GT(bsp.dtex.textures[1].data.size(), 0);
}

// specifically designed to break the old isHexen2()
// (has 0 faces, and model lump size is divisible by both Q1 and H2 model struct size)
TEST(qbspQ1, skipOnly)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_skip_only.map");

    EXPECT_EQ(bsp.loadversion, &bspver_q1);
    EXPECT_EQ(0, bsp.dfaces.size());
}

// specifically designed to break the old isHexen2()
// (has 0 faces, and model lump size is divisible by both Q1 and H2 model struct size)
TEST(qbspH2, skipOnly)
{
    const auto [bsp, bspx, prt] = LoadTestmap("h2_skip_only.map", {"-hexen2"});

    EXPECT_EQ(bsp.loadversion, &bspver_h2);
    EXPECT_EQ(0, bsp.dfaces.size());
}

TEST(qbspQ1, hull1Fail)
{
    GTEST_SKIP();

    SCOPED_TRACE("weird example of a phantom clip brush in hull1");
    const auto [bsp, bspx, prt] = LoadTestmap("q1_hull1_fail.map");

    {
        SCOPED_TRACE("contents at info_player_start");
        EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], qvec3d{-2256, -64, 264}));
    }
    {
        SCOPED_TRACE("contents at air_bubbles");
        EXPECT_EQ(CONTENTS_EMPTY, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], qvec3d{-2164, 126, 260}));
    }
    {
        SCOPED_TRACE("contents in void");
        EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], qvec3d{0, 0, 0}));
        EXPECT_EQ(CONTENTS_SOLID, BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], qvec3d{0, 0, 0}));
    }
}

TEST(qbspQ1, skyWindow)
{
    SCOPED_TRACE("faces partially covered by sky were getting wrongly merged and deleted");
    const auto [bsp, bspx, prt] = LoadTestmap("q1_sky_window.map");

    {
        SCOPED_TRACE("faces around window");
        EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-184, -252, -32))); // bottom
        EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-184, -252, 160))); // top
        EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-184, -288, 60))); // left
        EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-184, -224, 60))); // right
    }
}

TEST(qbspQ1, liquidSoftware)
{
    SCOPED_TRACE("map with just 1 liquid brush + a 'skip' platform, has render corruption on tyrquake");
    const auto [bsp, bspx, prt] = LoadTestmap("q1_liquid_software.map");

    const qvec3d top_face_point{-56, -56, 8};
    const qvec3d side_face_point{-56, -72, -8};

    auto *top = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], top_face_point, {0, 0, 1});
    auto *top_inwater = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], top_face_point, {0, 0, -1});

    auto *side = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], side_face_point, {0, -1, 0});
    auto *side_inwater = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], side_face_point, {0, 1, 0});

    ASSERT_TRUE(top);
    ASSERT_TRUE(top_inwater);
    ASSERT_TRUE(side);
    ASSERT_TRUE(side_inwater);

    // gather edge set used in and out of water.
    // recall that if edge 5 is from vert 12 to vert 13,
    // edge -5 is from vert 13 to vert 12.

    // for this test, we are converting directed to undirected
    // because we want to make sure there's no reuse across in-water and
    // out-of-water, which breaks software renderers.
    std::set<int> outwater_undirected_edges;
    std::set<int> inwater_undirected_edges;

    auto add_face_edges_to_set = [](const mbsp_t &b, const mface_t &face, std::set<int> &set) {
        for (int i = face.firstedge; i < (face.firstedge + face.numedges); ++i) {
            int edge = b.dsurfedges.at(i);

            // convert directed to undirected
            if (edge < 0) {
                edge = -edge;
            }

            set.insert(edge);
        }
    };

    add_face_edges_to_set(bsp, *top, outwater_undirected_edges);
    add_face_edges_to_set(bsp, *side, outwater_undirected_edges);

    add_face_edges_to_set(bsp, *top_inwater, inwater_undirected_edges);
    add_face_edges_to_set(bsp, *side_inwater, inwater_undirected_edges);

    EXPECT_EQ(7, outwater_undirected_edges.size());
    EXPECT_EQ(7, inwater_undirected_edges.size());

    // make sure there's no reuse between out-of-water and in-water
    for (int e : outwater_undirected_edges) {
        EXPECT_EQ(inwater_undirected_edges.find(e), inwater_undirected_edges.end());
    }
}

TEST(qbspQ1, edgeSharingSoftware)
{
    SCOPED_TRACE(
        "the software renderer only allows a given edge to be reused at most once, as the backwards version (negative index)");
    const auto [bsp, bspx, prt] = LoadTestmap("q1_edge_sharing_software.map");

    std::map<int, std::vector<const mface_t *>> signed_edge_faces;
    for (auto &face : bsp.dfaces) {
        for (int i = face.firstedge; i < (face.firstedge + face.numedges); ++i) {
            // may be negative
            const int edge = bsp.dsurfedges.at(i);

            signed_edge_faces[edge].push_back(&face);
        }
    }

    for (auto &[edge, faces] : signed_edge_faces) {
        EXPECT_EQ(1, faces.size());
    }
}

TEST(qbspQ1, missingTexture)
{
    const auto [bsp, bspx, prt] = LoadTestmap("q1_missing_texture.map");

    ASSERT_EQ(2, bsp.dtex.textures.size());

    // FIXME: we shouldn't really be writing skip
    // (our test data includes an actual "skip" texture,
    // so that gets included in the bsp.)
    EXPECT_EQ("skip", bsp.dtex.textures[0].name);
    EXPECT_FALSE(bsp.dtex.textures[0].null_texture);
    EXPECT_EQ(64, bsp.dtex.textures[0].width);
    EXPECT_EQ(64, bsp.dtex.textures[0].height);

    EXPECT_EQ("", bsp.dtex.textures[1].name);
    EXPECT_TRUE(bsp.dtex.textures[1].null_texture);

    EXPECT_EQ(6, bsp.dfaces.size());
}

TEST(qbspQ1, missingTextureAndMissingTexturesAsZeroSize)
{
    const auto [bsp, bspx, prt] = LoadTestmap("q1_missing_texture.map", {"-missing_textures_as_zero_size"});

    ASSERT_EQ(2, bsp.dtex.textures.size());

    // FIXME: we shouldn't really be writing skip
    // (our test data includes an actual "skip" texture,
    // so that gets included in the bsp.)
    EXPECT_EQ("skip", bsp.dtex.textures[0].name);
    EXPECT_FALSE(bsp.dtex.textures[0].null_texture);
    EXPECT_EQ(64, bsp.dtex.textures[0].width);
    EXPECT_EQ(64, bsp.dtex.textures[0].height);

    EXPECT_EQ("somemissingtext", bsp.dtex.textures[1].name);
    EXPECT_FALSE(bsp.dtex.textures[1].null_texture);
    EXPECT_EQ(0, bsp.dtex.textures[1].width);
    EXPECT_EQ(0, bsp.dtex.textures[1].height);

    EXPECT_EQ(6, bsp.dfaces.size());
}

TEST(qbspQ1, notex)
{
    const auto [bsp, bspx, prt] = LoadTestmap("q1_cube.map", {"-notex"});

    ASSERT_EQ(2, bsp.dtex.textures.size());

    {
        // FIXME: we shouldn't really be writing skip
        // (our test data includes an actual "skip" texture,
        // so that gets included in the bsp.)
        auto &t0 = bsp.dtex.textures[0];
        EXPECT_EQ("skip", t0.name);
        EXPECT_FALSE(t0.null_texture);
        EXPECT_EQ(64, t0.width);
        EXPECT_EQ(64, t0.height);
        EXPECT_EQ(t0.data.size(), sizeof(dmiptex_t));
        for (int i = 0; i < 4; ++i)
            EXPECT_EQ(t0.offsets[i], 0);
    }

    {
        auto &t1 = bsp.dtex.textures[1];
        EXPECT_EQ("orangestuff8", t1.name);
        EXPECT_FALSE(t1.null_texture);
        EXPECT_EQ(64, t1.width);
        EXPECT_EQ(64, t1.height);
        EXPECT_EQ(t1.data.size(), sizeof(dmiptex_t));
        for (int i = 0; i < 4; ++i)
            EXPECT_EQ(t1.offsets[i], 0);
    }
}

TEST(qbspHL, basic)
{
    const auto [bsp, bspx, prt] = LoadTestmap("hl_basic.map", {"-hlbsp"});
    EXPECT_TRUE(prt);

    ASSERT_EQ(2, bsp.dtex.textures.size());

    // FIXME: we shouldn't really be writing skip
    EXPECT_TRUE(bsp.dtex.textures[0].null_texture);

    EXPECT_EQ("hltest", bsp.dtex.textures[1].name);
    EXPECT_FALSE(bsp.dtex.textures[1].null_texture);
    EXPECT_EQ(64, bsp.dtex.textures[1].width);
    EXPECT_EQ(64, bsp.dtex.textures[1].height);
}

TEST(qbspHL, liquids)
{
    const auto [bsp, bspx, prt] = LoadTestmap("hl_liquids.map", {"-hlbsp", "-notex"});
    EXPECT_TRUE(prt);

    const qvec3f liquid_top_face_pos{104, -424, 64};
    const qvec3f liquid_interior_pos{104, -424, 40};

    const auto *top_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], liquid_top_face_pos, {0, 0, 1});
    ASSERT_TRUE(top_face);

    EXPECT_EQ(Face_TextureNameView(&bsp, top_face), "!liquidtest");

    EXPECT_EQ(CONTENTS_WATER, BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], liquid_interior_pos));
}

TEST(qbspHL, currents)
{
    const auto [bsp, bspx, prt] = LoadTestmap("hl_currents.map", {"-hlbsp"});
    EXPECT_TRUE(prt);

    // check the contents at a few points
    EXPECT_EQ(HL_CONTENTS_CURRENT_90, BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], {200, -200, -8}));
    EXPECT_EQ(HL_CONTENTS_CURRENT_0, BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], {376, -56, -8}));

    // we're not generating faces between different currents, unlike the vanilla compiler
    // (we could, but it'd be more work)
    const auto *cur90_cur0_transition = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {208, -64, -8});
    ASSERT_FALSE(cur90_cur0_transition);
}

TEST(qbspQ1, wrbrushesAndMiscExternalMap)
{
    const auto [bsp, bspx, prt] = LoadTestmap("q1_external_map_base.map", {"-wrbrushes"});

    bspxbrushes lump = deserialize<bspxbrushes>(bspx.at("BRUSHLIST"));

    ASSERT_EQ(lump.models.size(), 1);

    auto &model = lump.models.at(0);
    ASSERT_EQ(model.brushes.size(), 1);

    auto &brush = model.brushes.at(0);
    ASSERT_EQ(brush.bounds.maxs(), qvec3f(64, 64, 16));
    ASSERT_EQ(brush.bounds.mins(), qvec3f(-64, -64, -16));
}

TEST(qbspQ1, wrbrushesContentTypes)
{
    const auto [bsp, bspx, prt] = LoadTestmap("q1_hull1_content_types.map", {"-wrbrushes"});

    const bspxbrushes lump = deserialize<bspxbrushes>(bspx.at("BRUSHLIST"));
    ASSERT_EQ(lump.models.size(), 7); // world + 6x func_wall (solid, water, slime, lava, sky, clip)

    auto &worldmodel = lump.models.at(0);
    ASSERT_EQ(worldmodel.numfaces, 0); // all faces are axial
    ASSERT_EQ(worldmodel.modelnum, 0);

    // clang-format off
    const std::vector<int> expected
    {
        CONTENTS_SOLID,
        CONTENTS_SOLID,
        CONTENTS_SOLID,
        CONTENTS_SOLID,
        CONTENTS_SOLID,
        CONTENTS_SOLID,
        CONTENTS_WATER,
        CONTENTS_SLIME,
        CONTENTS_LAVA,
        CONTENTS_SOLID,
        CONTENTS_SKY,
        BSPXBRUSHES_CONTENTS_CLIP,
        CONTENTS_SOLID, // detail solid in source map
        CONTENTS_SOLID, // detail fence in source map
        // detail illusionary brush should be omitted
        CONTENTS_SOLID, // detail fence in source map
        // detail illusionary brush should be omitted
        CONTENTS_SOLID // detail wall in source map
    };
    // clang-format on
    ASSERT_EQ(worldmodel.brushes.size(), expected.size());

    for (size_t i = 0; i < expected.size(); ++i) {
        SCOPED_TRACE(fmt::format("brush {}", i));
        EXPECT_EQ(expected[i], worldmodel.brushes[i].contents);
    }

    {
        SCOPED_TRACE("bmodel contents");

        // 6x func_wall

        // clang-format off
        const std::vector<int> expected_bmodel_contents
        {
            CONTENTS_SOLID, // was solid
            CONTENTS_SOLID, // was water
            CONTENTS_SOLID, // was slime
            CONTENTS_SOLID, // was lava
            CONTENTS_SOLID, // was sky

            // clip is the only contents that doesn't behave as a solid when used in bmodels: you can shoot through
            // it but not walk through it. By mapping to BSPXBRUSHES_CONTENTS_CLIP
            // we get the same behaviour in FTEQW with -wrbrushes, as we do in the q1bsp loaded in QS.
            BSPXBRUSHES_CONTENTS_CLIP, // was clip
        };
        // clang-format on

        for (int i = 1; i < 7; ++i) {
            int expected_content = expected_bmodel_contents.at(i - 1);
            auto &bmodel = lump.models.at(i);

            ASSERT_EQ(bmodel.numfaces, 0); // all faces are axial
            ASSERT_EQ(bmodel.modelnum, i);

            ASSERT_EQ(bmodel.brushes.size(), 1);
            EXPECT_EQ(bmodel.brushes[0].contents, expected_content);
        }
    }
}

TEST(qbsp, readBspxBrushes)
{
    auto bsp_path = std::filesystem::path(testmaps_dir) / "compiled" / "q1_cube.bsp";

    bspdata_t bspdata;
    LoadBSPFile(bsp_path, &bspdata);
    bspdata.version->game->init_filesystem(bsp_path, qbsp_options);
    ConvertBSPFormat(&bspdata, &bspver_generic);

    const bspxbrushes lump = deserialize<bspxbrushes>(bspdata.bspx.entries.at("BRUSHLIST"));
    ASSERT_EQ(lump.models.size(), 1);

    EXPECT_EQ(lump.models[0].modelnum, 0);
    EXPECT_EQ(lump.models[0].numfaces, 0);
    EXPECT_EQ(lump.models[0].ver, 1);
    ASSERT_EQ(lump.models[0].brushes.size(), 1);

    auto &brush = lump.models[0].brushes[0];
    EXPECT_EQ(brush.bounds, aabb3f(qvec3f{32, -240, 80}, qvec3f{80, -144, 112}));
    EXPECT_EQ(brush.contents, CONTENTS_SOLID);
    EXPECT_EQ(brush.faces.size(), 0);
}

TEST(qbspQ1, lqE3m4map)
{
    GTEST_SKIP();

    const auto [bsp, bspx, prt] = LoadTestmap("LibreQuake/lq1/maps/src/e3/e3m4.map");
    EXPECT_TRUE(prt);
}

TEST(qbspQ1, tjuncMatrix)
{
    // TODO: test opaque water in q1 mode
    const auto [b, bspx, prt] = LoadTestmap("q1_tjunc_matrix.map");
    const mbsp_t &bsp = b; // workaround clang not allowing capturing bindings in lambdas
    auto *game = bsp.loadversion->game;

    EXPECT_EQ(GAME_QUAKE, game->id);

    const qvec3d face_midpoint_origin{-24, 0, 24};
    const qvec3d face_midpoint_to_tjunc{8, 0, 8};
    const qvec3d z_delta_to_next_face{0, 0, 64};
    const qvec3d x_delta_to_next_face{-64, 0, 0};

    enum index_t : int
    {
        INDEX_SOLID = 0,
        INDEX_SOLID_DETAIL,
        INDEX_DETAIL_WALL,
        INDEX_DETAIL_FENCE,
        INDEX_DETAIL_FENCE_MIRRORINSIDE,
        INDEX_DETAIL_ILLUSIONARY,
        INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES,
        INDEX_WATER,
        INDEX_SKY
    };

    auto has_tjunc = [&](index_t horizontal, index_t vertical) -> bool {
        const qvec3d face_midpoint = face_midpoint_origin + (x_delta_to_next_face * static_cast<int>(horizontal)) +
                                     (z_delta_to_next_face * static_cast<int>(vertical));

        auto *f = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], face_midpoint);

        const qvec3f tjunc_location = qvec3f(face_midpoint + face_midpoint_to_tjunc);

        for (int i = 0; i < f->numedges; ++i) {
            if (Face_PointAtIndex(&bsp, f, i) == tjunc_location) {
                return true;
            }
        }
        return false;
    };

    {
        SCOPED_TRACE("INDEX_SOLID horizontal - welds with anything opaque except detail_wall");
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_SOLID_DETAIL));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID, INDEX_DETAIL_WALL));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID, INDEX_DETAIL_FENCE));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID, INDEX_DETAIL_FENCE_MIRRORINSIDE));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID, INDEX_DETAIL_ILLUSIONARY));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID, INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_SOLID_DETAIL horizontal - welds with anything opaque except detail_wall");
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_SOLID_DETAIL));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_DETAIL_WALL));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_DETAIL_FENCE));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_DETAIL_FENCE_MIRRORINSIDE));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_DETAIL_ILLUSIONARY));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES));
        // see INDEX_SOLID, INDEX_WATER explanation
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_DETAIL_WALL horizontal");
        // solid cuts a hole in detail_wall
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_SOLID));
        // solid detail cuts a hole in detail_wall
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_SOLID_DETAIL));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_DETAIL_WALL));
        EXPECT_FALSE(has_tjunc(INDEX_DETAIL_WALL, INDEX_DETAIL_FENCE));
        EXPECT_FALSE(has_tjunc(INDEX_DETAIL_WALL, INDEX_DETAIL_FENCE_MIRRORINSIDE));
        EXPECT_FALSE(has_tjunc(INDEX_DETAIL_WALL, INDEX_DETAIL_ILLUSIONARY));
        EXPECT_FALSE(has_tjunc(INDEX_DETAIL_WALL, INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES));
        // see INDEX_SOLID, INDEX_WATER explanation
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_WATER));
        // sky cuts a hole in detail_wall
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_DETAIL_FENCE horizontal");
        // solid cuts a hole in fence
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE, INDEX_SOLID));
        // solid detail cuts a hole in fence
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE, INDEX_SOLID_DETAIL));
        // detail wall cuts a hole in fence
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE, INDEX_DETAIL_WALL));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE, INDEX_DETAIL_FENCE));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE, INDEX_DETAIL_FENCE_MIRRORINSIDE));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE, INDEX_DETAIL_ILLUSIONARY));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE, INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES));
        // weld because both are translucent
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE, INDEX_WATER));
        // sky cuts a hole in fence
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_DETAIL_FENCE_MIRRORINSIDE horizontal");
        // solid cuts a hole in fence
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE_MIRRORINSIDE, INDEX_SOLID));
        // solid detail cuts a hole in fence
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE_MIRRORINSIDE, INDEX_SOLID_DETAIL));
        // detail wall cuts a hole in fence
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE_MIRRORINSIDE, INDEX_DETAIL_WALL));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE_MIRRORINSIDE, INDEX_DETAIL_FENCE));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE_MIRRORINSIDE, INDEX_DETAIL_FENCE_MIRRORINSIDE));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE_MIRRORINSIDE, INDEX_DETAIL_ILLUSIONARY));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE_MIRRORINSIDE, INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES));
        // weld because both are translucent
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE_MIRRORINSIDE, INDEX_WATER));
        // sky cuts a hole in fence
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_FENCE_MIRRORINSIDE, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_DETAIL_ILLUSIONARY horizontal");
        // solid cuts a hole in illusionary
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY, INDEX_SOLID));
        // solid detail cuts a hole in illusionary
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY, INDEX_SOLID_DETAIL));
        // detail wall cuts a hole in illusionary
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY, INDEX_DETAIL_WALL));
        // fence and illusionary are both translucent, so weld
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY, INDEX_DETAIL_FENCE));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY, INDEX_DETAIL_FENCE_MIRRORINSIDE));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY, INDEX_DETAIL_ILLUSIONARY));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY, INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES));
        // weld because both are translucent
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY, INDEX_WATER));
        // sky cuts a hole in illusionary
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES horizontal");
        // solid cuts a hole in illusionary
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES, INDEX_SOLID));
        // solid detail cuts a hole in illusionary
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES, INDEX_SOLID_DETAIL));
        // detail wall cuts a hole in illusionary
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES, INDEX_DETAIL_WALL));
        // fence and illusionary are both translucent, so weld
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES, INDEX_DETAIL_FENCE));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES, INDEX_DETAIL_FENCE_MIRRORINSIDE));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES, INDEX_DETAIL_ILLUSIONARY));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES, INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES));
        // weld because both are translucent
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES, INDEX_WATER));
        // sky cuts a hole in illusionary
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_WATER horizontal");
        // solid cuts a hole in water
        EXPECT_TRUE(has_tjunc(INDEX_WATER, INDEX_SOLID));
        // solid detail cuts a hole in illusionary
        EXPECT_TRUE(has_tjunc(INDEX_WATER, INDEX_SOLID_DETAIL));
        // detail wall cuts a hole in water
        EXPECT_TRUE(has_tjunc(INDEX_WATER, INDEX_DETAIL_WALL));
        EXPECT_TRUE(has_tjunc(INDEX_WATER, INDEX_DETAIL_FENCE));
        EXPECT_TRUE(has_tjunc(INDEX_WATER, INDEX_DETAIL_FENCE_MIRRORINSIDE));
        EXPECT_TRUE(has_tjunc(INDEX_WATER, INDEX_DETAIL_ILLUSIONARY));
        EXPECT_TRUE(has_tjunc(INDEX_WATER, INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES));
        EXPECT_TRUE(has_tjunc(INDEX_WATER, INDEX_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_WATER, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_SKY horizontal");
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_SOLID_DETAIL));
        EXPECT_FALSE(has_tjunc(INDEX_SKY, INDEX_DETAIL_WALL));
        EXPECT_FALSE(has_tjunc(INDEX_SKY, INDEX_DETAIL_FENCE));
        EXPECT_FALSE(has_tjunc(INDEX_SKY, INDEX_DETAIL_FENCE_MIRRORINSIDE));
        EXPECT_FALSE(has_tjunc(INDEX_SKY, INDEX_DETAIL_ILLUSIONARY));
        EXPECT_FALSE(has_tjunc(INDEX_SKY, INDEX_DETAIL_ILLUSIONARY_NOCLIPFACES));
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_SKY));
    }
}

TEST(testmapsQ1, liquidIsDetail)
{
    const auto portal_underwater =
        prtfile_winding_t{{-168, -384, 32}, {-168, -320, 32}, {-168, -320, -32}, {-168, -384, -32}};
    const auto portal_above = portal_underwater.translate({0, 320, 128});

    {
        SCOPED_TRACE("transparent water");

        // by default, we're compiling with transparent water
        // this implies water is detail

        const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_liquid_is_detail.map");

        ASSERT_TRUE(prt.has_value());
        ASSERT_EQ(2, prt->portals.size());

        EXPECT_TRUE(((PortalMatcher(prt->portals[0].winding, portal_underwater) &&
                         PortalMatcher(prt->portals[1].winding, portal_above)) ||
                     (PortalMatcher(prt->portals[0].winding, portal_above) &&
                         PortalMatcher(prt->portals[1].winding, portal_underwater))));

        // only 3 clusters: room with water, side corridors
        EXPECT_EQ(prt->portalleafs, 3);

        // above water, in water, plus 2 side rooms.
        // note
        EXPECT_EQ(prt->portalleafs_real, 4);
    }

    {
        SCOPED_TRACE("opaque water");

        const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_liquid_is_detail.map", {"-notranswater"});

        ASSERT_TRUE(prt.has_value());
        ASSERT_EQ(2, prt->portals.size());

        // same portals as transparent water case
        // (since the water is opqaue, it doesn't get a portal)
        EXPECT_TRUE(((PortalMatcher(prt->portals[0].winding, portal_underwater) &&
                         PortalMatcher(prt->portals[1].winding, portal_above)) ||
                     (PortalMatcher(prt->portals[0].winding, portal_above) &&
                         PortalMatcher(prt->portals[1].winding, portal_underwater))));

        // 4 clusters this time:
        // above water, in water, plus 2 side rooms.
        EXPECT_EQ(prt->portalleafs, 4);
        EXPECT_EQ(prt->portalleafs_real, 4);
    }
}
