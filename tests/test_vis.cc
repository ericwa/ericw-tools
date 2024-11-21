#include <common/bsputils.hh>
#include <common/qvec.hh>

#include <stdexcept>
#include <vis/vis.hh>

#include "test_qbsp.hh"
#include "testutils.hh"

static bool q2_leaf_sees(
    const mbsp_t &bsp, const std::unordered_map<int, std::vector<uint8_t>> &vis, const mleaf_t *a, const mleaf_t *b)
{
    auto &pvs = vis.at(a->cluster);
    return Pvs_LeafVisible(&bsp, pvs, b);
}

static bool q1_leaf_sees(
    const mbsp_t &bsp, const std::unordered_map<int, std::vector<uint8_t>> &vis, const mleaf_t *a, const mleaf_t *b)
{
    auto &pvs = vis.at(a->visofs);
    return Pvs_LeafVisible(&bsp, pvs, b);
}

TEST(vis, detailLeakTest)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_detail_leak_test.map", {}, runvis_t::yes);
    const auto vis = DecompressAllVis(&bsp);

    // points arranged so the items can only see the corrseponding _curve point
    const auto item_enviro = qvec3d(48, 464, 32);
    const auto item_enviro_curve = qvec3d(-64, 848, 56);
    const auto player_start_curve = qvec3d(-64, -432, 56);
    const auto player_start = qvec3d(64, -176, 40);

    auto *item_enviro_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], item_enviro);
    auto *item_enviro_curve_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], item_enviro_curve);
    auto *player_start_curve_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_start_curve);
    auto *player_start_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_start);

    EXPECT_EQ(item_enviro_leaf->contents, 0);
    EXPECT_EQ(item_enviro_curve_leaf->contents, 0);
    EXPECT_EQ(player_start_curve_leaf->contents, 0);
    EXPECT_EQ(player_start_leaf->contents, 0);

    {
        SCOPED_TRACE("check item_enviro_leaf");
        EXPECT_TRUE(q2_leaf_sees(bsp, vis, item_enviro_leaf, item_enviro_curve_leaf));
        EXPECT_FALSE(q2_leaf_sees(bsp, vis, item_enviro_leaf, player_start_curve_leaf));
        EXPECT_FALSE(q2_leaf_sees(bsp, vis, item_enviro_leaf, player_start_leaf));
    }

    {
        SCOPED_TRACE("check player_start_leaf");
        EXPECT_TRUE(q2_leaf_sees(bsp, vis, player_start_leaf, player_start_curve_leaf));
        EXPECT_FALSE(q2_leaf_sees(bsp, vis, player_start_leaf, item_enviro_curve_leaf));
        EXPECT_FALSE(q2_leaf_sees(bsp, vis, player_start_leaf, item_enviro_leaf));
    }
}

TEST(vis, q2FuncIllusionaryVisblocker)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_func_illusionary_visblocker.map", {}, runvis_t::yes);

    // should export a face
    auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {80, 16, 96}, {0, 1, 0});
    ASSERT_TRUE(face);

    const auto vis = DecompressAllVis(&bsp);

    // bsp checks
    EXPECT_EQ(1, bsp.dmodels.size()); // make sure visblocker was merged with world
    EXPECT_EQ(9, bsp.dbrushes.size()); // make sure it emitted a brush

    // check leaf contents
    const auto in_visblocker = qvec3d(0, 0, 32);
    const auto item_enviro = qvec3d(48, 464, 32);
    const auto player_start = qvec3d(64, -176, 40);

    auto *item_enviro_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], item_enviro);
    auto *player_start_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_start);
    auto *in_visblocker_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_visblocker);

    EXPECT_EQ(item_enviro_leaf->contents, 0);
    EXPECT_EQ(player_start_leaf->contents, 0);
    EXPECT_EQ(in_visblocker_leaf->contents, Q2_CONTENTS_MIST);

    // check visdata
    {
        SCOPED_TRACE("check item_enviro_leaf");
        EXPECT_FALSE(q2_leaf_sees(bsp, vis, item_enviro_leaf, player_start_leaf));
        EXPECT_FALSE(q2_leaf_sees(bsp, vis, item_enviro_leaf, in_visblocker_leaf));
    }

    {
        SCOPED_TRACE("check player_start_leaf");
        EXPECT_FALSE(q2_leaf_sees(bsp, vis, player_start_leaf, item_enviro_leaf));
        EXPECT_FALSE(q2_leaf_sees(bsp, vis, player_start_leaf, in_visblocker_leaf));
    }

    // check brushes
    ASSERT_EQ(1, Leaf_Brushes(&bsp, in_visblocker_leaf).size());
    ASSERT_EQ(Q2_CONTENTS_MIST, Leaf_Brushes(&bsp, in_visblocker_leaf).at(0)->contents);
}

TEST(vis, q1FuncIllusionaryVisblocker)
{
    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_func_illusionary_visblocker.map", {}, runvis_t::yes);

    // func_illusionary_visblocker is 2 sided by default
    EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {80, 16, 96}, {0, 1, 0}));
    EXPECT_TRUE(BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {80, 16, 96}, {0, -1, 0}));

    const auto vis = DecompressAllVis(&bsp);

    // bsp checks
    EXPECT_EQ(1, bsp.dmodels.size()); // make sure visblocker was merged with world

    // check leaf contents
    const auto in_visblocker = qvec3d(0, 0, 32);
    const auto item_enviro = qvec3d(48, 464, 32);
    const auto player_start = qvec3d(64, -176, 40);

    auto *item_enviro_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], item_enviro);
    auto *player_start_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], player_start);
    auto *in_visblocker_leaf = BSP_FindLeafAtPoint(&bsp, &bsp.dmodels[0], in_visblocker);

    EXPECT_EQ(item_enviro_leaf->contents, CONTENTS_EMPTY);
    EXPECT_EQ(player_start_leaf->contents, CONTENTS_EMPTY);
    // water brush inside func_illusionary_visblocker gets converted to empty
    EXPECT_EQ(in_visblocker_leaf->contents, CONTENTS_EMPTY);

    // check visdata
    {
        SCOPED_TRACE("check item_enviro_leaf");
        EXPECT_FALSE(q1_leaf_sees(bsp, vis, item_enviro_leaf, player_start_leaf));
        EXPECT_FALSE(q1_leaf_sees(bsp, vis, item_enviro_leaf, in_visblocker_leaf));
    }

    {
        SCOPED_TRACE("check player_start_leaf");
        EXPECT_FALSE(q1_leaf_sees(bsp, vis, player_start_leaf, item_enviro_leaf));
        EXPECT_FALSE(q1_leaf_sees(bsp, vis, player_start_leaf, in_visblocker_leaf));
    }
}

TEST(vis, ClipStackWinding)
{
    pstack_t stack{};
    visstats_t stats{};

    auto *w1 = AllocStackWinding(stack);
    w1->numpoints = 4;
    w1->points[0] = {0, 0, 0};
    w1->points[1] = {32, 0, 0};
    w1->points[2] = {32, 0, -32};
    w1->points[3] = {0, 0, -32};
    w1->set_winding_sphere();

    w1 = ClipStackWinding(stats, w1, stack, qplane3d({-1, 0, 0}, -16));
    EXPECT_EQ(w1->size(), 4);
    EXPECT_EQ((*w1)[0], qvec3d(0, 0, 0));
    EXPECT_EQ((*w1)[1], qvec3d(16, 0, 0));
    EXPECT_EQ((*w1)[2], qvec3d(16, 0, -32));
    EXPECT_EQ((*w1)[3], qvec3d(0, 0, -32));

    FreeStackWinding(w1, stack);
}
