#include "test_qbsp.hh"

#include <qbsp/brush.hh>
#include <qbsp/brushbsp.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/map.hh>
#include <qbsp/csg.hh>
#include <common/fs.hh>
#include <common/bsputils.hh>
#include <common/decompile.hh>
#include <common/prtfile.hh>
#include <common/qvec.hh>
#include <common/log.hh>
#include <testmaps.hh>

#include <fstream>
#include <cstring>
#include <stdexcept>
#include <tuple>
#include <map>
#include <doctest/doctest.h>
#include "testutils.hh"

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

mapentity_t &LoadMap(const char *map, size_t length)
{
    ::map.reset();
    qbsp_options.reset();

    qbsp_options.target_version = &bspver_q1;
    qbsp_options.target_game = qbsp_options.target_version->game;

    parser_t parser(map, length, {doctest::getContextOptions()->currentTest->m_name});

    mapentity_t &entity = ::map.entities.emplace_back();
    texture_def_issues_t issue_stats;

    // FIXME: adds the brush to the global map...
    Q_assert(ParseEntity(parser, entity, issue_stats));

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

    std::vector<std::string> args{"", // the exe path, which we're ignoring in this case
        "-noverbose", "-path", wal_metadata_path.string()};
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
        CHECK(contents == Q2_CONTENTS_SOLID);
    } else {
        CHECK(contents == CONTENTS_SOLID);
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
TEST_CASE("testTextureIssue" * doctest::test_suite("qbsp"))
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
            CHECK(doctest::Approx(texvecsExpected[i][j]) == texvecsActual[i][j]);
        }
    }
#endif
}

TEST_CASE("duplicatePlanes" * doctest::test_suite("qbsp"))
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
    REQUIRE(1 == worldspawn.mapbrushes.size());
    CHECK(6 == worldspawn.mapbrushes.front().faces.size());

    auto brush = LoadBrush(worldspawn, worldspawn.mapbrushes.front(), {CONTENTS_SOLID}, 0, std::nullopt);
    CHECK(6 == brush->sides.size());
}

/**
 * Test that this skip face gets auto-corrected.
 */
TEST_CASE("InvalidTextureProjection" * doctest::test_suite("qbsp"))
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

    mapentity_t &worldspawn = LoadMap(map);
    Q_assert(1 == worldspawn.mapbrushes.size());

    const mapface_t *face = &worldspawn.mapbrushes.front().faces[5];
    REQUIRE("skip" == face->texname);
    const auto texvecs = face->get_texvecs();
    CHECK(IsValidTextureProjection(face->get_plane().get_normal(), texvecs.row(0), texvecs.row(1)));
}

/**
 * Same as above but the texture scales are 0
 */
TEST_CASE("InvalidTextureProjection2" * doctest::test_suite("qbsp"))
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

    mapentity_t &worldspawn = LoadMap(map);
    Q_assert(1 == worldspawn.mapbrushes.size());

    const mapface_t *face = &worldspawn.mapbrushes.front().faces[5];
    REQUIRE("skip" == face->texname);
    const auto texvecs = face->get_texvecs();
    CHECK(IsValidTextureProjection(face->get_plane().get_normal(), texvecs.row(0), texvecs.row(1)));
}

/**
 * More realistic: *lava1 has tex vecs perpendicular to face
 */
TEST_CASE("InvalidTextureProjection3" * doctest::test_suite("qbsp"))
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

    mapentity_t &worldspawn = LoadMap(map);
    Q_assert(1 == worldspawn.mapbrushes.size());

    const mapface_t *face = &worldspawn.mapbrushes.front().faces[3];
    REQUIRE("*lava1" == face->texname);
    const auto texvecs = face->get_texvecs();
    CHECK(IsValidTextureProjection(face->get_plane().get_normal(), texvecs.row(0), texvecs.row(1)));
}

TEST_SUITE("mathlib")
{
    TEST_CASE("WindingArea")
    {
        winding_t w(5);

        // poor test.. but at least checks that the colinear point is treated correctly
        w[0] = {0, 0, 0};
        w[1] = {0, 32, 0}; // colinear
        w[2] = {0, 64, 0};
        w[3] = {64, 64, 0};
        w[4] = {64, 0, 0};

        CHECK(64.0f * 64.0f == w.area());
    }
}

/**
 * checks that options are reset across tests.
 * set two random options and check that they don't carry over.
 */
TEST_CASE("options_reset1" * doctest::test_suite("testmaps_q1"))
{
    LoadTestmap("qbsp_simple_sealed.map", {"-transsky"});

    CHECK_FALSE(qbsp_options.forcegoodtree.value());
    CHECK(qbsp_options.transsky.value());
}

TEST_CASE("options_reset2" * doctest::test_suite("testmaps_q1"))
{
    LoadTestmap("qbsp_simple_sealed.map", {"-forcegoodtree"});

    CHECK(qbsp_options.forcegoodtree.value());
    CHECK_FALSE(qbsp_options.transsky.value());
}

/**
 * The brushes are touching but not intersecting, so ChopBrushes shouldn't change anything.
 */
TEST_CASE("chop_no_change" * doctest::test_suite("testmaps_q1"))
{
    LoadTestmapQ1("qbsp_chop_no_change.map");

    // TODO: ideally we should check we get back the same brush pointers from ChopBrushes
}

TEST_CASE("simple_sealed" * doctest::test_suite("testmaps_q1"))
{
    const std::vector<std::string> quake_maps{"qbsp_simple_sealed.map", "qbsp_simple_sealed_rotated.map"};

    for (const auto &mapname : quake_maps) {
        SUBCASE(fmt::format("testing {}", mapname).c_str())
        {

            const auto [bsp, bspx, prt] = LoadTestmapQ1(mapname);

            REQUIRE(bsp.dleafs.size() == 2);

            REQUIRE(bsp.dleafs[0].contents == CONTENTS_SOLID);
            REQUIRE(bsp.dleafs[1].contents == CONTENTS_EMPTY);

            // just a hollow box
            REQUIRE(bsp.dfaces.size() == 6);

            // no bspx lumps
            CHECK(bspx.empty());

            // check markfaces
            CHECK(bsp.dleafs[0].nummarksurfaces == 0);
            CHECK(bsp.dleafs[0].firstmarksurface == 0);

            CHECK(bsp.dleafs[1].nummarksurfaces == 6);
            CHECK(bsp.dleafs[1].firstmarksurface == 0);
            CHECK_VECTORS_UNOREDERED_EQUAL(bsp.dleaffaces, std::vector<uint32_t>{0, 1, 2, 3, 4, 5});
        }
    }
}

TEST_CASE("simple_sealed2" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_sealed2.map");

    CHECK(bsp.dleafs.size() == 3);

    CHECK(bsp.dleafs[0].contents == CONTENTS_SOLID);
    CHECK(bsp.dleafs[1].contents == CONTENTS_EMPTY);
    CHECK(bsp.dleafs[2].contents == CONTENTS_EMPTY);

    // L-shaped room
    // 2 ceiling + 2 floor + 6 wall faces
    CHECK(bsp.dfaces.size() == 10);

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

    CHECK_VECTORS_UNOREDERED_EQUAL(other_markfaces,
        std::vector<const mface_t *>{other_floor, other_ceil, other_minus_x, other_plus_x, other_plus_y});
}

TEST_CASE("simple_worldspawn_worldspawn" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_worldspawn_worldspawn.map", {"-tjunc", "rotate"});

    // 1 solid leaf
    // 5 empty leafs around the button
    REQUIRE(bsp.dleafs.size() == 6);

    // 5 faces for the "button"
    // 9 faces for the room (6 + 3 extra for the floor splits)
    REQUIRE(bsp.dfaces.size() == 14);

    int fan_faces = 0;
    int room_faces = 0;
    for (auto &face : bsp.dfaces) {
        const char *texname = Face_TextureName(&bsp, &face);
        if (!strcmp(texname, "orangestuff8")) {
            ++room_faces;
        } else if (!strcmp(texname, "+0fan")) {
            ++fan_faces;
        } else {
            FAIL("");
        }
    }
    REQUIRE(fan_faces == 5);
    REQUIRE(room_faces == 9);
}

TEST_CASE("simple_worldspawn_detail_wall" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_worldspawn_detail_wall.map");

    CHECK(prt.has_value());

    // 5 faces for the "button"
    // 6 faces for the room
    CHECK(bsp.dfaces.size() == 11);

    const qvec3d button_pos = {16, -48, 104};
    auto *button_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], button_pos);

    CHECK(button_leaf->contents == CONTENTS_SOLID);
    CHECK(button_leaf == &bsp.dleafs[0]); // should be using shared solid leaf because it's func_detail_wall
}

TEST_CASE("simple_worldspawn_detail" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_worldspawn_detail.map", {"-tjunc", "rotate"});

    REQUIRE(prt.has_value());

    // 5 faces for the "button"
    // 9 faces for the room
    REQUIRE(bsp.dfaces.size() == 14);

    // 6 for the box room
    // 5 for the "button"
    CHECK(bsp.dnodes.size() == 11);

    // this is how many we get with ericw-tools-v0.18.1-32-g6660c5f-win64
    CHECK(bsp.dclipnodes.size() <= 22);
}

TEST_CASE("simple_worldspawn_detail_illusionary" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_worldspawn_detail_illusionary.map");

    REQUIRE(prt.has_value());

    // 5 faces for the "button"
    // 6 faces for the room
    CHECK(bsp.dfaces.size() == 11);

    // leaf/node counts
    CHECK(11 == bsp.dnodes.size()); // one node per face
    CHECK(7 == bsp.dleafs.size()); // shared solid leaf + 6 empty leafs inside the room

    // where the func_detail_illusionary sticks into the void
    const qvec3d illusionary_in_void{8, -40, 72};
    CHECK(CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], illusionary_in_void)->contents);

    CHECK(prt->portals.size() == 0);
    CHECK(prt->portalleafs == 1);
}

TEST_CASE("simple_worldspawn_sky" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple_worldspawn_sky.map");

    REQUIRE(prt.has_value());

    // just a box with sky on the ceiling
    const auto textureToFace = MakeTextureToFaceMap(bsp);
    CHECK(1 == textureToFace.at("sky3").size());
    CHECK(5 == textureToFace.at("orangestuff8").size());

    // leaf/node counts
    // - we'd get 7 nodes if it's cut like a cube (solid outside), with 1 additional cut inside to divide sky / empty
    // - we'd get 11 if it's cut as the sky plane (1), then two open cubes (5 nodes each)
    // - can get in between values if it does some vertical cuts, then the sky plane, then other vertical cuts
    //
    // the 7 solution is better but the BSP heuristics won't help reach that one in this trivial test map
    CHECK(bsp.dnodes.size() >= 7);
    CHECK(bsp.dnodes.size() <= 11);
    CHECK(3 == bsp.dleafs.size()); // shared solid leaf + empty + sky

    // check contents
    const qvec3d player_pos{-88, -64, 120};
    const double inside_sky_z = 232;

    CHECK(CONTENTS_EMPTY == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos)->contents);

    // way above map is solid - sky should not fill outwards
    // (otherwise, if you had sky with a floor further up above it, it's not clear where the leafs would be divided, or
    // if the floor contents would turn to sky, etc.)
    CHECK(CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(0, 0, 500))->contents);

    CHECK(CONTENTS_SKY ==
          BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], qvec3d(player_pos[0], player_pos[1], inside_sky_z))->contents);

    CHECK(CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(500, 0, 0))->contents);
    CHECK(CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(-500, 0, 0))->contents);
    CHECK(CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(0, 500, 0))->contents);
    CHECK(CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(0, -500, 0))->contents);
    CHECK(CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_pos + qvec3d(0, 0, -500))->contents);

    CHECK(prt->portals.size() == 0);
    // FIXME: unsure what the expected number of visclusters is, does sky get one?
}

TEST_CASE("water_detail_illusionary" * doctest::test_suite("testmaps_q1"))
{
    static const std::string basic_mapname = "qbsp_water_detail_illusionary.map";
    static const std::string mirrorinside_mapname = "qbsp_water_detail_illusionary_mirrorinside.map";

    for (const auto &mapname : {basic_mapname, mirrorinside_mapname}) {
        SUBCASE(fmt::format("testing {}", mapname).c_str())
        {
            const auto [bsp, bspx, prt] = LoadTestmapQ1(mapname);

            REQUIRE(prt.has_value());

            const qvec3d inside_water_and_fence{-20, -52, 124};
            const qvec3d inside_fence{-20, -52, 172};

            CHECK(BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_water_and_fence)->contents == CONTENTS_WATER);
            CHECK(BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_fence)->contents == CONTENTS_EMPTY);

            const qvec3d underwater_face_pos{-40, -52, 124};
            const qvec3d above_face_pos{-40, -52, 172};

            // make sure the detail_illusionary face underwater isn't clipped away
            auto *underwater_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], underwater_face_pos, {-1, 0, 0});
            auto *underwater_face_inner = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], underwater_face_pos, {1, 0, 0});

            auto *above_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], above_face_pos, {-1, 0, 0});
            auto *above_face_inner = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], above_face_pos, {1, 0, 0});

            REQUIRE(nullptr != underwater_face);
            REQUIRE(nullptr != above_face);

            CHECK(std::string("{trigger") == Face_TextureName(&bsp, underwater_face));
            CHECK(std::string("{trigger") == Face_TextureName(&bsp, above_face));

            if (mapname == mirrorinside_mapname) {
                REQUIRE(underwater_face_inner != nullptr);
                REQUIRE(above_face_inner != nullptr);

                CHECK(std::string("{trigger") == Face_TextureName(&bsp, underwater_face_inner));
                CHECK(std::string("{trigger") == Face_TextureName(&bsp, above_face_inner));
            } else {
                CHECK(underwater_face_inner == nullptr);
                CHECK(above_face_inner == nullptr);
            }
        }
    }
}

TEST_CASE("qbsp_bmodel_mirrorinside_with_liquid" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_bmodel_mirrorinside_with_liquid.map");

    REQUIRE(prt.has_value());

    const qvec3d model1_fenceface{-16, -56, 168};
    const qvec3d model2_waterface{-16, -120, 168};

    CHECK(2 == BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[1], model1_fenceface).size());
    CHECK(2 == BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[2], model2_waterface).size());

    // both bmodels should be CONTENTS_SOLID in all hulls
    for (int model_idx = 1; model_idx <= 2; ++model_idx) {
        for (int hull = 0; hull <= 2; ++hull) {
            auto &model = bsp.dmodels[model_idx];

            INFO("model: ", model_idx, " hull: ", hull);
            CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, {hull}, &model, (model.mins + model.maxs) / 2));
        }
    }
}

TEST_CASE("q1_bmodel_liquid" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_bmodel_liquid.map", {"-bmodelcontents"});
    REQUIRE(prt.has_value());

    // nonsolid brushes don't show up in clipping hulls. so 6 for the box room in hull1, and 6 for hull2.
    REQUIRE(12 == bsp.dclipnodes.size());

    const auto inside_water = qvec3d{8, -120, 184};
    CHECK(CONTENTS_WATER == BSP_FindContentsAtPoint(&bsp, {0}, &bsp.dmodels[1], inside_water));

    CHECK(CONTENTS_EMPTY == BSP_FindContentsAtPoint(&bsp, {1}, &bsp.dmodels[1], inside_water));
    CHECK(CONTENTS_EMPTY == BSP_FindContentsAtPoint(&bsp, {2}, &bsp.dmodels[1], inside_water));
}

TEST_CASE("q1_liquid_mirrorinside_off" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_liquid_mirrorinside_off.map");
    REQUIRE(prt.has_value());

    // normally there would be 2 faces, but with _mirrorinside 0 we should get only the upwards-pointing one
    CHECK(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels.at(0), {-52, -56, 8}, {0, 0, 1}));
    CHECK(!BSP_FindFaceAtPoint(&bsp, &bsp.dmodels.at(0), {-52, -56, 8}, {0, 0, -1}));
}

TEST_CASE("noclipfaces" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_noclipfaces.map");

    REQUIRE(prt.has_value());

    REQUIRE(bsp.dfaces.size() == 2);

    // TODO: contents should be empty in hull0 because it's func_detail_illusionary

    for (auto &face : bsp.dfaces) {
        REQUIRE(std::string("{trigger") == Face_TextureName(&bsp, &face));
    }

    CHECK(prt->portals.size() == 0);
    CHECK(prt->portalleafs == 1);
}

/**
 * _noclipfaces 1 detail_fence meeting a _noclipfaces 0 one.
 *
 * Currently, to simplify the implementation, we're treating that the same as if both had _noclipfaces 1
 */
TEST_CASE("noclipfaces_junction" * doctest::test_suite("testmaps_q1"))
{
    const std::vector<std::string> maps{"qbsp_noclipfaces_junction.map", "q2_noclipfaces_junction.map"};

    for (const auto &map : maps) {
        const bool q2 = (map.find("q2") == 0);

        SUBCASE(map.c_str())
        {
            const auto [bsp, bspx, prt] = q2 ? LoadTestmapQ2(map) : LoadTestmapQ1(map);

            CHECK(bsp.dfaces.size() == 12);

            const qvec3d portal_pos{96, 56, 32};

            auto *pos_x = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], portal_pos, {1, 0, 0});
            auto *neg_x = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], portal_pos, {-1, 0, 0});

            REQUIRE(pos_x != nullptr);
            REQUIRE(neg_x != nullptr);

            if (q2) {
                CHECK(std::string("e1u1/wndow1_2") == Face_TextureName(&bsp, pos_x));
                CHECK(std::string("e1u1/window1") == Face_TextureName(&bsp, neg_x));
            } else {
                CHECK(std::string("{trigger") == Face_TextureName(&bsp, pos_x));
                CHECK(std::string("blood1") == Face_TextureName(&bsp, neg_x));
            }
        }
    }
}

/**
 * Same as previous test, but the T shaped brush entity has _mirrorinside
 */
TEST_CASE("noclipfaces_mirrorinside" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_noclipfaces_mirrorinside.map");

    REQUIRE(prt.has_value());

    REQUIRE(bsp.dfaces.size() == 4);

    // TODO: contents should be empty in hull0 because it's func_detail_illusionary

    for (auto &face : bsp.dfaces) {
        REQUIRE(std::string("{trigger") == Face_TextureName(&bsp, &face));
    }

    CHECK(prt->portals.size() == 0);
    CHECK(prt->portalleafs == 1);
}

TEST_CASE("detail_illusionary_intersecting" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_detail_illusionary_intersecting.map", {"-tjunc", "rotate"});

    REQUIRE(prt.has_value());

    // sides: 3*4 = 12
    // top: 3 (4 with new tjunc code that prefers more faces over 0-area tris)
    // bottom: 3 (4 with new tjunc code that prefers more faces over 0-area tris)
    CHECK(bsp.dfaces.size() >= 18);
    CHECK(bsp.dfaces.size() <= 20);

    for (auto &face : bsp.dfaces) {
        CHECK(std::string("{trigger") == Face_TextureName(&bsp, &face));
    }

    // top of cross
    CHECK(1 == BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -50, 120), qvec3d(0, 0, 1)).size());

    // interior face that should be clipped away
    CHECK(0 == BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -52, 116), qvec3d(0, -1, 0)).size());

    CHECK(prt->portals.size() == 0);
    CHECK(prt->portalleafs == 1);
}

TEST_CASE("detail_illusionary_noclipfaces_intersecting" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] =
        LoadTestmapQ1("qbsp_detail_illusionary_noclipfaces_intersecting.map", {"-tjunc", "rotate"});

    REQUIRE(prt.has_value());

    for (auto &face : bsp.dfaces) {
        CHECK(std::string("{trigger") == Face_TextureName(&bsp, &face));
    }

    // top of cross has 2 faces Z-fighting, because we disabled clipping
    // (with qbsp3 method, there won't ever be z-fighting since we only ever generate 1 face per portal)
    size_t faces_at_top = BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -50, 120), qvec3d(0, 0, 1)).size();
    CHECK(faces_at_top >= 1);
    CHECK(faces_at_top <= 2);

    // interior face not clipped away
    CHECK(1 == BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-58, -52, 116), qvec3d(0, -1, 0)).size());

    CHECK(prt->portals.size() == 0);
    CHECK(prt->portalleafs == 1);
}

TEST_CASE("q1_detail_non_sealing" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_detail_non_sealing.map");

    CHECK(!prt.has_value());
}

TEST_CASE("q1_sealing_contents" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_sealing_contents.map");

    CHECK(prt.has_value());
}

TEST_CASE("q1_detail_touching_water" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_detail_touching_water.map");

    CHECK(prt.has_value());
}

TEST_CASE("detail_doesnt_remove_world_nodes" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_detail_doesnt_remove_world_nodes.map");

    REQUIRE(prt.has_value());

    {
        // check for a face under the start pos
        const qvec3d floor_under_start{-56, -72, 64};
        auto *floor_under_start_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], floor_under_start, {0, 0, 1});
        CHECK(nullptr != floor_under_start_face);
    }

    {
        // floor face should be clipped away by detail
        const qvec3d floor_inside_detail{64, -72, 64};
        auto *floor_inside_detail_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], floor_inside_detail, {0, 0, 1});
        CHECK(nullptr == floor_inside_detail_face);
    }

    // make sure the detail face exists
    CHECK(nullptr != BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {32, -72, 136}, {-1, 0, 0}));

    {
        // but the sturctural nodes/leafs should not be clipped away by detail
        const qvec3d covered_by_detail{48, -88, 128};
        auto *covered_by_detail_node = BSP_FindNodeAtPoint(&bsp, &bsp.dmodels[0], covered_by_detail, {-1, 0, 0});
        CHECK(nullptr != covered_by_detail_node);
    }
}

TEST_CASE("merge" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_merge.map");

    REQUIRE_FALSE(prt.has_value());
    REQUIRE(bsp.dfaces.size() >= 6);

    // BrushBSP does a split through the middle first to keep the BSP balanced, which prevents
    // two of the side face from being merged
    REQUIRE(bsp.dfaces.size() <= 8);

    const auto exp_bounds = aabb3d{{48, 0, 96}, {224, 96, 96}};

    auto *top_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {48, 0, 96}, {0, 0, 1});
    const auto top_winding = Face_Winding(&bsp, top_face);

    CHECK(top_winding.bounds().mins() == exp_bounds.mins());
    CHECK(top_winding.bounds().maxs() == exp_bounds.maxs());
}

TEST_CASE("tjunc_many_sided_face" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_tjunc_many_sided_face.map", {"-tjunc", "rotate"});

    REQUIRE(prt.has_value());

    std::map<qvec3d, std::vector<const mface_t *>> faces_by_normal;
    for (auto &face : bsp.dfaces) {
        faces_by_normal[Face_Normal(&bsp, &face)].push_back(&face);
    }

    REQUIRE(6 == faces_by_normal.size());

    // the floor has a 0.1 texture scale, so it gets subdivided into many small faces
    CHECK(15 * 15 == (faces_by_normal.at({0, 0, 1}).size()));

    // the ceiling gets split into 2 faces because fixing T-Junctions with all of the
    // wall sections exceeds the max vertices per face limit
    CHECK(2 == (faces_by_normal.at({0, 0, -1}).size()));
}

TEST_CASE("tjunc_angled_face" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_tjunc_angled_face.map");
    CheckFilled(bsp);

    auto faces = FacesWithTextureName(bsp, "bolt6");
    REQUIRE(faces.size() == 1);

    auto *bolt6_face = faces.at(0);
    CHECK(bolt6_face->numedges == 5);
}

/**
 * Because it comes second, the sbutt2 brush should "win" in clipping against the floor,
 * in both a worldspawn test case, as well as a func_wall.
 */
TEST_CASE("brush_clipping_order" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_brush_clipping_order.map", {"-tjunc", "rotate"});

    REQUIRE(prt.has_value());

    const qvec3d world_button{-8, -8, 16};
    const qvec3d func_wall_button{152, -8, 16};

    // 0 = world, 1 = func_wall
    REQUIRE(2 == bsp.dmodels.size());

    REQUIRE(20 == bsp.dfaces.size());

    REQUIRE(10 == bsp.dmodels[0].numfaces); // 5 faces for the sides + bottom, 5 faces for the top
    REQUIRE(10 == bsp.dmodels[1].numfaces); // (same on worldspawn and func_wall)

    auto *world_button_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], world_button, {0, 0, 1});
    REQUIRE(nullptr != world_button_face);
    REQUIRE(std::string("sbutt2") == Face_TextureName(&bsp, world_button_face));

    auto *func_wall_button_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[1], func_wall_button, {0, 0, 1});
    REQUIRE(nullptr != func_wall_button_face);
    REQUIRE(std::string("sbutt2") == Face_TextureName(&bsp, func_wall_button_face));
}

/**
 * Box room with a rotating fan (just a cube). Works in a mod with hiprotate - AD, Quoth, etc.
 */
TEST_CASE("origin" * doctest::test_suite("testmaps_q1"))
{
    const std::vector<std::string> maps{
        "qbsp_origin.map",
        "qbsp_hiprotate.map" // same, but uses info_rotate instead of an origin brush
    };

    for (const auto &map : maps) {
        SUBCASE(map.c_str())
        {
            const auto [bsp, bspx, prt] = LoadTestmapQ1(map);

            REQUIRE(prt.has_value());

            // 0 = world, 1 = rotate_object
            REQUIRE(2 == bsp.dmodels.size());

            // check that the origin brush didn't clip away any solid faces, or generate faces
            REQUIRE(6 == bsp.dmodels[1].numfaces);

            // FIXME: should the origin brush update the dmodel's origin too?
            REQUIRE(qvec3f(0, 0, 0) == bsp.dmodels[1].origin);

            // check that the origin brush updated the entity lump
            auto ents = EntData_Parse(bsp);
            auto it = std::find_if(ents.begin(), ents.end(),
                [](const entdict_t &dict) -> bool { return dict.get("classname") == "rotate_object"; });

            REQUIRE(it != ents.end());
            CHECK(it->get("origin") == "216 -216 340");
        }
    }
}

TEST_CASE("simple" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple.map");

    REQUIRE_FALSE(prt.has_value());
}

/**
 * Just a solid cuboid
 */
TEST_CASE("q1_cube")
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_cube.map");

    REQUIRE_FALSE(prt.has_value());

    const aabb3f cube_bounds{{32, -240, 80}, {80, -144, 112}};

    CHECK(bsp.dedges.size() == 13); // index 0 is reserved, and the cube has 12 edges

    REQUIRE(7 == bsp.dleafs.size());

    // check the solid leaf
    auto &solid_leaf = bsp.dleafs[0];
    CHECK(solid_leaf.mins == qvec3f(0, 0, 0));
    CHECK(solid_leaf.maxs == qvec3f(0, 0, 0));

    // check the empty leafs
    for (int i = 1; i < 7; ++i) {
        SUBCASE(fmt::format("leaf {}", i).c_str())
        {
            auto &leaf = bsp.dleafs[i];
            CHECK(CONTENTS_EMPTY == leaf.contents);

            CHECK(1 == leaf.nummarksurfaces);
        }
    }

    REQUIRE(6 == bsp.dfaces.size());

    // node bounds
    auto cube_bounds_grown = cube_bounds.grow(24);

    auto &headnode = bsp.dnodes[bsp.dmodels[0].headnode[0]];
    CHECK(cube_bounds_grown.mins() == headnode.mins);
    CHECK(cube_bounds_grown.maxs() == headnode.maxs);

    // model bounds are shrunk by 1 unit on each side for some reason
    CHECK(cube_bounds.grow(-1).mins() == bsp.dmodels[0].mins);
    CHECK(cube_bounds.grow(-1).maxs() == bsp.dmodels[0].maxs);

    CHECK(6 == bsp.dnodes.size());

    CHECK(12 == bsp.dclipnodes.size());
}

/**
 * Two solid cuboids touching along one edge
 */
TEST_CASE("q1_cubes" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_cubes.map");

    CHECK(bsp.dedges.size() == 25);
}

/**
 * Ensure submodels that are all "clip" get bounds set correctly
 */
TEST_CASE("q1_clip_func_wall" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_clip_func_wall.map");

    REQUIRE(prt.has_value());

    const aabb3f cube_bounds{{64, 64, 48}, {128, 128, 80}};

    REQUIRE(2 == bsp.dmodels.size());

    // node bounds
    auto &headnode = bsp.dnodes[bsp.dmodels[1].headnode[0]];
    CHECK(cube_bounds.grow(24).mins() == headnode.mins);
    CHECK(cube_bounds.grow(24).maxs() == headnode.maxs);

    // model bounds are shrunk by 1 unit on each side for some reason
    CHECK(cube_bounds.grow(-1).mins() == bsp.dmodels[1].mins);
    CHECK(cube_bounds.grow(-1).maxs() == bsp.dmodels[1].maxs);
}

/**
 * Lots of features in one map, more for testing in game than automated testing
 */
TEST_CASE("features" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbspfeatures.map");

    REQUIRE(prt.has_value());

    CHECK(bsp.loadversion == &bspver_q1);
}

TEST_CASE("q1_detail_wall tjuncs" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_detail_wall.map");

    REQUIRE(prt.has_value());
    CHECK(bsp.loadversion == &bspver_q1);

    const auto behind_pillar = qvec3d(-160, -140, 120);
    auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], behind_pillar, qvec3d(1, 0, 0));
    REQUIRE(face);

    INFO("func_detail_wall should not generate extra tjunctions on structural faces");
    auto w = Face_Winding(&bsp, face);
    CHECK(w.size() == 5);
}

TEST_CASE("q1_detail_wall_intersecting_detail" * doctest::test_suite("testmaps_q1") * doctest::may_fail())
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_detail_wall_intersecting_detail.map");

    const auto *left_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-152, -192, 160}, {1, 0, 0});
    const auto *under_detail_wall_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-152, -176, 160}, {1, 0, 0});
    const auto *right_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-152, -152, 160}, {1, 0, 0});

    CHECK(left_face != nullptr);
    CHECK(under_detail_wall_face != nullptr);
    CHECK(right_face != nullptr);

    CHECK(left_face == under_detail_wall_face);
    CHECK(left_face == right_face);
}

bool PortalMatcher(const prtfile_winding_t &a, const prtfile_winding_t &b)
{
    return a.undirectional_equal(b);
}

TEST_CASE("qbsp_func_detail various types" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_func_detail.map");

    REQUIRE(prt.has_value());
    CHECK(GAME_QUAKE == bsp.loadversion->game->id);

    CHECK(1 == bsp.dmodels.size());

    const qvec3d in_func_detail{56, -56, 120};
    const qvec3d in_func_detail_wall{56, -136, 120};
    const qvec3d in_func_detail_illusionary{56, -216, 120};
    const qvec3d in_func_detail_illusionary_mirrorinside{56, -296, 120};

    // const double floor_z = 96;

    // detail clips away world faces, others don't
    CHECK(nullptr == BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], in_func_detail - qvec3d(0, 0, 24), {0, 0, 1}));
    CHECK(nullptr != BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], in_func_detail_wall - qvec3d(0, 0, 24), {0, 0, 1}));
    CHECK(nullptr !=
          BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], in_func_detail_illusionary - qvec3d(0, 0, 24), {0, 0, 1}));
    CHECK(nullptr != BSP_FindFaceAtPoint(
                         &bsp, &bsp.dmodels[0], in_func_detail_illusionary_mirrorinside - qvec3d(0, 0, 24), {0, 0, 1}));

    // check for correct contents
    auto *detail_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_func_detail);
    auto *detail_wall_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_func_detail_wall);
    auto *detail_illusionary_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_func_detail_illusionary);
    auto *detail_illusionary_mirrorinside_leaf =
        BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_func_detail_illusionary_mirrorinside);

    CHECK(CONTENTS_SOLID == detail_leaf->contents);
    CHECK(CONTENTS_SOLID == detail_wall_leaf->contents);
    CHECK(CONTENTS_EMPTY == detail_illusionary_leaf->contents);
    CHECK(CONTENTS_EMPTY == detail_illusionary_mirrorinside_leaf->contents);

    // portals

    REQUIRE(2 == prt->portals.size());

    const auto p0 = prtfile_winding_t{{-160, -8, 352}, {56, -8, 352}, {56, -8, 96}, {-160, -8, 96}};
    const auto p1 = p0.translate({232, 0, 0});

    CHECK(((PortalMatcher(prt->portals[0].winding, p0) && PortalMatcher(prt->portals[1].winding, p1)) ||
           (PortalMatcher(prt->portals[0].winding, p1) && PortalMatcher(prt->portals[1].winding, p0))));

    CHECK(prt->portalleafs == 3);
    CHECK(prt->portalleafs_real > 3);
}

TEST_CASE("qbsp_angled_brush" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_angled_brush.map");

    REQUIRE(prt.has_value());
    CHECK(GAME_QUAKE == bsp.loadversion->game->id);

    CHECK(1 == bsp.dmodels.size());
    // tilted cuboid floating in a box room, so shared solid leaf + 6 empty leafs around the cube
    CHECK(6 + 1 == bsp.dleafs.size());
}

TEST_CASE("qbsp_sealing_point_entity_on_outside" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_sealing_point_entity_on_outside.map");

    REQUIRE(prt.has_value());
}

TEST_CASE("q1_sealing_hull1_onnode" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_sealing_hull1_onnode.map");

    const auto player_start_pos = qvec3d(-192, 132, 56);

    INFO("hull0 is empty at the player start");
    CHECK(CONTENTS_EMPTY == BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], player_start_pos));

    INFO("hull1/2 are empty just above the player start");
    CHECK(CONTENTS_EMPTY == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], player_start_pos + qvec3d(0, 0, 1)));
    CHECK(CONTENTS_EMPTY == BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], player_start_pos + qvec3d(0, 0, 1)));

    INFO("hull0/1/2 are solid in the void");
    CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], player_start_pos + qvec3d(0, 0, 1000)));
    CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], player_start_pos + qvec3d(0, 0, 1000)));
    CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], player_start_pos + qvec3d(0, 0, 1000)));
}

TEST_CASE("q1_0125unit_faces" * doctest::test_suite("testmaps_q1") * doctest::may_fail())
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_0125unit_faces.map");

    CHECK(bsp.loadversion == &bspver_q1);
    CHECK(2 == bsp.dfaces.size());
}

TEST_CASE("quake maps" * doctest::test_suite("testmaps_q1") * doctest::skip())
{
    const std::vector<std::string> quake_maps{"DM1-test.map", "DM2-test.map", "DM3-test.map", "DM4-test.map",
        "DM5-test.map", "DM6-test.map", "DM7-test.map", "E1M1-test.map", "E1M2-test.map", "E1M3-test.map",
        "E1M4-test.map", "E1M5-test.map", "E1M6-test.map", "E1M7-test.map", "E1M8-test.map", "E2M1-test.map",
        "E2M2-test.map", "E2M3-test.map", "E2M4-test.map", "E2M5-test.map", "E2M6-test.map", "E2M7-test.map",
        "E3M1-test.map", "E3M2-test.map", "E3M3-test.map", "E3M4-test.map", "E3M5-test.map", "E3M6-test.map",
        "E3M7-test.map", "E4M1-test.map", "E4M2-test.map", "E4M3-test.map", "E4M4-test.map", "E4M5-test.map",
        "E4M6-test.map", "E4M7-test.map", "E4M8-test.map", "END-test.map"};

    for (const auto &map : quake_maps) {
        SUBCASE(map.c_str())
        {
            const auto [bsp, bspx, prt] = LoadTestmapQ1("quake_map_source/" + map);

            CHECK(GAME_QUAKE == bsp.loadversion->game->id);
            CHECK(prt);
            CheckFilled(bsp);
        }
    }
}

TEST_CASE("chop" * doctest::test_suite("testmaps_q1") * doctest::skip())
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("quake_map_source/DM1-test.map", {"-chop", "-debugchop"});

    CHECK(GAME_QUAKE == bsp.loadversion->game->id);
    CHECK(prt);
    CheckFilled(bsp);
}

TEST_CASE("mountain" * doctest::test_suite("testmaps_q1") * doctest::skip() * doctest::may_fail())
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_mountain.map");

    CHECK(GAME_QUAKE == bsp.loadversion->game->id);
    CHECK(prt);
    CheckFilled(bsp);
}

/**
 * Q1 sealing test:
 * - hull0 can use Q2 method (fill inside)
 * - hull1+ can't, because it would cause areas containing no entities but connected by a thin gap to the
 *   rest of the world to get sealed off as solid.
 **/
TEST_CASE("q1_sealing" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_sealing.map");

    CHECK(GAME_QUAKE == bsp.loadversion->game->id);

    const qvec3d in_start_room{-192, 144, 104};
    const qvec3d in_emptyroom{-168, 544, 104};
    const qvec3d in_void{-16, -800, 56};
    const qvec3d connected_by_thin_gap{72, 136, 104};

    // check leaf contents in hull 0
    CHECK(CONTENTS_EMPTY == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_start_room)->contents);
    CHECK(CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_emptyroom)
                                ->contents); // can get sealed, since there are no entities
    CHECK(CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_void)->contents);
    CHECK(CONTENTS_EMPTY == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], connected_by_thin_gap)->contents);

    // check leaf contents in hull 1
    CHECK(CONTENTS_EMPTY == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], in_start_room));
    CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], in_emptyroom));
    CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], in_void));
    // ideally this wouldn't get sealed, but we need to do the "inside filling" for compatibility with complex
    // maps using e.g. obj2map geometry, otherwise the clipnodes count explodes
    CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], connected_by_thin_gap));

    // check leaf contents in hull 2
    CHECK(CONTENTS_EMPTY == BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], in_start_room));
    CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], in_emptyroom));
    CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], in_void));
    CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], connected_by_thin_gap));

    CHECK(prt->portals.size() == 2);
    CHECK(prt->portalleafs == 3); // 2 connected rooms + gap (other room is filled in with solid)
    CHECK(prt->portalleafs_real == 3); // no detail, so same as above
}

TEST_CASE("q1_csg" * doctest::test_suite("testmaps_q1"))
{
    auto &entity = LoadMapPath("q1_csg.map");

    REQUIRE(entity.mapbrushes.size() == 2);

    bspbrush_t::container bspbrushes;
    for (int i = 0; i < 2; ++i) {
        auto b = LoadBrush(entity, entity.mapbrushes[i], {CONTENTS_SOLID}, 0, std::nullopt);

        CHECK(6 == b->sides.size());

        bspbrushes.push_back(bspbrush_t::make_ptr(std::move(*b)));
    }

    auto csged = CSGFaces(bspbrushes);
    CHECK(2 == csged.size());

    for (int i = 0; i < 2; ++i) {
        CHECK(5 == csged[i]->sides.size());
    }
}

/**
 * Test for WAD internal textures
 **/
TEST_CASE("q1_wad_internal" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple.map");

    CHECK(GAME_QUAKE == bsp.loadversion->game->id);

    CHECK(bsp.dtex.textures.size() == 4);
    // skip is only here because of the water
    CHECK(bsp.dtex.textures[0].name == "skip");

    CHECK(bsp.dtex.textures[1].name == "orangestuff8");
    CHECK(bsp.dtex.textures[2].name == "*zwater1");
    CHECK(bsp.dtex.textures[3].name == "brown_brick");

    CHECK(!bsp.dtex.textures[1].data.empty());
    CHECK(!bsp.dtex.textures[2].data.empty());
    CHECK(!bsp.dtex.textures[3].data.empty());

    CHECK(img::load_mip("orangestuff8", bsp.dtex.textures[1].data, false, bsp.loadversion->game));
    CHECK(img::load_mip("*zwater1", bsp.dtex.textures[2].data, false, bsp.loadversion->game));
    CHECK(img::load_mip("brown_brick", bsp.dtex.textures[3].data, false, bsp.loadversion->game));
}

/**
 * Test for WAD internal textures
 **/
TEST_CASE("q1_wad_external" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("qbsp_simple.map", {"-xwadpath", std::string(testmaps_dir)});

    CHECK(GAME_QUAKE == bsp.loadversion->game->id);

    CHECK(bsp.dtex.textures.size() == 4);
    // skip is only here because of the water
    CHECK(bsp.dtex.textures[0].name == "skip");

    CHECK(bsp.dtex.textures[1].name == "orangestuff8");
    CHECK(bsp.dtex.textures[2].name == "*zwater1");
    CHECK(bsp.dtex.textures[3].name == "brown_brick");

    CHECK(bsp.dtex.textures[1].data.size() == sizeof(dmiptex_t));
    CHECK(bsp.dtex.textures[2].data.size() == sizeof(dmiptex_t));
    CHECK(bsp.dtex.textures[3].data.size() == sizeof(dmiptex_t));
}

/**
 * Test that we automatically try to load X.wad when compiling X.map
 **/
TEST_CASE("q1_wad_mapname" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_wad_mapname.map");

    CHECK(GAME_QUAKE == bsp.loadversion->game->id);

    CHECK(bsp.dtex.textures.size() == 2);
    CHECK(bsp.dtex.textures[0].name == ""); // skip
    CHECK(bsp.dtex.textures[0].data.size() == 0); // no texture data
    CHECK(bsp.dtex.textures[0].null_texture); // no texture data

    CHECK(bsp.dtex.textures[1].name == "{trigger");
    CHECK(bsp.dtex.textures[1].data.size() > sizeof(dmiptex_t));
}

TEST_CASE("q1_merge_maps" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_merge_maps_base.map", {"-add", "q1_merge_maps_addition.map"});

    CHECK(GAME_QUAKE == bsp.loadversion->game->id);

    // check brushwork from the two maps is merged
    REQUIRE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {5, 0, 16}, {0, 0, 1}));
    REQUIRE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-5, 0, 16}, {0, 0, 1}));

    // check that the worldspawn keys from the base map are used
    auto ents = EntData_Parse(bsp);
    REQUIRE(ents.size() == 3); // worldspawn, info_player_start, func_wall

    REQUIRE(ents[0].get("classname") == "worldspawn");
    CHECK(ents[0].get("message") == "merge maps base");

    // check info_player_start
    auto it = std::find_if(ents.begin(), ents.end(),
        [](const entdict_t &dict) -> bool { return dict.get("classname") == "info_player_start"; });
    REQUIRE(it != ents.end());

    // check func_wall entity from addition map is included
    it = std::find_if(
        ents.begin(), ents.end(), [](const entdict_t &dict) -> bool { return dict.get("classname") == "func_wall"; });
    REQUIRE(it != ents.end());
}

/**
 * Tests that hollow obj2map style geometry (tetrahedrons) get filled in, in all hulls.
 */
TEST_CASE("q1_rocks" * doctest::test_suite("testmaps_q1") * doctest::may_fail())
{
    constexpr auto *q1_rocks_structural_cube = "q1_rocks_structural_cube.map";

    const auto mapnames = {
        "q1_rocks.map", // box room with a func_detail "mountain" of tetrahedrons with a hollow inside
        "q1_rocks_merged.map", // same as above but the mountain has been merged in the .map file into 1 brush
        "q1_rocks_structural.map", // same as q1_rocks.map but without the use of func_detail
        "q1_rocks_structural_merged.map",
        q1_rocks_structural_cube // simpler version where the mountain is just a cube
    };
    for (auto *mapname : mapnames) {
        SUBCASE(mapname)
        {
            const auto [bsp, bspx, prt] = LoadTestmapQ1(mapname);

            CHECK(GAME_QUAKE == bsp.loadversion->game->id);

            const qvec3d point{48, 320, 88};

            CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], point));
            CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], point));
            CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 2, &bsp.dmodels[0], point));

            for (int i = 1; i <= 2; ++i) {
                INFO("hull " << i);

                const auto clipnodes = CountClipnodeLeafsByContentType(bsp, i);

                REQUIRE(clipnodes.size() == 2);
                REQUIRE(clipnodes.find(CONTENTS_SOLID) != clipnodes.end());
                REQUIRE(clipnodes.find(CONTENTS_EMPTY) != clipnodes.end());

                // 6 for the walls of the box, and 1 for the rock structure, which is convex
                CHECK(clipnodes.at(CONTENTS_SOLID) == 7);

                if (std::string(q1_rocks_structural_cube) == mapname) {
                    CHECK((5 + 6) == CountClipnodeNodes(bsp, i));
                }
            }

            // for completion's sake, check the nodes
            if (std::string(q1_rocks_structural_cube) == mapname) {
                CHECK((5 + 6) == bsp.dnodes.size());
            }
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

/**
 * Tests a bad hull expansion
 */
TEST_CASE("q1_hull_expansion_lip" * doctest::test_suite("testmaps_q1") * doctest::may_fail())
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_hull_expansion_lip.map");

    CHECK(GAME_QUAKE == bsp.loadversion->game->id);

    const qvec3d point{174, 308, 42};
    CHECK(CONTENTS_EMPTY == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], point));

    for (int i = 1; i <= 2; ++i) {
        INFO("hull " << i);

        const auto clipnodes = CountClipnodeLeafsByContentType(bsp, i);

        REQUIRE(clipnodes.size() == 2);
        REQUIRE(clipnodes.find(CONTENTS_SOLID) != clipnodes.end());
        REQUIRE(clipnodes.find(CONTENTS_EMPTY) != clipnodes.end());

        // room shaped like:
        //
        // |\    /|
        // | \__/ |
        // |______|
        //
        // 6 solid leafs for the walls/floor, 3 for the empty regions inside
        CHECK(clipnodes.at(CONTENTS_SOLID) == 6);
        CHECK(clipnodes.at(CONTENTS_EMPTY) == 3);

        // 6 walls + 2 floors
        CHECK(CountClipnodeNodes(bsp, i) == 8);
    }
}

TEST_CASE("q1_hull1_content_types" * doctest::test_suite("testmaps_q1"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_hull1_content_types.map");

    CHECK(GAME_QUAKE == bsp.loadversion->game->id);

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
    };

    for (const auto &[point, expected_types] : expected) {
        std::string message = qv::to_string(point);
        CAPTURE(message);

        // hull 0
        auto *hull0_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], point);

        CHECK(expected_types.hull0_contenttype == hull0_leaf->contents);
        ptrdiff_t hull0_leaf_index = hull0_leaf - &bsp.dleafs[0];

        if (expected_types.hull0_leaf == shared_leaf_0) {
            CHECK(hull0_leaf_index == 0);
        } else {
            CHECK(hull0_leaf_index != 0);
        }

        // hull 1
        CHECK(expected_types.hull1_contenttype == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], point));
    }
}

TEST_CASE("BrushFromBounds")
{
    map.reset();
    qbsp_options.reset();
    qbsp_options.worldextent.set_value(1024, settings::source::COMMANDLINE);

    auto brush = BrushFromBounds({{2, 2, 2}, {32, 32, 32}});

    CHECK(brush->sides.size() == 6);

    const auto top_winding = winding_t{{2, 2, 32}, {2, 32, 32}, {32, 32, 32}, {32, 2, 32}};
    const auto bottom_winding = winding_t{{32, 2, 2}, {32, 32, 2}, {2, 32, 2}, {2, 2, 2}};

    int found = 0;

    for (auto &side : brush->sides) {
        CHECK(side.w);

        if (side.w.directional_equal(top_winding)) {
            found++;
            auto &plane = side.get_plane();
            CHECK(plane.get_normal() == qvec3d{0, 0, 1});
            CHECK(plane.get_dist() == 32);
        }

        if (side.w.directional_equal(bottom_winding)) {
            found++;
            auto plane = side.get_plane();
            CHECK(plane.get_normal() == qvec3d{0, 0, -1});
            CHECK(plane.get_dist() == -2);
        }
    }
    CHECK(found == 2);
}

// FIXME: failing because water tjuncs with walls
TEST_CASE("q1_water_subdivision with lit water off" * doctest::may_fail())
{
    INFO("-litwater 0 should suppress water subdivision");

    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_water_subdivision.map", {"-litwater", "0"});

    auto faces = FacesWithTextureName(bsp, "*swater5");
    CHECK(2 == faces.size());

    for (auto *face : faces) {
        auto *texinfo = BSP_GetTexinfo(&bsp, face->texinfo);
        CHECK(texinfo->flags.native == TEX_SPECIAL);
    }
}

TEST_CASE("q1_water_subdivision with defaults")
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_water_subdivision.map");

    auto faces = FacesWithTextureName(bsp, "*swater5");
    CHECK(faces.size() > 2);

    for (auto *face : faces) {
        auto *texinfo = BSP_GetTexinfo(&bsp, face->texinfo);
        CHECK(texinfo->flags.native == 0);
    }
}

TEST_CASE("textures search relative to current directory")
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
    REQUIRE(2 == bsp.dtex.textures.size());
    // FIXME: we shouldn't really be writing skip
    CHECK("" == bsp.dtex.textures[0].name);

    // make sure the texture was written
    CHECK("orangestuff8" == bsp.dtex.textures[1].name);
    CHECK(64 == bsp.dtex.textures[1].width);
    CHECK(64 == bsp.dtex.textures[1].height);
    CHECK(bsp.dtex.textures[1].data.size() > 0);
}

// specifically designed to break the old isHexen2()
// (has 0 faces, and model lump size is divisible by both Q1 and H2 model struct size)
TEST_CASE("q1_skip_only")
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_skip_only.map");

    CHECK(bsp.loadversion == &bspver_q1);
    CHECK(0 == bsp.dfaces.size());
}

// specifically designed to break the old isHexen2()
// (has 0 faces, and model lump size is divisible by both Q1 and H2 model struct size)
TEST_CASE("h2_skip_only")
{
    const auto [bsp, bspx, prt] = LoadTestmap("h2_skip_only.map", {"-hexen2"});

    CHECK(bsp.loadversion == &bspver_h2);
    CHECK(0 == bsp.dfaces.size());
}

TEST_CASE("q1_hull1_fail" * doctest::may_fail())
{
    INFO("weird example of a phantom clip brush in hull1");
    const auto [bsp, bspx, prt] = LoadTestmap("q1_hull1_fail.map");

    {
        INFO("contents at info_player_start");
        CHECK(CONTENTS_EMPTY == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], qvec3d{-2256, -64, 264}));
    }
    {
        INFO("contents at air_bubbles");
        CHECK(CONTENTS_EMPTY == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], qvec3d{-2164, 126, 260}));
    }
    {
        INFO("contents in void");
        CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 0, &bsp.dmodels[0], qvec3d{0, 0, 0}));
        CHECK(CONTENTS_SOLID == BSP_FindContentsAtPoint(&bsp, 1, &bsp.dmodels[0], qvec3d{0, 0, 0}));
    }
}

TEST_CASE("q1_sky_window")
{
    INFO("faces partially covered by sky were getting wrongly merged and deleted");
    const auto [bsp, bspx, prt] = LoadTestmap("q1_sky_window.map");

    {
        INFO("faces around window");
        CHECK(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-184, -252, -32))); // bottom
        CHECK(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-184, -252, 160))); // top
        CHECK(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-184, -288, 60))); // left
        CHECK(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d(-184, -224, 60))); // right
    }
}

TEST_CASE("q1_liquid_software")
{
    INFO("map with just 1 liquid brush + a 'skip' platform, has render corruption on tyrquake");
    const auto [bsp, bspx, prt] = LoadTestmap("q1_liquid_software.map");

    const qvec3d top_face_point{-56, -56, 8};
    const qvec3d side_face_point{-56, -72, -8};

    auto *top = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], top_face_point, {0, 0, 1});
    auto *top_inwater = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], top_face_point, {0, 0, -1});

    auto *side = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], side_face_point, {0, -1, 0});
    auto *side_inwater = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], side_face_point, {0, 1, 0});

    REQUIRE(top);
    REQUIRE(top_inwater);
    REQUIRE(side);
    REQUIRE(side_inwater);

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

    CHECK(7 == outwater_undirected_edges.size());
    CHECK(7 == inwater_undirected_edges.size());

    // make sure there's no reuse between out-of-water and in-water
    for (int e : outwater_undirected_edges) {
        CHECK(inwater_undirected_edges.find(e) == inwater_undirected_edges.end());
    }
}

TEST_CASE("q1_missing_texture")
{
    const auto [bsp, bspx, prt] = LoadTestmap("q1_missing_texture.map");

    REQUIRE(2 == bsp.dtex.textures.size());

    // FIXME: we shouldn't really be writing skip
    // (our test data includes an actual "skip" texture,
    // so that gets included in the bsp.)
    CHECK("skip" == bsp.dtex.textures[0].name);
    CHECK(!bsp.dtex.textures[0].null_texture);
    CHECK(64 == bsp.dtex.textures[0].width);
    CHECK(64 == bsp.dtex.textures[0].height);

    CHECK("" == bsp.dtex.textures[1].name);
    CHECK(bsp.dtex.textures[1].null_texture);

    CHECK(6 == bsp.dfaces.size());
}

TEST_CASE("q1 notex")
{
    const auto [bsp, bspx, prt] = LoadTestmap("q1_cube.map", {"-notex"});

    REQUIRE(2 == bsp.dtex.textures.size());

    {
        // FIXME: we shouldn't really be writing skip
        // (our test data includes an actual "skip" texture,
        // so that gets included in the bsp.)
        auto &t0 = bsp.dtex.textures[0];
        CHECK("skip" == t0.name);
        CHECK(!t0.null_texture);
        CHECK(64 == t0.width);
        CHECK(64 == t0.height);
        CHECK(t0.data.size() == sizeof(dmiptex_t));
        for (int i = 0; i < 4; ++i)
            CHECK(t0.offsets[i] == 0);
    }

    {
        auto &t1 = bsp.dtex.textures[1];
        CHECK("orangestuff8" == t1.name);
        CHECK(!t1.null_texture);
        CHECK(64 == t1.width);
        CHECK(64 == t1.height);
        CHECK(t1.data.size() == sizeof(dmiptex_t));
        for (int i = 0; i < 4; ++i)
            CHECK(t1.offsets[i] == 0);
    }
}

TEST_CASE("hl_basic")
{
    const auto [bsp, bspx, prt] = LoadTestmap("hl_basic.map", {"-hlbsp"});
    CHECK(prt);

    REQUIRE(2 == bsp.dtex.textures.size());

    // FIXME: we shouldn't really be writing skip
    CHECK(bsp.dtex.textures[0].null_texture);

    CHECK("hltest" == bsp.dtex.textures[1].name);
    CHECK(!bsp.dtex.textures[1].null_texture);
    CHECK(64 == bsp.dtex.textures[1].width);
    CHECK(64 == bsp.dtex.textures[1].height);
}
