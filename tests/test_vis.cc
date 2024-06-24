#include <common/bsputils.hh>
#include <common/qvec.hh>

#include <stdexcept>
#include <vis/vis.hh>

#include "test_qbsp.hh"
#include "testutils.hh"

TEST(vis, detailLeakTest)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_detail_leak_test.map", {}, runvis_t::yes);
    const auto vis = DecompressAllVis(&bsp);

    auto leaf_sees = [&](const mleaf_t *a, const mleaf_t *b) -> bool {
        auto &pvs = vis.at(a->cluster);
        return !!(pvs[b->cluster >> 3] & (1 << (b->cluster & 7)));
    };

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
        EXPECT_TRUE(leaf_sees(item_enviro_leaf, item_enviro_curve_leaf));
        EXPECT_FALSE(leaf_sees(item_enviro_leaf, player_start_curve_leaf));
        EXPECT_FALSE(leaf_sees(item_enviro_leaf, player_start_leaf));
    }

    {
        SCOPED_TRACE("check player_start_leaf");
        EXPECT_TRUE(leaf_sees(player_start_leaf, player_start_curve_leaf));
        EXPECT_FALSE(leaf_sees(player_start_leaf, item_enviro_curve_leaf));
        EXPECT_FALSE(leaf_sees(player_start_leaf, item_enviro_leaf));
    }
}

TEST(vis, ClipStackWinding) {
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
