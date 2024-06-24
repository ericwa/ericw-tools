#include <gtest/gtest.h>

#include <filesystem>
#include <common/bspfile.hh>
#include <common/bspfile_q1.hh>
#include <common/bspfile_q2.hh>
#include <common/imglib.hh>
#include <common/settings.hh>
#include <testmaps.hh>

TEST(common, StripFilename)
{
    ASSERT_EQ("/home/foo", fs::path("/home/foo/bar.txt").parent_path());
    ASSERT_EQ("", fs::path("bar.txt").parent_path());
}

TEST(common, q1Contents)
{
    auto *game_q1 = bspver_q1.game;

    const auto solid = game_q1->create_solid_contents();
    const auto detail_solid = game_q1->create_detail_solid_contents(solid);
    const auto detail_wall = game_q1->create_detail_wall_contents(solid);
    const auto detail_fence = game_q1->create_detail_fence_contents(solid);
    const auto detail_illusionary = game_q1->create_detail_illusionary_contents(solid);

    const std::array test_contents{game_q1->create_contents_from_native(CONTENTS_EMPTY),
                                   game_q1->create_contents_from_native(CONTENTS_SOLID),
                                   game_q1->create_contents_from_native(CONTENTS_WATER),
                                   game_q1->create_contents_from_native(CONTENTS_SLIME),
                                   game_q1->create_contents_from_native(CONTENTS_LAVA),
                                   game_q1->create_contents_from_native(CONTENTS_SKY),
                                   detail_solid, detail_wall, detail_fence, detail_illusionary};

    {
        SCOPED_TRACE("solid combined with others");

        EXPECT_EQ(game_q1->contents_to_native(solid), CONTENTS_SOLID);

        for (const auto &c : test_contents) {
            auto combined = game_q1->combine_contents(solid, c);

            EXPECT_EQ(game_q1->contents_to_native(combined), CONTENTS_SOLID);
            EXPECT_TRUE(combined.is_solid(game_q1));

            EXPECT_FALSE(combined.is_any_detail(game_q1));
        }
    }

    {
        SCOPED_TRACE("detail_illusionary plus water");
        auto combined = game_q1->combine_contents(detail_illusionary, game_q1->create_contents_from_native(CONTENTS_WATER));

        EXPECT_EQ(game_q1->contents_to_native(combined), CONTENTS_WATER);
        EXPECT_TRUE(combined.is_detail_illusionary(game_q1));
    }

    {
        SCOPED_TRACE("detail_solid plus water");
        auto combined = game_q1->combine_contents(detail_solid, game_q1->create_contents_from_native(CONTENTS_WATER));

        EXPECT_TRUE(combined.is_any_solid(game_q1));
        EXPECT_TRUE(combined.is_detail_solid(game_q1));
        EXPECT_FALSE(combined.is_liquid(game_q1));
        EXPECT_FALSE(combined.is_solid(game_q1));
    }

    {
        SCOPED_TRACE("detail_solid plus sky");
        auto combined = game_q1->combine_contents(detail_solid, game_q1->create_contents_from_native(CONTENTS_SKY));

        EXPECT_FALSE(combined.is_detail_solid(game_q1));
        EXPECT_TRUE(combined.is_sky(game_q1));
        EXPECT_FALSE(combined.is_solid(game_q1));
    }
}

TEST(common, clusterContents)
{
    for (auto *bspver : bspversions) {
        auto *game = bspver->game;
        if (!game)
            continue;

        {
            SCOPED_TRACE(bspver->name);

            const auto solid = game->create_solid_contents();
            const auto solid_detail = game->create_detail_solid_contents(solid);
            const auto empty = game->create_empty_contents();

            auto solid_solid_cluster = game->cluster_contents(solid_detail, solid_detail);
            SCOPED_TRACE(solid_solid_cluster.to_string(game));
            EXPECT_TRUE(solid_solid_cluster.is_detail_solid(game));

            auto solid_empty_cluster = game->cluster_contents(solid_detail, empty);
            SCOPED_TRACE(solid_empty_cluster.to_string(game));

            // it's empty because of the rule that:
            // - if all leaves in the cluster are solid, it means you can't see in, and there's no visportal
            // - otherwise, you can see in, and it needs a visportal
            EXPECT_TRUE(solid_empty_cluster.is_empty(game));
            // this is a bit weird...
            EXPECT_TRUE(solid_empty_cluster.is_any_detail(game));

            // check portal_can_see_through
            EXPECT_FALSE(game->portal_can_see_through(empty, solid_detail, true));
        }
    }
}

TEST(common, q1Origin)
{
    auto *game = bspver_q1.game;

    auto origin = game->face_get_contents("origin", {}, {});

    EXPECT_TRUE(origin.is_origin(game));
    EXPECT_FALSE(origin.is_empty(game));
}

TEST(common, q2Origin)
{
    auto *game = bspver_q2.game;

    auto origin = game->face_get_contents("", {}, game->create_contents_from_native(Q2_CONTENTS_ORIGIN));

    EXPECT_TRUE(origin.is_origin(game));
    EXPECT_FALSE(origin.is_empty(game));
}

TEST(common, sharedContentFlagTests)
{
    for (auto *bspver : bspversions) {
        auto *game = bspver->game;
        if (!game)
            continue;

        {
            SCOPED_TRACE(bspver->name);

            const auto solid = game->create_solid_contents();
            const auto detail_solid = game->create_detail_solid_contents(solid);
            const auto detail_wall = game->create_detail_wall_contents(solid);
            const auto detail_fence = game->create_detail_fence_contents(solid);
            const auto detail_illusionary = game->create_detail_illusionary_contents(solid);

            SCOPED_TRACE(solid.to_string(game));
            SCOPED_TRACE(detail_solid.to_string(game));
            SCOPED_TRACE(detail_wall.to_string(game));
            SCOPED_TRACE(detail_fence.to_string(game));
            SCOPED_TRACE(detail_illusionary.to_string(game));

            {
                SCOPED_TRACE("is_empty");

                EXPECT_TRUE(game->create_empty_contents().is_empty(game));
                EXPECT_FALSE(solid.is_empty(game));
                EXPECT_FALSE(detail_solid.is_empty(game));
                EXPECT_FALSE(detail_wall.is_empty(game));
                EXPECT_FALSE(detail_fence.is_empty(game));
                EXPECT_FALSE(detail_illusionary.is_empty(game));
            }

            {
                SCOPED_TRACE("is_any_detail");

                EXPECT_FALSE(solid.is_any_detail(game));
                EXPECT_TRUE(detail_solid.is_any_detail(game));
                EXPECT_TRUE(detail_wall.is_any_detail(game));
                EXPECT_TRUE(detail_fence.is_any_detail(game));
                EXPECT_TRUE(detail_illusionary.is_any_detail(game));
            }

            {
                SCOPED_TRACE("is_any_solid");

                EXPECT_TRUE(solid.is_any_solid(game));
                EXPECT_TRUE(detail_solid.is_any_solid(game));
                EXPECT_FALSE(detail_wall.is_any_solid(game));
                EXPECT_FALSE(detail_fence.is_any_solid(game));
                EXPECT_FALSE(detail_illusionary.is_any_solid(game));
            }

            {
                SCOPED_TRACE("is_detail_solid");

                EXPECT_FALSE(solid.is_detail_solid(game));
                EXPECT_TRUE(detail_solid.is_detail_solid(game));
                EXPECT_FALSE(detail_wall.is_detail_solid(game));
                EXPECT_FALSE(detail_fence.is_detail_solid(game));
                EXPECT_FALSE(detail_illusionary.is_detail_solid(game));
            }

            {
                SCOPED_TRACE("is_detail_wall");

                EXPECT_FALSE(solid.is_detail_wall(game));
                EXPECT_FALSE(detail_solid.is_detail_wall(game));
                EXPECT_TRUE(detail_wall.is_detail_wall(game));
                EXPECT_FALSE(detail_fence.is_detail_wall(game));
                EXPECT_FALSE(detail_illusionary.is_detail_wall(game));
            }

            {
                SCOPED_TRACE("is_detail_fence");

                EXPECT_FALSE(solid.is_detail_fence(game));
                EXPECT_FALSE(detail_solid.is_detail_fence(game));
                EXPECT_FALSE(detail_wall.is_detail_fence(game));
                EXPECT_TRUE(detail_fence.is_detail_fence(game));
                EXPECT_FALSE(detail_illusionary.is_detail_fence(game));
            }

            {
                SCOPED_TRACE("is_detail_illusionary");

                EXPECT_FALSE(solid.is_detail_illusionary(game));
                EXPECT_FALSE(detail_solid.is_detail_illusionary(game));
                EXPECT_FALSE(detail_wall.is_detail_illusionary(game));
                EXPECT_FALSE(detail_fence.is_detail_illusionary(game));
                EXPECT_TRUE(detail_illusionary.is_detail_illusionary(game));
            }
        }
    }
}

TEST(common, q2Contents)
{
    auto *game_q2 = bspver_q2.game;

    struct before_after_t {
        int32_t before;
        int32_t after;
    };

    {
        SCOPED_TRACE("solid combined with others");
        const std::vector<before_after_t> before_after_adding_solid {
                {Q2_CONTENTS_EMPTY, Q2_CONTENTS_SOLID},
                {Q2_CONTENTS_SOLID, Q2_CONTENTS_SOLID},
                {Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER, Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER},
                {Q2_CONTENTS_WINDOW, Q2_CONTENTS_SOLID | Q2_CONTENTS_WINDOW},
                {Q2_CONTENTS_AUX, Q2_CONTENTS_SOLID | Q2_CONTENTS_AUX},
                {Q2_CONTENTS_LAVA, Q2_CONTENTS_SOLID | Q2_CONTENTS_LAVA},
                {Q2_CONTENTS_SLIME, Q2_CONTENTS_SOLID | Q2_CONTENTS_SLIME},
                {Q2_CONTENTS_WATER, Q2_CONTENTS_SOLID | Q2_CONTENTS_WATER},
                {Q2_CONTENTS_MIST, Q2_CONTENTS_SOLID | Q2_CONTENTS_MIST},
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_SOLID, Q2_CONTENTS_SOLID}, // detail flag gets erased
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW, Q2_CONTENTS_SOLID | Q2_CONTENTS_WINDOW}, // detail flag gets erased
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_AUX, Q2_CONTENTS_SOLID |Q2_CONTENTS_AUX}, // detail flag gets erased
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_LAVA, Q2_CONTENTS_SOLID |Q2_CONTENTS_LAVA}, // detail flag gets erased
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_SLIME, Q2_CONTENTS_SOLID |Q2_CONTENTS_SLIME}, // detail flag gets erased
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_WATER, Q2_CONTENTS_SOLID | Q2_CONTENTS_WATER}, // detail flag gets erased
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_MIST, Q2_CONTENTS_SOLID | Q2_CONTENTS_MIST} // detail flag gets erased
        };

        auto solid = game_q2->create_solid_contents();
        EXPECT_EQ(game_q2->contents_to_native(solid), Q2_CONTENTS_SOLID);

        for (const auto &[before, after] : before_after_adding_solid) {

            auto combined = game_q2->contents_remap_for_export(
                    game_q2->combine_contents(game_q2->create_contents_from_native(before),
                                              solid), gamedef_t::remap_type_t::leaf);

            EXPECT_EQ(game_q2->contents_to_native(combined), after);
            EXPECT_TRUE(combined.is_solid(game_q2));
            EXPECT_FALSE(combined.is_any_detail(game_q2));
        }
    }

    {
        SCOPED_TRACE("water combined with others");
        contentflags_t water = game_q2->create_contents_from_native(Q2_CONTENTS_WATER);

        const std::vector<before_after_t> before_after_adding_water {
                {Q2_CONTENTS_EMPTY, Q2_CONTENTS_WATER},
                {Q2_CONTENTS_SOLID, Q2_CONTENTS_WATER | Q2_CONTENTS_SOLID},
                {Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER, Q2_CONTENTS_WATER | Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER},
                {Q2_CONTENTS_WINDOW, Q2_CONTENTS_WATER | Q2_CONTENTS_WINDOW},
                {Q2_CONTENTS_AUX, Q2_CONTENTS_WATER | Q2_CONTENTS_AUX},
                {Q2_CONTENTS_LAVA, Q2_CONTENTS_WATER | Q2_CONTENTS_LAVA},
                {Q2_CONTENTS_SLIME, Q2_CONTENTS_WATER | Q2_CONTENTS_SLIME},
                {Q2_CONTENTS_WATER, Q2_CONTENTS_WATER},
                {Q2_CONTENTS_MIST, Q2_CONTENTS_WATER | Q2_CONTENTS_MIST},
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_SOLID, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_SOLID},
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW},
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_AUX, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_AUX},
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_LAVA, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_LAVA},
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_SLIME, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_SLIME},
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_WATER, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL},
                {Q2_CONTENTS_DETAIL | Q2_CONTENTS_MIST, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_MIST}
        };
        for (const auto &[before, after] : before_after_adding_water) {
            auto combined = game_q2->combine_contents(game_q2->create_contents_from_native(before), water);

            {
                SCOPED_TRACE(fmt::format("water combined with {}", game_q2->create_contents_from_native(before).to_string(game_q2)).c_str());
                EXPECT_EQ(game_q2->contents_to_native(combined), after);
            }
        }
    }
}

TEST(common, q1ContentsRoundtrip)
{
    auto *game_q1 = bspver_q1.game;

    for (int i = CONTENTS_EMPTY; i >= CONTENTS_MIN; --i) {
        contentflags_t test_internal = game_q1->create_contents_from_native(i);

        uint32_t test_out = game_q1->contents_to_native(test_internal);

        SCOPED_TRACE(fmt::format("contents {}", i));
        EXPECT_EQ(test_out, i);
    }
}

TEST(common, q2ContentsRoundtrip)
{
    auto *game_q2 = bspver_q2.game;

    EXPECT_EQ(game_q2->contents_to_native(game_q2->create_contents_from_native(0)), 0);

    for (int i = 0; i <= 31; ++i) {
        uint32_t test_in = nth_bit<uint32_t>(i);

        contentflags_t test_internal = game_q2->create_contents_from_native(test_in);

        uint32_t test_out = game_q2->contents_to_native(test_internal);

        SCOPED_TRACE(fmt::format("contents bit {}",  i));
        EXPECT_EQ(test_out, test_in);
    }
}

TEST(common, q2PortalCanSeeThrough)
{
    auto *game_q2 = bspver_q2.game;

    EXPECT_TRUE(game_q2->portal_can_see_through(contentflags_t::make(EWT_VISCONTENTS_DETAIL_WALL | EWT_CFLAG_DETAIL),
                                    contentflags_t::make(EWT_INVISCONTENTS_PLAYERCLIP), false));
}

TEST(imglib, png)
{
    auto *game = bspver_q2.game;
    auto wal_metadata_path = std::filesystem::path(testmaps_dir) / "q2_wal_metadata";

    settings::common_settings settings;
    settings.paths.add_value(wal_metadata_path.string(), settings::source::COMMANDLINE);

    game->init_filesystem("placeholder.map", settings);

    auto [texture, resolve, data] = img::load_texture("e1u1/yellow32x32", false, game, settings);
    ASSERT_TRUE(texture);

    EXPECT_EQ(texture->meta.name, "e1u1/yellow32x32");
    EXPECT_EQ(texture->meta.width, 32);
    EXPECT_EQ(texture->meta.height, 32);
    EXPECT_EQ(texture->meta.extension.value(), img::ext::STB);
    EXPECT_FALSE(texture->meta.color_override);

    EXPECT_EQ(texture->width, 32);
    EXPECT_EQ(texture->height, 32);

    EXPECT_EQ(texture->width_scale, 1);
    EXPECT_EQ(texture->height_scale, 1);
}

TEST(qmat, transpose)
{
    // clang-format off
    auto in = qmat<float, 2, 3>::row_major(
        {1.0, 2.0, 3.0,
         4.0, 5.0, 6.0});
    auto exp = qmat<float, 3, 2>::row_major(
        {1.0, 4.0,
         2.0, 5.0,
         3.0, 6.0});
    // clang-format on

    EXPECT_EQ(in.transpose(), exp);
}
