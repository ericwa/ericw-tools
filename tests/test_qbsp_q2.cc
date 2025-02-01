#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <qbsp/map.hh>
#include <common/bsputils.hh>
#include <common/qvec.hh>

#include <cstring>
#include <set>
#include <stdexcept>
#include <tuple>
#include <map>

#include "test_qbsp.hh"

TEST(testmapsQ2, detail)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_detail.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    // stats
    EXPECT_EQ(1, bsp.dmodels.size());
    // Q2 reserves leaf 0 as an invalid leaf
    const auto &leaf0 = bsp.dleafs[0];
    EXPECT_EQ(Q2_CONTENTS_SOLID, leaf0.contents);
    EXPECT_EQ(-1, leaf0.visofs);
    EXPECT_EQ(qvec3f{}, leaf0.mins);
    EXPECT_EQ(qvec3f{}, leaf0.maxs);
    EXPECT_EQ(0, leaf0.firstmarksurface);
    EXPECT_EQ(0, leaf0.nummarksurfaces);
    EXPECT_EQ(leaf0.ambient_level, (std::array<uint8_t, NUM_AMBIENTS>{0, 0, 0, 0}));
    EXPECT_EQ(CLUSTER_INVALID, leaf0.cluster);
    EXPECT_EQ(AREA_INVALID, leaf0.area);
    EXPECT_EQ(0, leaf0.firstleafbrush);
    EXPECT_EQ(0, leaf0.numleafbrushes);

    // no areaportals except the placeholder
    EXPECT_EQ(1, bsp.dareaportals.size());
    EXPECT_EQ(2, bsp.dareas.size());

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

    EXPECT_EQ(1, counts_by_contents.at(Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL)); // detail leafs
    EXPECT_EQ(8, counts_by_contents.at(0)); // empty leafs
    EXPECT_GE(counts_by_contents.at(Q2_CONTENTS_SOLID), 6);
    EXPECT_LE(counts_by_contents.at(Q2_CONTENTS_SOLID), 12);

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
    const qvec3d under_button{246, 436, 96}; // directly on the main floor plane
    const qvec3d inside_button{246, 436, 98};
    const qvec3d above_button{246, 436, 120};
    const qvec3d beside_button{246, 400, 100}; // should be a different empty leaf than above_button, but same cluster

    // side room (different cluster)
    const qvec3d side_room{138, 576, 140};

    // detail clips away world faces
    EXPECT_EQ(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], under_button, {0, 0, 1}));

    // check for correct contents
    auto *detail_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_button);
    EXPECT_EQ((Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL), detail_leaf->contents);
    EXPECT_EQ(-1, detail_leaf->cluster);
    EXPECT_EQ(0, detail_leaf->area); // solid leafs get the invalid area 0

    // check for button (detail) brush
    EXPECT_EQ(1, Leaf_Brushes(&bsp, detail_leaf).size());
    EXPECT_EQ((Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL), Leaf_Brushes(&bsp, detail_leaf).at(0)->contents);

    // get more leafs
    auto *empty_leaf_above_button = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], above_button);
    EXPECT_EQ(0, empty_leaf_above_button->contents);
    EXPECT_EQ(0, Leaf_Brushes(&bsp, empty_leaf_above_button).size());
    EXPECT_EQ(1, empty_leaf_above_button->area);

    auto *empty_leaf_side_room = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], side_room);
    EXPECT_EQ(0, empty_leaf_side_room->contents);
    EXPECT_EQ(0, Leaf_Brushes(&bsp, empty_leaf_side_room).size());
    EXPECT_NE(empty_leaf_side_room->cluster, empty_leaf_above_button->cluster);
    EXPECT_EQ(1, empty_leaf_side_room->area);

    auto *empty_leaf_beside_button = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], beside_button);
    EXPECT_EQ(0, empty_leaf_beside_button->contents);
    EXPECT_NE(-1, empty_leaf_beside_button->cluster);
    EXPECT_EQ(empty_leaf_above_button->cluster, empty_leaf_beside_button->cluster);
    EXPECT_NE(empty_leaf_above_button, empty_leaf_beside_button);
    EXPECT_EQ(1, empty_leaf_beside_button->area);

    EXPECT_EQ(prt->portals.size(), 5);
    EXPECT_EQ(prt->portalleafs_real, 0); // not used by Q2
    EXPECT_EQ(prt->portalleafs, 4);
}

TEST(testmapsQ2, Q2DetailWithNodetail)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_detail.map", {"-nodetail"});

    const qvec3d inside_button{246, 436, 98};
    auto *inside_button_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_button);
    EXPECT_EQ(Q2_CONTENTS_SOLID, inside_button_leaf->contents);

    EXPECT_GT(prt->portals.size(), 5);
    EXPECT_EQ(prt->portalleafs, 8);
}

TEST(testmapsQ2, Q2DetailWithOmitdetail)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_detail.map", {"-omitdetail"});

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d inside_button{246, 436, 98};
    const qvec3d above_button{246, 436, 120};

    auto *inside_button_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_button);
    EXPECT_EQ(Q2_CONTENTS_EMPTY, inside_button_leaf->contents);

    auto *above_button_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], above_button);
    EXPECT_EQ(inside_button_leaf, above_button_leaf);
}

TEST(testmapsQ2, omitdetailRemovingAllBrushesInAFunc)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_omitdetail_in_func.map", {"-omitdetail"});
}

TEST(testmapsQ2, playerclip)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_playerclip.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d in_playerclip{32, -136, 144};
    auto *playerclip_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_playerclip);
    EXPECT_EQ((Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_DETAIL), playerclip_leaf->contents);

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
    EXPECT_EQ((Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_DETAIL), Leaf_Brushes(&bsp, playerclip_leaf).at(0)->contents);
}

TEST(testmapsQ2, areaportal)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_areaportal.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    // area 0 is a placeholder
    // areaportal 0 is a placeholder
    //
    // the conceptual area portal has portalnum 1, and consists of two dareaportals entries with connections to area 1
    // and 2
    EXPECT_THAT(
        bsp.dareaportals, testing::UnorderedElementsAreArray(std::vector<dareaportal_t>{{0, 0}, {1, 1}, {1, 2}}));
    EXPECT_THAT(bsp.dareas, testing::UnorderedElementsAreArray(std::vector<darea_t>{{0, 0}, {1, 1}, {1, 2}}));

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
    EXPECT_THAT(
        (std::vector<int32_t>{player_start_leaf->area, other_room_leaf->area}), testing::UnorderedElementsAre(1, 2));
    // the areaportal leaf itself actually gets assigned to one of the two sides' areas
    EXPECT_TRUE(areaportal_leaf->area == 1 || areaportal_leaf->area == 2);
    EXPECT_EQ(0, void_leaf->area); // a solid leaf gets the invalid area

    // check the func_areaportal entity had its "style" set
    auto ents = EntData_Parse(bsp);
    auto it = std::find_if(
        ents.begin(), ents.end(), [](const entdict_t &dict) { return dict.get("classname") == "func_areaportal"; });

    ASSERT_NE(it, ents.end());
    ASSERT_EQ("1", it->get("style"));
}

/**
 *  Similar to above test, but there's a detail brush sticking into the area portal
 */
TEST(testmapsQ2, areaportalWithDetail)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_areaportal_with_detail.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    // area 0 is a placeholder
    // areaportal 0 is a placeholder
    //
    // the conceptual area portal has portalnum 1, and consists of two dareaportals entries with connections to area 1
    // and 2
    EXPECT_THAT(
        bsp.dareaportals, testing::UnorderedElementsAreArray(std::vector<dareaportal_t>{{0, 0}, {1, 1}, {1, 2}}));
    EXPECT_THAT(bsp.dareas, testing::UnorderedElementsAreArray(std::vector<darea_t>{{0, 0}, {1, 1}, {1, 2}}));
}

/**
 * same as q2_areaportal.map but has 2 areaportals
 * more clearly shows how areaportal indices work
 *
 *        ap1      ap2
 *
 *  player |  light |   ammo
 *  start  |        | grenades
 *
 *   area     area      area
 *    3         2        1
 *
 *         -- +x -->
 */
TEST(testmapsQ2, areaportals)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_areaportals.map");

    ASSERT_EQ(4, bsp.dareas.size()); // 1 reserved + 3 actual = 4
    ASSERT_EQ(5, bsp.dareaportals.size()); // 1 reserved + (2 portals * 2 directions) = 5

    // check the areaportal numbers from the "style" keys of the func_areaportal entities
    auto ents = EntData_Parse(bsp);

    auto playerstart_portal_it = std::ranges::find_if(
        ents, [](const entdict_t &dict) { return dict.get("targetname") == "playerstart_portal"; });
    auto grenades_portal_it =
        std::ranges::find_if(ents, [](const entdict_t &dict) { return dict.get("targetname") == "grenades_portal"; });

    ASSERT_NE(playerstart_portal_it, ents.end());
    ASSERT_NE(grenades_portal_it, ents.end());

    const int32_t playerstart_portal_num = playerstart_portal_it->get_int("style");
    const int32_t grenades_portal_num = grenades_portal_it->get_int("style");

    // may need to be adjusted
    ASSERT_EQ(1, playerstart_portal_num);
    ASSERT_EQ(2, grenades_portal_num);

    // look up the leafs
    const qvec3d player_start{-88, -112, 120};
    const qvec3d light_pos{72, -136, 168};
    const qvec3d grenades_pos{416, -128, 112};

    auto *player_start_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_start);
    auto *light_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], light_pos);
    auto *grenades_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], grenades_pos);

    // check leaf areas (may need to be adjusted)
    EXPECT_EQ(2, light_leaf->area);
    EXPECT_EQ(3, player_start_leaf->area);
    EXPECT_EQ(1, grenades_leaf->area);

    // inspect player_start_leaf area
    {
        const darea_t &area = bsp.dareas[player_start_leaf->area];
        ASSERT_EQ(area.numareaportals, 1); // to light area

        const dareaportal_t &portal = bsp.dareaportals[area.firstareaportal];
        EXPECT_EQ(portal.otherarea, light_leaf->area);
        EXPECT_EQ(portal.portalnum, playerstart_portal_num);
    }

    // inspect "light" leaf
    {
        const darea_t &area = bsp.dareas[light_leaf->area];
        ASSERT_EQ(area.numareaportals, 2); // to player start, grenades areas

        dareaportal_t portal_x = bsp.dareaportals[area.firstareaportal];
        dareaportal_t portal_y = bsp.dareaportals[area.firstareaportal + 1];

        EXPECT_THAT((std::vector{portal_x, portal_y}),
            testing::UnorderedElementsAre(
                dareaportal_t{.portalnum = playerstart_portal_num, .otherarea = player_start_leaf->area},
                dareaportal_t{.portalnum = grenades_portal_num, .otherarea = grenades_leaf->area}));
    }

    // inspect "grenades" leaf
    {
        const darea_t &area = bsp.dareas[grenades_leaf->area];
        ASSERT_EQ(area.numareaportals, 1); // to light leaf

        dareaportal_t portal = bsp.dareaportals[area.firstareaportal];

        EXPECT_EQ(portal, (dareaportal_t{.portalnum = grenades_portal_num, .otherarea = light_leaf->area}));
    }
}

TEST(testmapsQ2, nodrawLight)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_nodraw_light.map", {"-includeskip"});

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d topface_center{160, -148, 208};
    auto *topface = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], topface_center, {0, 0, 1});
    ASSERT_NE(nullptr, topface);

    auto *texinfo = Face_Texinfo(&bsp, topface);
    EXPECT_EQ(std::string(texinfo->texture.data()), "e1u1/trigger");
    EXPECT_EQ(texinfo->flags.native_q2, (Q2_SURF_LIGHT | Q2_SURF_NODRAW));
}

TEST(testmapsQ2, longTextureName)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_long_texture_name.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    auto *topface = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 16}, {0, 0, 1});
    ASSERT_NE(nullptr, topface);

    // this won't work in game, but we're mostly checking for lack of memory corruption
    // (a warning is issued)
    auto *texinfo = Face_Texinfo(&bsp, topface);
    EXPECT_EQ(std::string(texinfo->texture.data()), "long_folder_name_test/long_text");
    EXPECT_EQ(texinfo->nexttexinfo, -1);
}

TEST(testmapsQ2, base1)
{
    GTEST_SKIP();

    const auto [bsp, bspx, prt] = LoadTestmapQ2("base1-test.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);
    EXPECT_TRUE(prt);
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

    EXPECT_EQ(3, bsp.dareaportals.size());
    EXPECT_EQ(3, bsp.dareas.size());

    // check for a sliver face which we had issues with being missing
    {
        const qvec3d face_point{-315.975, -208.036, -84.5};
        const qvec3d normal_point{-315.851, -208.051, -84.5072}; // obtained in TB

        const qvec3d normal = qv::normalize(normal_point - face_point);

        auto *sliver_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], face_point, normal);
        ASSERT_NE(nullptr, sliver_face);

        EXPECT_EQ(std::string_view("e1u1/metal3_5"), Face_TextureName(&bsp, sliver_face));
        EXPECT_LT(Face_Winding(&bsp, sliver_face).area(), 5.0);
    }
}

TEST(testmapsQ2, base1leak)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("base1leak.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    EXPECT_EQ(8, bsp.dbrushes.size());

    EXPECT_GE(bsp.dleafs.size(), 8); // 1 placeholder + 1 empty (room interior) + 6 solid (sides of room)
    EXPECT_LE(bsp.dleafs.size(), 12); // q2tools-220 generates 12

    const qvec3d in_plus_y_wall{-776, 976, -24};
    auto *plus_y_wall_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_plus_y_wall);
    EXPECT_EQ(Q2_CONTENTS_SOLID, plus_y_wall_leaf->contents);

    EXPECT_EQ(3, plus_y_wall_leaf->numleafbrushes);

    EXPECT_EQ(prt->portals.size(), 0);
    EXPECT_EQ(prt->portalleafs, 1);
}

/**
 * e1u1/brlava brush intersecting e1u1/clip
 **/
TEST(testmapsQ2, lavaclip)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_lavaclip.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    // not touching the lava, but inside the clip
    const qvec3d playerclip_outside1{-88, -32, 8};
    const qvec3d playerclip_outside2{88, -32, 8};

    // inside both clip and lava
    const qvec3d playerclip_inside_lava{0, -32, 8};

    const qvec3d in_lava_only{0, 32, 8};

    // near the player start's feet. There should be a lava face here
    const qvec3d lava_top_face_in_playerclip{0, -32, 16};

    // check leaf contents
    EXPECT_EQ((Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP | Q2_CONTENTS_DETAIL),
        BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], playerclip_outside1)->contents);
    EXPECT_EQ((Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP | Q2_CONTENTS_DETAIL),
        BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], playerclip_outside2)->contents);
    EXPECT_EQ((Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP | Q2_CONTENTS_DETAIL | Q2_CONTENTS_LAVA),
        BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], playerclip_inside_lava)->contents);
    EXPECT_EQ(Q2_CONTENTS_LAVA, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_lava_only)->contents);

    // search for face
    auto *topface = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], lava_top_face_in_playerclip, {0, 0, 1});
    ASSERT_NE(nullptr, topface);

    auto *texinfo = Face_Texinfo(&bsp, topface);
    EXPECT_EQ(std::string(texinfo->texture.data()), "e1u1/brlava");
    EXPECT_EQ(texinfo->flags.native_q2, (Q2_SURF_LIGHT | Q2_SURF_WARP));
}

/**
 * check that e1u1/clip intersecting mist doesn't split up the mist faces
 **/
TEST(testmapsQ2, mistClip)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_mist_clip.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    // mist is two sided, so 12 faces for a cube
    EXPECT_EQ(12, bsp.dfaces.size());
}

/**
 * e1u1/brlava brush intersecting e1u1/brwater
 **/
TEST(testmapsQ2, lavawater)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_lavawater.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d inside_both{0, 32, 8};

    // check leaf contents
    EXPECT_EQ(
        (Q2_CONTENTS_LAVA | Q2_CONTENTS_WATER), BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], inside_both)->contents);
}

/**
 * Weird mystery issue with a func_wall with broken collision
 * (ended up being a PLANE_X/Y/Z plane with negative facing normal, which is illegal - engine assumes they are positive)
 */
TEST(testmapsQ2, bmodelCollision)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_bmodel_collision.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d in_bmodel{-544, -312, -258};
    ASSERT_EQ(2, bsp.dmodels.size());
    EXPECT_EQ(Q2_CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[1], in_bmodel)->contents);
}

TEST(testmapsQ2, liquids)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_liquids.map");

    // water/air face is two sided
    {
        const qvec3d watertrans66_air{-116, -168, 144};
        const qvec3d watertrans33_trans66 = watertrans66_air - qvec3d(0, 0, 48);
        const qvec3d wateropaque_trans33 = watertrans33_trans66 - qvec3d(0, 0, 48);
        const qvec3d floor_wateropaque = wateropaque_trans33 - qvec3d(0, 0, 48);

        EXPECT_THAT(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], watertrans66_air)),
            testing::UnorderedElementsAre("e1u1/bluwter", "e1u1/bluwter"));
        EXPECT_EQ(0, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], watertrans33_trans66).size());
        EXPECT_EQ(0, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], wateropaque_trans33).size());
        EXPECT_THAT(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], floor_wateropaque)),
            testing::UnorderedElementsAre("e1u1/c_met11_2"));
    }

    const qvec3d watertrans66_slimetrans66{-116, -144, 116};

    // water trans66 / slime trans66
    {
        EXPECT_THAT(
            TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], watertrans66_slimetrans66, qvec3d(0, -1, 0))),
            testing::UnorderedElementsAre("e1u1/sewer1"));

        EXPECT_THAT(
            TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], watertrans66_slimetrans66, qvec3d(0, 1, 0))),
            testing::UnorderedElementsAre("e1u1/sewer1"));
    }

    // slime trans66 / lava trans66
    const qvec3d slimetrans66_lavatrans66 = watertrans66_slimetrans66 + qvec3d(0, 48, 0);
    {
        EXPECT_THAT(
            TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], slimetrans66_lavatrans66, qvec3d(0, -1, 0))),
            testing::UnorderedElementsAre("e1u1/brlava"));

        EXPECT_THAT(
            TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], slimetrans66_lavatrans66, qvec3d(0, 1, 0))),
            testing::UnorderedElementsAre("e1u1/brlava"));
    }
}

/**
 * Empty rooms are sealed to solid in Q2
 **/
TEST(testmapsQ2, sealEmptyRooms)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_seal_empty_rooms.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d in_start_room{-240, 80, 56};
    const qvec3d in_empty_room{-244, 476, 68};

    // check leaf contents
    EXPECT_EQ(Q2_CONTENTS_EMPTY, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_start_room)->contents);
    EXPECT_EQ(Q2_CONTENTS_SOLID, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_empty_room)->contents);

    EXPECT_EQ(prt->portals.size(), 0);
    EXPECT_EQ(prt->portalleafs, 1);
}

TEST(testmapsQ2, detailNonSealing)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_detail_non_sealing.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d in_start_room{-240, 80, 56};
    const qvec3d in_void{-336, 80, 56};

    // check leaf contents
    EXPECT_EQ(Q2_CONTENTS_EMPTY, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_start_room)->contents);
    EXPECT_EQ(Q2_CONTENTS_EMPTY, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_void)->contents);
}

TEST(testmapsQ2, detailOverlappingSolidSealing)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_detail_overlapping_solid_sealing.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const qvec3d in_start_room{-240, 80, 56};
    const qvec3d in_void{-336, 80, 56};

    // check leaf contents
    EXPECT_EQ(Q2_CONTENTS_EMPTY, BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_start_room)->contents);
    EXPECT_EQ((Q2_CONTENTS_SOLID & BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_void)->contents), Q2_CONTENTS_SOLID);
}

/**
 * Two areaportals with a small gap in between creating another area.
 *
 * Also, the faces on the ceiling/floor cross the areaportal
 * (due to our aggressive face merging).
 */
TEST(testmapsQ2, doubleAreaportal)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_double_areaportal.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);
    CheckFilled(bsp);

    EXPECT_EQ(4, bsp.dareas.size());
    EXPECT_EQ(5, bsp.dareaportals.size());
}

TEST(testmapsQ2, areaportalSplit)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_areaportal_split.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);
    CheckFilled(bsp);

    EXPECT_EQ(3, bsp.dareas.size()); // 1 invalid index zero reserved + 2 areas
    EXPECT_EQ(
        3, bsp.dareaportals
               .size()); // 1 invalid index zero reserved + 2 dareaportals to store the two directions of the portal
}

/**
 * Test for q2 bmodel bounds
 **/
TEST(testmapsQ2, door)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_door.map");

    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    const aabb3f world_tight_bounds{{-64, -64, -16}, {64, 80, 128}};
    const aabb3f bmodel_tight_bounds{{-48, 48, 16}, {48, 64, 112}};

    EXPECT_EQ(world_tight_bounds.mins(), bsp.dmodels[0].mins);
    EXPECT_EQ(world_tight_bounds.maxs(), bsp.dmodels[0].maxs);

    EXPECT_EQ(bmodel_tight_bounds.mins(), bsp.dmodels[1].mins);
    EXPECT_EQ(bmodel_tight_bounds.maxs(), bsp.dmodels[1].maxs);
}

TEST(testmapsQ2, mirrorinside)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_mirrorinside.map");

    {
        SCOPED_TRACE("window is not two sided by default");
        const qvec3d window_pos{192, 96, 156};
        EXPECT_THAT(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], window_pos)),
            testing::UnorderedElementsAre("e2u2/wndow1_1"));
    }

    {
        SCOPED_TRACE("aux is not two sided by default");
        const qvec3d aux_pos{32, 96, 156};
        EXPECT_THAT(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], aux_pos)),
            testing::UnorderedElementsAre("e1u1/brwater"));
    }

    {
        SCOPED_TRACE("mist is two sided by default");
        const qvec3d mist_pos{32, -28, 156};
        EXPECT_THAT(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], mist_pos)),
            testing::UnorderedElementsAre("e1u1/brwater", "e1u1/brwater"));
    }

    {
        SCOPED_TRACE("_mirrorinside 0 disables the inside faces on mist");
        const qvec3d mist_mirrorinside0_pos{32, -224, 156};
        EXPECT_THAT(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], mist_mirrorinside0_pos)),
            testing::UnorderedElementsAre("e1u1/brwater"));
    }

    {
        SCOPED_TRACE("_mirrorinside 1 works on func_detail_fence");
        EXPECT_THAT(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], {32, -348, 156})),
            testing::UnorderedElementsAre("e1u1/alphamask", "e1u1/alphamask"));
    }
}

TEST(testmapsQ2, alphatestWindow)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_alphatest_window.map");

    SCOPED_TRACE("alphatest + window implies detail and translucent");
    auto *leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0});

    EXPECT_EQ(leaf->contents, (Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW | Q2_CONTENTS_TRANSLUCENT));
}

TEST(testmapsQ2, alphatestSolid)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_alphatest_solid.map");

    SCOPED_TRACE("alphatest + solid implies window, detail and translucent");
    auto *leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0});

    EXPECT_EQ(leaf->contents, (Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW | Q2_CONTENTS_TRANSLUCENT));
}

TEST(testmapsQ2, trans33Window)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_trans33_window.map");

    SCOPED_TRACE("trans33 + window implies detail and translucent");
    auto *leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0});

    EXPECT_EQ(leaf->contents, (Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW | Q2_CONTENTS_TRANSLUCENT));
}

TEST(testmapsQ2, trans33Solid)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_trans33_solid.map");

    SCOPED_TRACE("trans33 + solid implies window, detail and translucent");
    auto *leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0});

    EXPECT_EQ(leaf->contents, (Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW | Q2_CONTENTS_TRANSLUCENT));
}

/**
 * Ensure that leaked maps still get areas assigned properly
 * (empty leafs should get area 1, solid leafs area 0)
 */
TEST(testmapsQ2, leaked)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_leaked.map");

    EXPECT_FALSE(prt);
    EXPECT_EQ(GAME_QUAKE_II, bsp.loadversion->game->id);

    EXPECT_EQ(bsp.dareaportals.size(), 1);
    EXPECT_EQ(bsp.dareas.size(), 2);
    EXPECT_EQ(bsp.dleafs.size(), 8);
    for (auto &leaf : bsp.dleafs) {
        if (leaf.contents == Q2_CONTENTS_SOLID) {
            EXPECT_EQ(0, leaf.area);
        } else {
            EXPECT_EQ(1, leaf.area);
        }
    }
}

TEST(testmapsQ2, missingFaces)
{
    GTEST_SKIP();

    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_missing_faces.map");

    const qvec3d point_on_missing_face{-137, 125, -76.1593};
    const qvec3d point_on_missing_face2{-30, 12, -75.6411};
    const qvec3d point_on_present_face{-137, 133, -76.6997};

    CheckFilled(bsp);
    EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], point_on_missing_face));
    EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], point_on_missing_face2));
    EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], point_on_present_face));
}

TEST(testmapsQ2, ladder)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_ladder.map");

    const qvec3d point_in_ladder{-8, 184, 24};

    CheckFilled(bsp);

    auto *leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], point_in_ladder);

    // the brush lacked a visible contents, so it became solid.
    // ladder and detail flags are preseved now.
    // (previously we were wiping them out and just writing out leafs as Q2_CONTENTS_SOLID).
    EXPECT_EQ(leaf->contents, (Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER | Q2_CONTENTS_DETAIL));

    EXPECT_EQ(1, Leaf_Brushes(&bsp, leaf).size());
    EXPECT_EQ((Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER | Q2_CONTENTS_DETAIL), Leaf_Brushes(&bsp, leaf).at(0)->contents);
}

TEST(testmapsQ2, hintMissingFaces)
{
    GTEST_SKIP();

    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_hint_missing_faces.map");

    EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {36, 144, 30}));
}

TEST(testmapsQ2, tbCleanup)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_tb_cleanup.map");

    {
        SCOPED_TRACE("check that __TB_empty was not converted to skip");
        EXPECT_NE(nullptr, BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}));
    }

    {
        auto ents = EntData_Parse(bsp);

        ASSERT_EQ(ents.size(), 2);
        SCOPED_TRACE("check that _tb_textures was stripped out");
        EXPECT_EQ((entdict_t{{"classname", "worldspawn"}}), ents[0]);
    }
}

TEST(testmapsQ2, detailWall)
{
    // q2_detail_wall_with_detail_bit.map has the DETAIL content flag set on the
    // brushes inside the func_detail_wall. the func_detail_wall should take priority.
    const std::vector<std::string> maps{"q2_detail_wall.map", "q2_detail_wall_with_detail_bit.map"};

    for (const auto &mapname : maps) {
        SCOPED_TRACE(mapname);

        const auto [bsp, bspx, prt] = LoadTestmapQ2(mapname);
        auto *game = bsp.loadversion->game;

        EXPECT_EQ(GAME_QUAKE_II, game->id);

        const auto deleted_face_pos = qvec3d{320, 384, 96};
        const auto in_detail_wall = qvec3d{320, 384, 100};

        auto *detail_wall_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_detail_wall);

        {
            SCOPED_TRACE("check leaf / brush contents");

            SCOPED_TRACE(game->create_contents_from_native(detail_wall_leaf->contents).to_string());
            EXPECT_EQ((Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL), detail_wall_leaf->contents);

            ASSERT_EQ(1, Leaf_Brushes(&bsp, detail_wall_leaf).size());
            auto *brush = Leaf_Brushes(&bsp, detail_wall_leaf).at(0);

            SCOPED_TRACE(game->create_contents_from_native(brush->contents).to_string());
            EXPECT_EQ((Q2_CONTENTS_SOLID | Q2_CONTENTS_DETAIL), brush->contents);
        }

        {
            SCOPED_TRACE("check fully covered face is deleted");
            EXPECT_FALSE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], deleted_face_pos));
        }

        {
            SCOPED_TRACE("check floor under detail fence is not deleted, and not split");

            auto *face_under_fence = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 348, 96});
            auto *face_outside_fence = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 312, 96});

            EXPECT_TRUE(face_under_fence);
            EXPECT_EQ(face_under_fence, face_outside_fence);
        }
    }
}

TEST(testmapsQ2, detailFence)
{
    const std::vector<std::string> maps{"q2_detail_fence.map", "q2_detail_fence_with_detail_bit.map"};

    for (const auto &mapname : maps) {
        SCOPED_TRACE(mapname);

        const auto [bsp, bspx, prt] = LoadTestmapQ2(mapname);
        auto *game = bsp.loadversion->game;

        EXPECT_EQ(GAME_QUAKE_II, game->id);

        auto *detail_wall_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 384, 100});

        {
            SCOPED_TRACE("check leaf / brush contents");
            SCOPED_TRACE(game->create_contents_from_native(detail_wall_leaf->contents).to_string());

            EXPECT_EQ((Q2_CONTENTS_WINDOW | Q2_CONTENTS_DETAIL | Q2_CONTENTS_TRANSLUCENT), detail_wall_leaf->contents);

            ASSERT_EQ(1, Leaf_Brushes(&bsp, detail_wall_leaf).size());
            EXPECT_EQ((Q2_CONTENTS_WINDOW | Q2_CONTENTS_DETAIL | Q2_CONTENTS_TRANSLUCENT),
                Leaf_Brushes(&bsp, detail_wall_leaf).at(0)->contents);
        }

        {
            SCOPED_TRACE("check fully covered face is not deleted");
            EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 384, 96}));
        }

        {
            SCOPED_TRACE("check floor under detail fence is not deleted, and not split");

            auto *face_under_fence = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 348, 96});
            auto *face_outside_fence = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], qvec3d{320, 312, 96});

            EXPECT_TRUE(face_under_fence);
            EXPECT_EQ(face_under_fence, face_outside_fence);
        }
    }
}

TEST(testmapsQ2, mistTranswater)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_mist_transwater.map", {"-tjunc", "none"});

    const qvec3d top_of_water = {-216, -16, 352};

    auto up_faces = BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], top_of_water, {0, 0, 1});
    auto down_faces = BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], top_of_water, {0, 0, -1});

    ASSERT_EQ(1, up_faces.size());
    ASSERT_EQ(1, down_faces.size());

    // water has a higher priority (lower content bits are stronger), so it should cut a hole in the mist
    EXPECT_EQ(Face_TextureNameView(&bsp, up_faces[0]), "e1u1/water6");
    EXPECT_EQ(Face_TextureNameView(&bsp, down_faces[0]), "e1u1/water6");

    const auto top_of_water_up = winding_t{{-232, -32, 352}, {-232, 0, 352}, {-200, 0, 352}, {-200, -32, 352}};
    const auto top_of_water_dn = top_of_water_up.flip();

    EXPECT_TRUE(Face_Winding(&bsp, up_faces[0]).directional_equal(top_of_water_up));
    EXPECT_TRUE(Face_Winding(&bsp, down_faces[0]).directional_equal(top_of_water_dn));
}

TEST(testmapsQ2, mistAuxImplicitlyDetail)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_mist_aux_implicitly_detail.map", {});

    ASSERT_TRUE(prt);
    EXPECT_EQ(prt->portals.size(), 0);
    EXPECT_EQ(prt->portalleafs, 1);
}

TEST(testmapsQ2, tjuncMatrix)
{
    const auto [b, bspx, prt] = LoadTestmapQ2("q2_tjunc_matrix.map");
    const mbsp_t &bsp = b; // workaround clang not allowing capturing bindings in lambdas
    auto *game = bsp.loadversion->game;

    EXPECT_EQ(GAME_QUAKE_II, game->id);

    const qvec3d face_midpoint_origin{-24, 0, 24};
    const qvec3d face_midpoint_to_tjunc{8, 0, 8};
    const qvec3d z_delta_to_next_face{0, 0, 64};
    const qvec3d x_delta_to_next_face{-64, 0, 0};

    enum index_t : int
    {
        INDEX_DETAIL_WALL = 0,
        INDEX_SOLID,
        INDEX_SOLID_DETAIL,
        INDEX_TRANSPARENT_WATER,
        INDEX_OPAQUE_WATER,
        INDEX_OPAQUE_MIST,
        INDEX_TRANSPARENT_WINDOW,
        INDEX_OPAQUE_AUX,
        INDEX_SKY,
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
        SCOPED_TRACE("INDEX_DETAIL_WALL horizontal");
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_DETAIL_WALL));
        // this one is tricky - the solid cuts a hole in the top
        // that hole (the detail_wall faces) are what weld with the side
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_SOLID));
        // same as INDEX_DETAIL_WALL, INDEX_SOLID
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_SOLID_DETAIL));
        // 2.0.0-alpha9: water welds with everything
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_TRANSPARENT_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_OPAQUE_WATER));
        EXPECT_FALSE(has_tjunc(INDEX_DETAIL_WALL, INDEX_OPAQUE_MIST));
        EXPECT_FALSE(has_tjunc(INDEX_DETAIL_WALL, INDEX_TRANSPARENT_WINDOW));
        EXPECT_FALSE(has_tjunc(INDEX_DETAIL_WALL, INDEX_OPAQUE_AUX));
        // same as INDEX_DETAIL_WALL, INDEX_SOLID
        EXPECT_TRUE(has_tjunc(INDEX_DETAIL_WALL, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_SOLID horizontal - welds with anything opaque except detail_wall");
        EXPECT_FALSE(has_tjunc(INDEX_SOLID, INDEX_DETAIL_WALL));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_SOLID_DETAIL));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_TRANSPARENT_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_OPAQUE_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_OPAQUE_MIST));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID, INDEX_TRANSPARENT_WINDOW));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_OPAQUE_AUX));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_SOLID_DETAIL horizontal - same as INDEX_SOLID");
        EXPECT_FALSE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_DETAIL_WALL));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_SOLID_DETAIL));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_TRANSPARENT_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_OPAQUE_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_OPAQUE_MIST));
        EXPECT_FALSE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_TRANSPARENT_WINDOW));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_OPAQUE_AUX));
        EXPECT_TRUE(has_tjunc(INDEX_SOLID_DETAIL, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_TRANSPARENT_WATER horizontal");
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WATER, INDEX_DETAIL_WALL));
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WATER, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WATER, INDEX_SOLID_DETAIL));
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WATER, INDEX_TRANSPARENT_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WATER, INDEX_OPAQUE_WATER));
        // water is stronger than mist, so cuts away the bottom face of the mist
        // the top face of the water then doesn't need to weld because
        EXPECT_FALSE(has_tjunc(INDEX_TRANSPARENT_WATER, INDEX_OPAQUE_MIST));
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WATER, INDEX_TRANSPARENT_WINDOW));
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WATER, INDEX_OPAQUE_AUX));
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WATER, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_OPAQUE_WATER horizontal");
        // detail wall is stronger than water, so cuts a hole and the water then welds with itself
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_WATER, INDEX_DETAIL_WALL));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_WATER, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_WATER, INDEX_SOLID_DETAIL));
        // welds because opaque water and translucent don't get a face between them
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_WATER, INDEX_TRANSPARENT_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_WATER, INDEX_OPAQUE_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_WATER, INDEX_OPAQUE_MIST));
        // window is stronger and cuts a hole in the water
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_WATER, INDEX_TRANSPARENT_WINDOW));
        // same with aux
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_WATER, INDEX_OPAQUE_AUX));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_WATER, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_OPAQUE_MIST horizontal");
        // detail wall is stronger, cuts mist
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_MIST, INDEX_DETAIL_WALL));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_MIST, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_MIST, INDEX_SOLID_DETAIL));
        // water is stronger, cuts mist
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_MIST, INDEX_TRANSPARENT_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_MIST, INDEX_OPAQUE_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_MIST, INDEX_OPAQUE_MIST));
        // window is stronger, cuts mist
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_MIST, INDEX_TRANSPARENT_WINDOW));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_MIST, INDEX_OPAQUE_AUX));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_MIST, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_TRANSPARENT_WINDOW horizontal");
        // detail wall is stronger than window, cuts a hole in the window, so window
        // tjuncs with itself
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WINDOW, INDEX_DETAIL_WALL));
        // solid cuts a hole in the window
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WINDOW, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WINDOW, INDEX_SOLID_DETAIL));
        // translucent window and translucent water weld
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WINDOW, INDEX_TRANSPARENT_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WINDOW, INDEX_OPAQUE_WATER));
        EXPECT_FALSE(has_tjunc(INDEX_TRANSPARENT_WINDOW, INDEX_OPAQUE_MIST));
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WINDOW, INDEX_TRANSPARENT_WINDOW));
        // note, aux is lower priority than window, so bottom face of aux gets cut away
        EXPECT_FALSE(has_tjunc(INDEX_TRANSPARENT_WINDOW, INDEX_OPAQUE_AUX));
        // sky cuts hole in window
        EXPECT_TRUE(has_tjunc(INDEX_TRANSPARENT_WINDOW, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_OPAQUE_AUX horizontal");
        // detail_wall is higher priority, cuts a hole in aux, which welds with itself
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_AUX, INDEX_DETAIL_WALL));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_AUX, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_AUX, INDEX_SOLID_DETAIL));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_AUX, INDEX_TRANSPARENT_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_AUX, INDEX_OPAQUE_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_AUX, INDEX_OPAQUE_MIST));
        // window is stronger, cuts a hole which causes aux to weld
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_AUX, INDEX_TRANSPARENT_WINDOW));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_AUX, INDEX_OPAQUE_AUX));
        EXPECT_TRUE(has_tjunc(INDEX_OPAQUE_AUX, INDEX_SKY));
    }

    {
        SCOPED_TRACE("INDEX_SKY horizontal - same as INDEX_SOLID");
        EXPECT_FALSE(has_tjunc(INDEX_SKY, INDEX_DETAIL_WALL));
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_SOLID));
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_SOLID_DETAIL));
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_TRANSPARENT_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_OPAQUE_WATER));
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_OPAQUE_MIST));
        EXPECT_FALSE(has_tjunc(INDEX_SKY, INDEX_TRANSPARENT_WINDOW));
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_OPAQUE_AUX));
        EXPECT_TRUE(has_tjunc(INDEX_SKY, INDEX_SKY));
    }
}

TEST(testmapsQ2, unknownContents)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_unknown_contents.map");

    {
        SCOPED_TRACE("leaf with contents 1<<10 which is not a valid contents");

        auto *leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0});

        // FIXME: should the unknown contents get converted to SOLID in the leaf?
        EXPECT_EQ(leaf->contents, (Q2_CONTENTS_SOLID | 1024));

        EXPECT_EQ(1, Leaf_Brushes(&bsp, leaf).size());
        // FIXME: should the unknown contents have SOLID added in the brush?
        EXPECT_EQ((Q2_CONTENTS_SOLID | 1024), Leaf_Brushes(&bsp, leaf).at(0)->contents);
    }

    {
        SCOPED_TRACE("leaf with contents 1<<30 which is not a valid contents");

        auto *leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], {64, 0, 0});

        // FIXME: should the unknown contents get converted to SOLID in the leaf?
        EXPECT_EQ(leaf->contents, (Q2_CONTENTS_SOLID | nth_bit(30)));

        EXPECT_EQ(1, Leaf_Brushes(&bsp, leaf).size());
        // FIXME: should the unknown contents have SOLID added in the brush?
        EXPECT_EQ((Q2_CONTENTS_SOLID | nth_bit(30)), Leaf_Brushes(&bsp, leaf).at(0)->contents);
    }

    {
        SCOPED_TRACE("face with contents 1<<10 which is not a valid surrflags");

        auto *top_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {128, 0, 16}, {0, 0, 1});
        ASSERT_TRUE(top_face);

        auto *texinfo = BSP_GetTexinfo(&bsp, top_face->texinfo);
        ASSERT_TRUE(texinfo);

        EXPECT_EQ(texinfo->flags.native_q2, 1024);
    }
}

TEST(ltfaceQ2, noclipfacesNodraw)
{
    GTEST_SKIP();

    SCOPED_TRACE("when _noclipfaces has a choice of faces, don't use the nodraw one");

    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_noclipfaces_nodraw.map");

    const qvec3d top_of_water = {0, 0, 0};

    auto up_faces = BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], top_of_water, {0, 0, 1});
    auto down_faces = BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], top_of_water, {0, 0, -1});

    ASSERT_EQ(1, up_faces.size());
    ASSERT_EQ(1, down_faces.size());

    EXPECT_EQ(Face_TextureNameView(&bsp, up_faces[0]), "e1u1/water1_8");
    EXPECT_EQ(Face_TextureNameView(&bsp, down_faces[0]), "e1u1/water1_8");
}

TEST(testmapsQ2, chopOrder0)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_chop_order_0.map");

    EXPECT_THAT(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0})),
        testing::UnorderedElementsAre("e1u1/ggrat4_2"));
}

TEST(testmapsQ2, chopOrder1)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ2("q2_chop_order_1.map");

    EXPECT_THAT(TexNames(bsp, BSP_FindFacesAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0})),
        testing::UnorderedElementsAre("e1u1/+0btshoot2"));
}
