#include <doctest/doctest.h>

#include <filesystem>
#include <common/bspfile.hh>
#include <common/bspfile_q1.hh>
#include <common/bspfile_q2.hh>
#include <common/imglib.hh>
#include <common/settings.hh>
#include <testmaps.hh>

TEST_SUITE("common")
{

    TEST_CASE("StripFilename")
    {
        REQUIRE("/home/foo" == fs::path("/home/foo/bar.txt").parent_path());
        REQUIRE("" == fs::path("bar.txt").parent_path());
    }

    TEST_CASE("q1 contents")
    {
        auto *game_q1 = bspver_q1.game;

        const auto solid = game_q1->create_solid_contents();
        const auto detail_solid = game_q1->create_detail_solid_contents(solid);
        const auto detail_wall = game_q1->create_detail_wall_contents(solid);
        const auto detail_fence = game_q1->create_detail_fence_contents(solid);
        const auto detail_illusionary = game_q1->create_detail_illusionary_contents(solid);

        const std::array test_contents{contentflags_t{CONTENTS_EMPTY}, contentflags_t{CONTENTS_SOLID},
            contentflags_t{CONTENTS_WATER}, contentflags_t{CONTENTS_SLIME}, contentflags_t{CONTENTS_LAVA},
            contentflags_t{CONTENTS_SKY},

            detail_solid, detail_wall, detail_fence, detail_illusionary};

        SUBCASE("solid combined with others")
        {
            CHECK(solid.native == CONTENTS_SOLID);
            CHECK(!solid.game_data.has_value());

            for (const auto &c : test_contents) {
                auto combined = game_q1->combine_contents(solid, c);

                CHECK(combined.native == CONTENTS_SOLID);
                CHECK(combined.is_solid(game_q1));

                CHECK(!combined.is_any_detail(game_q1));
            }
        }

        SUBCASE("detail_illusionary plus water")
        {
            auto combined = game_q1->combine_contents(detail_illusionary, contentflags_t{CONTENTS_WATER});

            CHECK(combined.native == CONTENTS_WATER);
            CHECK(combined.is_detail_illusionary(game_q1));
        }

        SUBCASE("detail_solid plus water")
        {
            auto combined = game_q1->combine_contents(detail_solid, contentflags_t{CONTENTS_WATER});

            CHECK(combined.is_any_solid(game_q1));
            CHECK(combined.is_detail_solid(game_q1));
            CHECK(!combined.is_liquid(game_q1));
            CHECK(!combined.is_solid(game_q1));
        }

        SUBCASE("detail_solid plus sky")
        {
            auto combined = game_q1->combine_contents(detail_solid, contentflags_t{CONTENTS_SKY});

            CHECK(!combined.is_detail_solid(game_q1));
            CHECK(combined.is_sky(game_q1));
            CHECK(!combined.is_solid(game_q1));
        }
    }

    TEST_CASE("cluster_contents")
    {
        for (auto *bspver : bspversions) {
            auto *game = bspver->game;
            if (!game)
                continue;

            SUBCASE(bspver->name)
            {
                const auto solid = game->create_solid_contents();
                const auto solid_detail = game->create_detail_solid_contents(solid);
                const auto empty = game->create_empty_contents();

                auto solid_solid_cluster = game->cluster_contents(solid_detail, solid_detail);
                CAPTURE(solid_solid_cluster.to_string(game));
                CHECK(solid_solid_cluster.is_detail_solid(game));

                auto solid_empty_cluster = game->cluster_contents(solid_detail, empty);
                CAPTURE(solid_empty_cluster.to_string(game));

                // it's empty because of the rule that:
                // - if all leaves in the cluster are solid, it means you can't see in, and there's no visportal
                // - otherwise, you can see in, and it needs a visportal
                CHECK(solid_empty_cluster.is_empty(game));
                // this is a bit weird...
                CHECK(solid_empty_cluster.is_any_detail(game));

                // check portal_can_see_through
                CHECK(!game->portal_can_see_through(empty, solid_detail, true, true));
            }
        }
    }

    TEST_CASE("q1 origin")
    {
        auto *game = bspver_q1.game;

        auto origin = game->face_get_contents("origin", {}, {});

        CHECK(origin.is_origin(game));
        CHECK(!origin.is_empty(game));
    }

    TEST_CASE("q2 origin")
    {
        auto *game = bspver_q2.game;

        auto origin = game->face_get_contents("", {}, {Q2_CONTENTS_ORIGIN});

        CHECK(origin.is_origin(game));
        CHECK(!origin.is_empty(game));
    }

    TEST_CASE("shared content flag tests")
    {
        for (auto *bspver : bspversions) {
            auto *game = bspver->game;
            if (!game)
                continue;

            SUBCASE(bspver->name)
            {
                const auto solid = game->create_solid_contents();
                const auto detail_solid = game->create_detail_solid_contents(solid);
                const auto detail_wall = game->create_detail_wall_contents(solid);
                const auto detail_fence = game->create_detail_fence_contents(solid);
                const auto detail_illusionary = game->create_detail_illusionary_contents(solid);

                CAPTURE(solid.to_string(game));
                CAPTURE(detail_solid.to_string(game));
                CAPTURE(detail_wall.to_string(game));
                CAPTURE(detail_fence.to_string(game));
                CAPTURE(detail_illusionary.to_string(game));

                SUBCASE("is_empty")
                {
                    CHECK(game->create_empty_contents().is_empty(game));
                    CHECK(!solid.is_empty(game));
                    CHECK(!detail_solid.is_empty(game));
                    CHECK(!detail_wall.is_empty(game));
                    CHECK(!detail_fence.is_empty(game));
                    CHECK(!detail_illusionary.is_empty(game));
                }

                SUBCASE("is_any_detail")
                {
                    CHECK(!solid.is_any_detail(game));
                    CHECK(detail_solid.is_any_detail(game));
                    CHECK(detail_wall.is_any_detail(game));
                    CHECK(detail_fence.is_any_detail(game));
                    CHECK(detail_illusionary.is_any_detail(game));
                }

                SUBCASE("is_any_solid")
                {
                    CHECK(solid.is_any_solid(game));
                    CHECK(detail_solid.is_any_solid(game));
                    CHECK(!detail_wall.is_any_solid(game));
                    CHECK(!detail_fence.is_any_solid(game));
                    CHECK(!detail_illusionary.is_any_solid(game));
                }

                SUBCASE("is_detail_solid")
                {
                    CHECK(!solid.is_detail_solid(game));
                    CHECK(detail_solid.is_detail_solid(game));
                    CHECK(!detail_wall.is_detail_solid(game));
                    CHECK(!detail_fence.is_detail_solid(game));
                    CHECK(!detail_illusionary.is_detail_solid(game));
                }

                SUBCASE("is_detail_wall")
                {
                    CHECK(!solid.is_detail_wall(game));
                    CHECK(!detail_solid.is_detail_wall(game));
                    CHECK(detail_wall.is_detail_wall(game));
                    CHECK(!detail_fence.is_detail_wall(game));
                    CHECK(!detail_illusionary.is_detail_wall(game));
                }

                SUBCASE("is_detail_fence")
                {
                    CHECK(!solid.is_detail_fence(game));
                    CHECK(!detail_solid.is_detail_fence(game));
                    CHECK(!detail_wall.is_detail_fence(game));
                    CHECK(detail_fence.is_detail_fence(game));
                    CHECK(!detail_illusionary.is_detail_fence(game));
                }

                SUBCASE("is_detail_illusionary")
                {
                    CHECK(!solid.is_detail_illusionary(game));
                    CHECK(!detail_solid.is_detail_illusionary(game));
                    CHECK(!detail_wall.is_detail_illusionary(game));
                    CHECK(!detail_fence.is_detail_illusionary(game));
                    CHECK(detail_illusionary.is_detail_illusionary(game));
                }
            }
        }
    }

    TEST_CASE("q2 contents")
    {
        const std::array test_contents{contentflags_t{Q2_CONTENTS_EMPTY}, contentflags_t{Q2_CONTENTS_SOLID},
            contentflags_t{Q2_CONTENTS_WINDOW}, contentflags_t{Q2_CONTENTS_AUX}, contentflags_t{Q2_CONTENTS_LAVA},
            contentflags_t{Q2_CONTENTS_SLIME}, contentflags_t{Q2_CONTENTS_WATER}, contentflags_t{Q2_CONTENTS_MIST},

            contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_SOLID},
            contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW},
            contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_AUX}, contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_LAVA},
            contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_SLIME},
            contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_WATER},
            contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_MIST}};

        auto *game_q2 = bspver_q2.game;

        SUBCASE("solid combined with others")
        {
            auto solid = game_q2->create_solid_contents();
            CHECK(solid.native == Q2_CONTENTS_SOLID);
            CHECK(!solid.game_data.has_value());

            for (const auto &c : test_contents) {
                // solid is treated specially in Q2 and wipes out any other content
                // flags when combined
                auto combined = game_q2->contents_remap_for_export(
                    game_q2->combine_contents(solid, c), gamedef_t::remap_type_t::leaf);

                CHECK(combined.native == Q2_CONTENTS_SOLID);
                CHECK(!combined.game_data.has_value());
                CHECK(combined.is_solid(game_q2));
            }
        }

        SUBCASE("water combined with others")
        {
            contentflags_t water{Q2_CONTENTS_WATER};

            for (const auto &c : test_contents) {
                auto combined = game_q2->combine_contents(water, c);
                CHECK(!combined.game_data.has_value());

                SUBCASE(fmt::format("water combined with {}", c.to_string(game_q2)).c_str())
                {
                    if (!(c.native & Q2_CONTENTS_SOLID)) {
                        CHECK(combined.native == (Q2_CONTENTS_WATER | c.native));
                    }
                }
            }
        }
    }

    TEST_CASE("imglib png loader")
    {
        auto *game = bspver_q2.game;
        auto wal_metadata_path = std::filesystem::path(testmaps_dir) / "q2_wal_metadata";

        settings::common_settings settings;
        settings.paths.add_value(wal_metadata_path.string(), settings::source::COMMANDLINE);

        game->init_filesystem("placeholder.map", settings);

        auto [texture, resolve, data] = img::load_texture("e1u1/yellow32x32", false, game, settings);
        REQUIRE(texture);

        CHECK(texture->meta.name == "e1u1/yellow32x32");
        CHECK(texture->meta.width == 32);
        CHECK(texture->meta.height == 32);
        CHECK(texture->meta.extension.value() == img::ext::STB);
        CHECK(!texture->meta.color_override);

        CHECK(texture->width == 32);
        CHECK(texture->height == 32);

        CHECK(texture->width_scale == 1);
        CHECK(texture->height_scale == 1);
    }
}

TEST_SUITE("qmat")
{
    TEST_CASE("transpose")
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

        CHECK(in.transpose() == exp);
    }
}
