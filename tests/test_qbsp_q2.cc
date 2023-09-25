#include <qbsp/map.hh>
#include <common/bsputils.hh>
#include <common/qvec.hh>

#include <cstring>
#include <set>
#include <stdexcept>
#include <tuple>
#include <map>

#include "test_qbsp.hh"
#include "testutils.hh"

TEST_CASE("detail" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_detail.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    // stats
    CHECK(1 == bsp.dmodels.size());
    // Q2 reserves leaf 0 as an invalid leaf
    const auto &leaf0 = bsp.dleafs[0];
    CHECK(Q2_CONTENTS_SOLID == leaf0.contents);
    CHECK(-1 == leaf0.visofs);
    CHECK(qvec3f{} == leaf0.mins);
    CHECK(qvec3f{} == leaf0.maxs);
    CHECK(0 == leaf0.firstmarksurface);
    CHECK(0 == leaf0.nummarksurfaces);
    CHECK(leaf0.ambient_level == std::array<uint8_t, NUM_AMBIENTS>{0, 0, 0, 0});
    CHECK(CLUSTER_INVALID == leaf0.cluster);
    CHECK(AREA_INVALID == leaf0.area);
    CHECK(0 == leaf0.firstleafbrush);
    CHECK(0 == leaf0.numleafbrushes);

    // no areaportals except the placeholder
    CHECK(1 == bsp.dareaportals.size());
    CHECK(2 == bsp.dareas.size());

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
    CHECK(2 == counts_by_contents.size()); // number of types

    CHECK(counts_by_contents.find(Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL) ==
          counts_by_contents.end()); // the detail bit gets cleared
    CHECK(8 == counts_by_contents.at(0)); // empty leafs
    CHECK(counts_by_contents.at(Q2_CONTENTS_SOLID) >= 8);
    CHECK(counts_by_contents.at(Q2_CONTENTS_SOLID) <= 12);

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
    CHECK(4 == clusters.size());

    // various points in the main room cluster
    const qvec3d under_button{246, 436, 96}; // directly on the main floor plane
    const qvec3d inside_button{246, 436, 98};
    const qvec3d above_button{246, 436, 120};
    const qvec3d beside_button{246, 400, 100}; // should be a different empty leaf than above_button, but same cluster

    // side room (different cluster)
    const qvec3d side_room{138, 576, 140};

    // detail clips away world faces
    CHECK(nullptr == BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], under_button, {0, 0, 1}));

    // check for correct contents
    auto *detail_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_button);
    CHECK(Q2_CONTENTS_SOLID == detail_leaf->contents);
    CHECK(-1 == detail_leaf->cluster);
    CHECK(0 == detail_leaf->area); // solid leafs get the invalid area 0

    // check for button (detail) brush
    CHECK(1 == Leaf_Brushes(&bsp, detail_leaf).size());
    CHECK((Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL) == Leaf_Brushes(&bsp, detail_leaf).at(0)->contents);

    // get more leafs
    auto *empty_leaf_above_button = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], above_button);
    CHECK(0 == empty_leaf_above_button->contents);
    CHECK(0 == Leaf_Brushes(&bsp, empty_leaf_above_button).size());
    CHECK(1 == empty_leaf_above_button->area);

    auto *empty_leaf_side_room = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], side_room);
    CHECK(0 == empty_leaf_side_room->contents);
    CHECK(0 == Leaf_Brushes(&bsp, empty_leaf_side_room).size());
    CHECK(empty_leaf_side_room->cluster != empty_leaf_above_button->cluster);
    CHECK(1 == empty_leaf_side_room->area);

    auto *empty_leaf_beside_button = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], beside_button);
    CHECK(0 == empty_leaf_beside_button->contents);
    CHECK(-1 != empty_leaf_beside_button->cluster);
    CHECK(empty_leaf_above_button->cluster == empty_leaf_beside_button->cluster);
    CHECK(empty_leaf_above_button != empty_leaf_beside_button);
    CHECK(1 == empty_leaf_beside_button->area);

    CHECK(prt->portals.size() == 5);
    CHECK(prt->portalleafs_real == 0); // not used by Q2
    CHECK(prt->portalleafs == 4);
}

TEST_CASE("q2 detail with -nodetail" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_detail.map", {"-nodetail"});

    const qvec3d inside_button{246, 436, 98};
    auto *inside_button_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_button);
    CHECK(Q2_CONTENTS_SOLID == inside_button_leaf->contents);

    CHECK(prt->portals.size() > 5);
    CHECK(prt->portalleafs == 8);
}

TEST_CASE("q2 detail with -omitdetail" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_detail.map", {"-omitdetail"});

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    const qvec3d inside_button{246, 436, 98};
    const qvec3d above_button{246, 436, 120};

    auto *inside_button_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_button);
    CHECK(Q2_CONTENTS_EMPTY == inside_button_leaf->contents);

    auto *above_button_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], above_button);
    CHECK(inside_button_leaf == above_button_leaf);
}

TEST_CASE("-omitdetail removing all brushes in a func" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_omitdetail_in_func.map", {"-omitdetail"});
}

TEST_CASE("playerclip" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_playerclip.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    const qvec3d in_playerclip{32, -136, 144};
    auto *playerclip_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_playerclip);
    CHECK((Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_DETAIL) == playerclip_leaf->contents);

    // make sure faces at these locations aren't clipped away
    const qvec3d floor_under_clip{32, -136, 96};
    const qvec3d pillar_side_in_clip1{32, -48, 144};
    const qvec3d pillar_side_in_clip2{32, -208, 144};

    CHECK(nullptr != BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], floor_under_clip, {0, 0, 1}));
    CHECK(nullptr != BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], pillar_side_in_clip1, {0, -1, 0}));
    CHECK(nullptr != BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], pillar_side_in_clip2, {0, 1, 0}));

    // make sure no face is generated for the playerclip brush
    const qvec3d playerclip_front_face{16, -152, 144};
    CHECK(nullptr == BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], playerclip_front_face, {-1, 0, 0}));

    // check for brush
    CHECK(1 == Leaf_Brushes(&bsp, playerclip_leaf).size());
    CHECK((Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_DETAIL) == Leaf_Brushes(&bsp, playerclip_leaf).at(0)->contents);
}

TEST_CASE("areaportal" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_areaportal.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    // area 0 is a placeholder
    // areaportal 0 is a placeholder
    //
    // the conceptual area portal has portalnum 1, and consists of two dareaportals entries with connections to area 1
    // and 2
    CHECK_VECTORS_UNOREDERED_EQUAL(bsp.dareaportals, std::vector<dareaportal_t>{{0, 0}, {1, 1}, {1, 2}});
    CHECK_VECTORS_UNOREDERED_EQUAL(bsp.dareas, std::vector<darea_t>{{0, 0}, {1, 1}, {1, 2}});

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
    CHECK(0 == player_start_leaf->contents);
    CHECK(0 == other_room_leaf->contents);
    CHECK(Q2_CONTENTS_AREAPORTAL == areaportal_leaf->contents);
    CHECK(Q2_CONTENTS_SOLID == void_leaf->contents);

    // make sure faces at these locations aren't clipped away
    const qvec3d floor_under_areaportal{32, -136, 96};
    CHECK(nullptr != BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], floor_under_areaportal, {0, 0, 1}));

    // check for brushes
    CHECK(1 == Leaf_Brushes(&bsp, areaportal_leaf).size());
    CHECK(Q2_CONTENTS_AREAPORTAL == Leaf_Brushes(&bsp, areaportal_leaf).at(0)->contents);

    CHECK(1 == Leaf_Brushes(&bsp, void_leaf).size());
    CHECK(Q2_CONTENTS_SOLID == Leaf_Brushes(&bsp, void_leaf).at(0)->contents);

    // check leaf areas
    CHECK_VECTORS_UNOREDERED_EQUAL(
        (std::vector<int32_t>{1, 2}), std::vector<int32_t>{player_start_leaf->area, other_room_leaf->area});
    // the areaportal leaf itself actually gets assigned to one of the two sides' areas
    CHECK((areaportal_leaf->area == 1 || areaportal_leaf->area == 2));
    CHECK(0 == void_leaf->area); // a solid leaf gets the invalid area

    // check the func_areaportal entity had its "style" set
    auto ents = EntData_Parse(bsp);
    auto it = std::find_if(
        ents.begin(), ents.end(), [](const entdict_t &dict) { return dict.get("classname") == "func_areaportal"; });

    REQUIRE(it != ents.end());
    REQUIRE("1" == it->get("style"));
}

/**
 *  Similar to above test, but there's a detail brush sticking into the area portal
 */
TEST_CASE("areaportal_with_detail" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_areaportal_with_detail.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    // area 0 is a placeholder
    // areaportal 0 is a placeholder
    //
    // the conceptual area portal has portalnum 1, and consists of two dareaportals entries with connections to area 1
    // and 2
    CHECK_VECTORS_UNOREDERED_EQUAL(bsp.dareaportals, std::vector<dareaportal_t>{{0, 0}, {1, 1}, {1, 2}});
    CHECK_VECTORS_UNOREDERED_EQUAL(bsp.dareas, std::vector<darea_t>{{0, 0}, {1, 1}, {1, 2}});
}

TEST_CASE("nodraw_light" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_nodraw_light.map", {"-includeskip"});

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    const qvec3d topface_center{160, -148, 208};
    auto *topface = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], topface_center, {0, 0, 1});
    REQUIRE(nullptr != topface);

    auto *texinfo = Face_Texinfo(&bsp, topface);
    CHECK(std::string(texinfo->texture.data()) == "e1u1/trigger");
    CHECK(texinfo->flags.native == (Q2_SURF_LIGHT | Q2_SURF_NODRAW));
}

TEST_CASE("q2_long_texture_name" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_long_texture_name.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    auto *topface = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 16}, {0, 0, 1});
    REQUIRE(nullptr != topface);

    // this won't work in game, but we're mostly checking for lack of memory corruption
    // (a warning is issued)
    auto *texinfo = Face_Texinfo(&bsp, topface);
    CHECK(std::string(texinfo->texture.data()) == "long_folder_name_test/long_text");
    CHECK(texinfo->nexttexinfo == -1);
}

TEST_CASE("nodraw_light" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_nodraw_light.map", {"-includeskip"});

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    const qvec3d topface_center{160, -148, 208};
    auto *topface = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], topface_center, {0, 0, 1});
    REQUIRE(nullptr != topface);

    auto *texinfo = Face_Texinfo(&bsp, topface);
    CHECK(std::string(texinfo->texture.data()) == "e1u1/trigger");
    CHECK(texinfo->flags.native == (Q2_SURF_LIGHT | Q2_SURF_NODRAW));
}

TEST_CASE("base1" * doctest::test_suite("testmaps_q2") * doctest::skip())
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("base1-test.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);
    CHECK(prt);
    CheckFilled(bsp);

    // bspinfo output from a compile done with
    // https://github.com/qbism/q2tools-220 at 46fd97bbe1b3657ca9e93227f89aaf0fbd3677c9.
    // only took a couple of seconds (debug build)

    //   35 models
    // 9918 planes           198360
    // 10367 vertexes         124404
    // 5177 nodes            144956
    //  637 texinfos          48412
    // 7645 faces            152900
    // 5213 leafs            145964
    // 9273 leaffaces         18546
    // 7307 leafbrushes       14614
    // 20143 edges             80572
    // 37287 surfedges        149148
    // 1765 brushes           21180
    // 15035 brushsides        60140
    //    3 areas                24
    //    3 areaportals          24
    //      lightdata             0
    //      visdata               0
    //      entdata           53623

    CHECK(3 == bsp.dareaportals.size());
    CHECK(3 == bsp.dareas.size());

    // check for a sliver face which we had issues with being missing
    {
        const qvec3d face_point{-315.975, -208.036, -84.5};
        const qvec3d normal_point{-315.851, -208.051, -84.5072}; // obtained in TB

        const qvec3d normal = qv::normalize(normal_point - face_point);

        auto *sliver_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], face_point, normal);
        REQUIRE(nullptr != sliver_face);

        CHECK(std::string_view("e1u1/metal3_5") == Face_TextureName(&bsp, sliver_face));
        CHECK(Face_Winding(&bsp, sliver_face).area() < 5.0);
    }
}

TEST_CASE("base1leak" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("base1leak.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    CHECK(8 == bsp.dbrushes.size());

    CHECK(bsp.dleafs.size() >= 8); // 1 placeholder + 1 empty (room interior) + 6 solid (sides of room)
    CHECK(bsp.dleafs.size() <= 12); // q2tools-220 generates 12

    const qvec3d in_plus_y_wall{-776, 976, -24};
    auto *plus_y_wall_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_plus_y_wall);
    CHECK(Q2_CONTENTS_SOLID == plus_y_wall_leaf->contents);

    CHECK(3 == plus_y_wall_leaf->numleafbrushes);

    CHECK(prt->portals.size() == 0);
    CHECK(prt->portalleafs == 1);
}

/**
 * e1u1/brlava brush intersecting e1u1/clip
 **/
TEST_CASE("lavaclip" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_lavaclip.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    // not touching the lava, but inside the clip
    const qvec3d playerclip_outside1{-88, -32, 8};
    const qvec3d playerclip_outside2{88, -32, 8};

    // inside both clip and lava
    const qvec3d playerclip_inside_lava{0, -32, 8};

    const qvec3d in_lava_only{0, 32, 8};

    // near the player start's feet. There should be a lava face here
    const qvec3d lava_top_face_in_playerclip{0, -32, 16};

    // check leaf contents
    CHECK((Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP | Q2_CONTENTS_DETAIL) ==
          BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], playerclip_outside1)->contents);
    CHECK((Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP | Q2_CONTENTS_DETAIL) ==
          BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], playerclip_outside2)->contents);
    CHECK((Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP | Q2_CONTENTS_DETAIL | Q2_CONTENTS_LAVA) ==
          BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], playerclip_inside_lava)->contents);
    CHECK(Q2_CONTENTS_LAVA == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_lava_only)->contents);

    // search for face
    auto *topface = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], lava_top_face_in_playerclip, {0, 0, 1});
    REQUIRE(nullptr != topface);

    auto *texinfo = Face_Texinfo(&bsp, topface);
    CHECK(std::string(texinfo->texture.data()) == "e1u1/brlava");
    CHECK(texinfo->flags.native == (Q2_SURF_LIGHT | Q2_SURF_WARP));
}

/**
 * check that e1u1/clip intersecting mist doesn't split up the mist faces
 **/
TEST_CASE("mist_clip" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_mist_clip.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    // mist is two sided, so 12 faces for a cube
    CHECK(12 == bsp.dfaces.size());
}

/**
 * e1u1/brlava brush intersecting e1u1/brwater
 **/
TEST_CASE("lavawater" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_lavawater.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    const qvec3d inside_both{0, 32, 8};

    // check leaf contents
    CHECK((Q2_CONTENTS_LAVA | Q2_CONTENTS_WATER) == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_both)->contents);
}

/**
 * Weird mystery issue with a func_wall with broken collision
 * (ended up being a PLANE_X/Y/Z plane with negative facing normal, which is illegal - engine assumes they are positive)
 */
TEST_CASE("q2_bmodel_collision" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_bmodel_collision.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    const qvec3d in_bmodel{-544, -312, -258};
    REQUIRE(2 == bsp.dmodels.size());
    CHECK(Q2_CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[1], in_bmodel)->contents);
}

TEST_CASE("q2_liquids" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_liquids.map");

    // water/air face is two sided
    {
        const qvec3d watertrans66_air{-116, -168, 144};
        const qvec3d watertrans33_trans66 = watertrans66_air - qvec3d(0, 0, 48);
        const qvec3d wateropaque_trans33 = watertrans33_trans66 - qvec3d(0, 0, 48);
        const qvec3d floor_wateropaque = wateropaque_trans33 - qvec3d(0, 0, 48);

        CHECK_VECTORS_UNOREDERED_EQUAL(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], watertrans66_air)),
            std::vector<std::string>({"e1u1/bluwter", "e1u1/bluwter"}));
        CHECK(0 == BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], watertrans33_trans66).size());
        CHECK(0 == BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], wateropaque_trans33).size());
        CHECK_VECTORS_UNOREDERED_EQUAL(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], floor_wateropaque)),
            std::vector<std::string>({"e1u1/c_met11_2"}));
    }

    const qvec3d watertrans66_slimetrans66{-116, -144, 116};

    // water trans66 / slime trans66
    {
        CHECK_VECTORS_UNOREDERED_EQUAL(
            TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], watertrans66_slimetrans66, qvec3d(0, -1, 0))),
            std::vector<std::string>({"e1u1/sewer1"}));

        CHECK_VECTORS_UNOREDERED_EQUAL(
            TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], watertrans66_slimetrans66, qvec3d(0, 1, 0))),
            std::vector<std::string>({"e1u1/sewer1"}));
    }

    // slime trans66 / lava trans66
    const qvec3d slimetrans66_lavatrans66 = watertrans66_slimetrans66 + qvec3d(0, 48, 0);
    {
        CHECK_VECTORS_UNOREDERED_EQUAL(
            TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], slimetrans66_lavatrans66, qvec3d(0, -1, 0))),
            std::vector<std::string>({"e1u1/brlava"}));

        CHECK_VECTORS_UNOREDERED_EQUAL(
            TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], slimetrans66_lavatrans66, qvec3d(0, 1, 0))),
            std::vector<std::string>({"e1u1/brlava"}));
    }
}

/**
 * Empty rooms are sealed to solid in Q2
 **/
TEST_CASE("q2_seal_empty_rooms" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_seal_empty_rooms.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    const qvec3d in_start_room{-240, 80, 56};
    const qvec3d in_empty_room{-244, 476, 68};

    // check leaf contents
    CHECK(Q2_CONTENTS_EMPTY == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_start_room)->contents);
    CHECK(Q2_CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_empty_room)->contents);

    CHECK(prt->portals.size() == 0);
    CHECK(prt->portalleafs == 1);
}

TEST_CASE("q2_detail_non_sealing" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_detail_non_sealing.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    const qvec3d in_start_room{-240, 80, 56};
    const qvec3d in_void{-336, 80, 56};

    // check leaf contents
    CHECK(Q2_CONTENTS_EMPTY == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_start_room)->contents);
    CHECK(Q2_CONTENTS_EMPTY == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_void)->contents);
}

TEST_CASE("q2_detail_overlapping_solid_sealing" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_detail_overlapping_solid_sealing.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    const qvec3d in_start_room{-240, 80, 56};
    const qvec3d in_void{-336, 80, 56};

    // check leaf contents
    CHECK(Q2_CONTENTS_EMPTY == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_start_room)->contents);
    CHECK(Q2_CONTENTS_SOLID == BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_void)->contents);
}

/**
 * Two areaportals with a small gap in between creating another area.
 *
 * Also, the faces on the ceiling/floor cross the areaportal
 * (due to our aggressive face merging).
 */
TEST_CASE("q2_double_areaportal" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_double_areaportal.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);
    CheckFilled(bsp);

    CHECK(4 == bsp.dareas.size());
    CHECK(5 == bsp.dareaportals.size());
}

TEST_CASE("q2_areaportal_split" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_areaportal_split.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);
    CheckFilled(bsp);

    CHECK(3 == bsp.dareas.size()); // 1 invalid index zero reserved + 2 areas
    CHECK(3 == bsp.dareaportals
                   .size()); // 1 invalid index zero reserved + 2 dareaportals to store the two directions of the portal
}

/**
 * Test for q2 bmodel bounds
 **/
TEST_CASE("q2_door" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_door.map");

    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    const aabb3f world_tight_bounds{{-64, -64, -16}, {64, 80, 128}};
    const aabb3f bmodel_tight_bounds{{-48, 48, 16}, {48, 64, 112}};

    CHECK(world_tight_bounds.mins() == bsp.dmodels[0].mins);
    CHECK(world_tight_bounds.maxs() == bsp.dmodels[0].maxs);

    CHECK(bmodel_tight_bounds.mins() == bsp.dmodels[1].mins);
    CHECK(bmodel_tight_bounds.maxs() == bsp.dmodels[1].maxs);
}

TEST_CASE("q2_mirrorinside" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_mirrorinside.map");

    {
        INFO("window is not two sided by default");
        const qvec3d window_pos{192, 96, 156};
        CHECK_VECTORS_UNOREDERED_EQUAL(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], window_pos)),
            std::vector<std::string>({"e2u2/wndow1_1"}));
    }

    {
        INFO("aux is not two sided by default");
        const qvec3d aux_pos{32, 96, 156};
        CHECK_VECTORS_UNOREDERED_EQUAL(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], aux_pos)),
            std::vector<std::string>({"e1u1/brwater"}));
    }

    {
        INFO("mist is two sided by default");
        const qvec3d mist_pos{32, -28, 156};
        CHECK_VECTORS_UNOREDERED_EQUAL(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], mist_pos)),
            std::vector<std::string>({"e1u1/brwater", "e1u1/brwater"}));
    }

    {
        INFO("_mirrorinside 0 disables the inside faces on mist");
        const qvec3d mist_mirrorinside0_pos{32, -224, 156};
        CHECK_VECTORS_UNOREDERED_EQUAL(
            TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], mist_mirrorinside0_pos)),
            std::vector<std::string>({"e1u1/brwater"}));
    }

    {
        INFO("_mirrorinside 1 works on func_detail_fence");
        CHECK_VECTORS_UNOREDERED_EQUAL(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], {32, -348, 156})),
            std::vector<std::string>({"e1u1/alphamask", "e1u1/alphamask"}));
    }
}

/**
 * Ensure that leaked maps still get areas assigned properly
 * (empty leafs should get area 1, solid leafs area 0)
 */
TEST_CASE("q2_leaked" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_leaked.map");

    CHECK(!prt);
    CHECK(GAME_QUAKE_II == bsp.loadversion->game->id);

    CHECK(bsp.dareaportals.size() == 1);
    CHECK(bsp.dareas.size() == 2);
    CHECK(bsp.dleafs.size() == 8);
    for (auto &leaf : bsp.dleafs) {
        if (leaf.contents == Q2_CONTENTS_SOLID) {
            CHECK(0 == leaf.area);
        } else {
            CHECK(1 == leaf.area);
        }
    }
}

TEST_CASE("q2_missing_faces" * doctest::test_suite("testmaps_q2") * doctest::may_fail())
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_missing_faces.map");

    const qvec3d point_on_missing_face{-137, 125, -76.1593};
    const qvec3d point_on_missing_face2{-30, 12, -75.6411};
    const qvec3d point_on_present_face{-137, 133, -76.6997};

    CheckFilled(bsp);
    CHECK(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], point_on_missing_face));
    CHECK(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], point_on_missing_face2));
    CHECK(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], point_on_present_face));
}

TEST_CASE("q2_ladder" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_ladder.map");

    const qvec3d point_in_ladder{-8, 184, 24};

    CheckFilled(bsp);

    auto *leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], point_in_ladder);

    // the brush lacked a visible contents, so it became solid. solid leafs wipe out any other content bits
    CHECK(leaf->contents == (Q2_CONTENTS_SOLID));

    CHECK(1 == Leaf_Brushes(&bsp, leaf).size());
    CHECK((Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER | Q2_CONTENTS_DETAIL) == Leaf_Brushes(&bsp, leaf).at(0)->contents);
}

TEST_CASE("q2_hint_missing_faces" * doctest::test_suite("testmaps_q2") * doctest::may_fail())
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_hint_missing_faces.map");

    CHECK(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {36, 144, 30}));
}

TEST_CASE("q2_tb_cleanup" * doctest::test_suite("testmaps_q2"))
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_tb_cleanup.map");

    {
        INFO("check that __TB_empty was converted to skip");
        CHECK(nullptr == BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}));
    }

    {
        auto ents = EntData_Parse(bsp);

        REQUIRE(ents.size() == 2);
        INFO("check that _tb_textures was stripped out");
        CHECK(entdict_t{{"classname", "worldspawn"}} == ents[0]);
    }
}

TEST_CASE("q2_detail_wall" * doctest::test_suite("testmaps_q2"))
{
    // q2_detail_wall_with_detail_bit.map has the DETAIL content flag set on the
    // brushes inside the func_detail_wall. the func_detail_wall should take priority.
    const std::vector<std::string> maps{"q2_detail_wall.map", "q2_detail_wall_with_detail_bit.map"};

    for (const auto &mapname : maps) {
        SUBCASE(mapname.c_str())
        {
            const auto [bsp, bspx, prt] = LoadTestmapQ2(mapname);
            auto *game = bsp.loadversion->game;

            CHECK(GAME_QUAKE_II == game->id);

            const auto deleted_face_pos = qvec3d{320, 384, 96};
            const auto in_detail_wall = qvec3d{320, 384, 100};

            auto *detail_wall_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_detail_wall);

            {
                INFO("check leaf / brush contents");

                CAPTURE(contentflags_t{detail_wall_leaf->contents}.to_string(game));
                CHECK(Q2_CONTENTS_SOLID == detail_wall_leaf->contents);

                REQUIRE(1 == Leaf_Brushes(&bsp, detail_wall_leaf).size());
                auto *brush = Leaf_Brushes(&bsp, detail_wall_leaf).at(0);

                CAPTURE(contentflags_t{brush->contents}.to_string(game));
                CHECK(Q2_CONTENTS_SOLID == brush->contents);
            }

            {
                INFO("check fully covered face is deleted");
                CHECK(!BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], deleted_face_pos));
            }

            {
                INFO("check floor under detail fence is not deleted, and not split");

                auto *face_under_fence = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 348, 96});
                auto *face_outside_fence = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 312, 96});

                CHECK(face_under_fence);
                CHECK(face_under_fence == face_outside_fence);
            }
        }
    }
}

TEST_CASE("q2_detail_fence" * doctest::test_suite("testmaps_q2"))
{
    const std::vector<std::string> maps{"q2_detail_fence.map", "q2_detail_fence_with_detail_bit.map"};

    for (const auto &mapname : maps) {
        SUBCASE(mapname.c_str())
        {
            const auto [bsp, bspx, prt] = LoadTestmapQ2(mapname);
            auto *game = bsp.loadversion->game;

            CHECK(GAME_QUAKE_II == game->id);

            auto *detail_wall_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 384, 100});

            {
                INFO("check leaf / brush contents");
                CAPTURE(contentflags_t{detail_wall_leaf->contents}.to_string(game));

                CHECK(
                    (Q2_CONTENTS_WINDOW | Q2_CONTENTS_DETAIL | Q2_CONTENTS_TRANSLUCENT) == detail_wall_leaf->contents);

                REQUIRE(1 == Leaf_Brushes(&bsp, detail_wall_leaf).size());
                CHECK((Q2_CONTENTS_WINDOW | Q2_CONTENTS_DETAIL | Q2_CONTENTS_TRANSLUCENT) ==
                      Leaf_Brushes(&bsp, detail_wall_leaf).at(0)->contents);
            }

            {
                INFO("check fully covered face is not deleted");
                CHECK(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 384, 96}));
            }

            {
                INFO("check floor under detail fence is not deleted, and not split");

                auto *face_under_fence = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 348, 96});
                auto *face_outside_fence = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 312, 96});

                CHECK(face_under_fence);
                CHECK(face_under_fence == face_outside_fence);
            }
        }
    }
}
