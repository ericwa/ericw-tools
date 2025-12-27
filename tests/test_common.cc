#include "common/json.hh"

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

TEST(common, stringIStartsWith)
{
    // true cases
    EXPECT_TRUE(string_istarts_with("asdf", "a"));
    EXPECT_TRUE(string_istarts_with("asdf", "AS"));
    EXPECT_TRUE(string_istarts_with("asdf", "ASDF"));
    EXPECT_TRUE(string_istarts_with("asdf", ""));

    // false cases
    EXPECT_FALSE(string_istarts_with("asdf", "ASt"));
    EXPECT_FALSE(string_istarts_with("asdf", "ASDFX"));
}

TEST(common, q1Contents)
{
    auto *game_q1 = bspver_q1.game;

    const auto solid = contentflags_t::make(EWT_VISCONTENTS_SOLID);
    const auto detail_solid = contentflags_t::create_detail_solid_contents(solid);
    const auto detail_wall = contentflags_t::create_detail_wall_contents(solid);
    const auto detail_fence = contentflags_t::create_detail_fence_contents(solid);
    const auto detail_illusionary = contentflags_t::create_detail_illusionary_contents(solid);

    const std::array test_contents{game_q1->create_contents_from_native(CONTENTS_EMPTY),
        game_q1->create_contents_from_native(CONTENTS_SOLID), game_q1->create_contents_from_native(CONTENTS_WATER),
        game_q1->create_contents_from_native(CONTENTS_SLIME), game_q1->create_contents_from_native(CONTENTS_LAVA),
        game_q1->create_contents_from_native(CONTENTS_SKY), detail_solid, detail_wall, detail_fence,
        detail_illusionary};

    {
        SCOPED_TRACE("solid combined with others");

        EXPECT_EQ(game_q1->contents_to_native(solid), CONTENTS_SOLID);

        for (const auto &c : test_contents) {
            auto combined = contentflags_t::combine_contents(solid, c);

            EXPECT_EQ(game_q1->contents_to_native(combined), CONTENTS_SOLID);
            EXPECT_TRUE(combined.is_solid());

            EXPECT_FALSE(combined.is_any_detail());
        }
    }

    {
        SCOPED_TRACE("detail_illusionary plus water");
        auto combined =
            contentflags_t::combine_contents(detail_illusionary, game_q1->create_contents_from_native(CONTENTS_WATER));

        EXPECT_EQ(game_q1->contents_to_native(combined), CONTENTS_WATER);
        EXPECT_TRUE(combined.is_detail_illusionary());
    }

    {
        SCOPED_TRACE("detail_solid plus water");
        auto combined =
            contentflags_t::combine_contents(detail_solid, game_q1->create_contents_from_native(CONTENTS_WATER));

        EXPECT_TRUE(combined.is_any_solid());
        EXPECT_TRUE(combined.is_detail_solid());
        EXPECT_FALSE(combined.is_liquid());
        EXPECT_FALSE(combined.is_solid());
    }

    {
        SCOPED_TRACE("detail_solid plus sky");
        auto combined =
            contentflags_t::combine_contents(detail_solid, game_q1->create_contents_from_native(CONTENTS_SKY));

        EXPECT_FALSE(combined.is_detail_solid());
        EXPECT_TRUE(combined.is_sky());
        EXPECT_TRUE(combined.is_solid());
    }
}

TEST(common, hlCurrents)
{
    auto *game = bspver_hl.game;

    struct case_t
    {
        const char *texname;
        contents_int_t expected_ewt;
        int expected_hl;
    };

    std::vector<case_t> cases{
        {"!cur_0X", EWT_CFLAG_CURRENT_0 | EWT_VISCONTENTS_WATER, HL_CONTENTS_CURRENT_0},
        {"!cur_90X", EWT_CFLAG_CURRENT_90 | EWT_VISCONTENTS_WATER, HL_CONTENTS_CURRENT_90},
        {"!cur_180X", EWT_CFLAG_CURRENT_180 | EWT_VISCONTENTS_WATER, HL_CONTENTS_CURRENT_180},
        {"!cur_270X", EWT_CFLAG_CURRENT_270 | EWT_VISCONTENTS_WATER, HL_CONTENTS_CURRENT_270},
        {"!cur_upX", EWT_CFLAG_CURRENT_UP | EWT_VISCONTENTS_WATER, HL_CONTENTS_CURRENT_UP},
        {"!cur_dwnX", EWT_CFLAG_CURRENT_DOWN | EWT_VISCONTENTS_WATER, HL_CONTENTS_CURRENT_DOWN},
    };

    for (const case_t &c : cases) {
        // check face_get_contents
        auto case_contents = game->face_get_contents(c.texname, {}, {}, false);
        EXPECT_EQ(case_contents.flags, c.expected_ewt);

        // check EWT -> HL
        EXPECT_EQ(c.expected_hl, game->contents_to_native(case_contents));

        // check HL -> EWT
        EXPECT_EQ(c.expected_ewt, game->create_contents_from_native(c.expected_hl).flags);
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

            const auto solid = contentflags_t::make(EWT_VISCONTENTS_SOLID);
            const auto solid_detail = contentflags_t::create_detail_solid_contents(solid);
            const auto empty = contentflags_t::make(EWT_VISCONTENTS_EMPTY);

            auto solid_solid_cluster = solid_detail.cluster_contents(solid_detail);
            SCOPED_TRACE(solid_solid_cluster.to_string());
            EXPECT_TRUE(solid_solid_cluster.is_detail_solid());

            auto solid_empty_cluster = solid_detail.cluster_contents(empty);
            SCOPED_TRACE(solid_empty_cluster.to_string());

            // it's empty because of the rule that:
            // - if all leaves in the cluster are solid, it means you can't see in, and there's no visportal
            // - otherwise, you can see in, and it needs a visportal
            EXPECT_TRUE(solid_empty_cluster.is_empty());
            // this is a bit weird...
            EXPECT_TRUE(solid_empty_cluster.is_any_detail());

            // check portal_can_see_through
            EXPECT_FALSE(contentflags_t::portal_can_see_through(empty, solid_detail));
        }
    }
}

TEST(common, q1Origin)
{
    auto *game = bspver_q1.game;

    auto origin = game->face_get_contents("origin", {}, {}, false);

    EXPECT_TRUE(origin.is_origin());
    EXPECT_FALSE(origin.is_empty());
}

TEST(common, q2Origin)
{
    auto *game = bspver_q2.game;

    auto origin = game->face_get_contents("", {}, game->create_contents_from_native(Q2_CONTENTS_ORIGIN), false);

    EXPECT_TRUE(origin.is_origin());
    EXPECT_FALSE(origin.is_empty());
}

TEST(common, sharedContentFlagTests)
{
    for (auto *bspver : bspversions) {
        auto *game = bspver->game;
        if (!game)
            continue;

        {
            SCOPED_TRACE(bspver->name);

            const auto solid = contentflags_t::make(EWT_VISCONTENTS_SOLID);
            const auto detail_solid = contentflags_t::create_detail_solid_contents(solid);
            const auto detail_wall = contentflags_t::create_detail_wall_contents(solid);
            const auto detail_fence = contentflags_t::create_detail_fence_contents(solid);
            const auto detail_illusionary = contentflags_t::create_detail_illusionary_contents(solid);

            SCOPED_TRACE(solid.to_string());
            SCOPED_TRACE(detail_solid.to_string());
            SCOPED_TRACE(detail_wall.to_string());
            SCOPED_TRACE(detail_fence.to_string());
            SCOPED_TRACE(detail_illusionary.to_string());

            {
                SCOPED_TRACE("is_empty");

                EXPECT_TRUE(contentflags_t::make(EWT_VISCONTENTS_EMPTY).is_empty());
                EXPECT_FALSE(solid.is_empty());
                EXPECT_FALSE(detail_solid.is_empty());
                EXPECT_FALSE(detail_wall.is_empty());
                EXPECT_FALSE(detail_fence.is_empty());
                EXPECT_FALSE(detail_illusionary.is_empty());
            }

            {
                SCOPED_TRACE("is_any_detail");

                EXPECT_FALSE(solid.is_any_detail());
                EXPECT_TRUE(detail_solid.is_any_detail());
                EXPECT_TRUE(detail_wall.is_any_detail());
                EXPECT_TRUE(detail_fence.is_any_detail());
                EXPECT_TRUE(detail_illusionary.is_any_detail());
            }

            {
                SCOPED_TRACE("is_any_solid");

                EXPECT_TRUE(solid.is_any_solid());
                EXPECT_TRUE(detail_solid.is_any_solid());
                EXPECT_FALSE(detail_wall.is_any_solid());
                EXPECT_FALSE(detail_fence.is_any_solid());
                EXPECT_FALSE(detail_illusionary.is_any_solid());
            }

            {
                SCOPED_TRACE("is_detail_solid");

                EXPECT_FALSE(solid.is_detail_solid());
                EXPECT_TRUE(detail_solid.is_detail_solid());
                EXPECT_FALSE(detail_wall.is_detail_solid());
                EXPECT_FALSE(detail_fence.is_detail_solid());
                EXPECT_FALSE(detail_illusionary.is_detail_solid());
            }

            {
                SCOPED_TRACE("is_detail_wall");

                EXPECT_FALSE(solid.is_detail_wall());
                EXPECT_FALSE(detail_solid.is_detail_wall());
                EXPECT_TRUE(detail_wall.is_detail_wall());
                EXPECT_FALSE(detail_fence.is_detail_wall());
                EXPECT_FALSE(detail_illusionary.is_detail_wall());
            }

            {
                SCOPED_TRACE("is_detail_fence");

                EXPECT_FALSE(solid.is_detail_fence());
                EXPECT_FALSE(detail_solid.is_detail_fence());
                EXPECT_FALSE(detail_wall.is_detail_fence());
                EXPECT_TRUE(detail_fence.is_detail_fence());
                EXPECT_FALSE(detail_illusionary.is_detail_fence());
            }

            {
                SCOPED_TRACE("is_detail_illusionary");

                EXPECT_FALSE(solid.is_detail_illusionary());
                EXPECT_FALSE(detail_solid.is_detail_illusionary());
                EXPECT_FALSE(detail_wall.is_detail_illusionary());
                EXPECT_FALSE(detail_fence.is_detail_illusionary());
                EXPECT_TRUE(detail_illusionary.is_detail_illusionary());
            }
        }
    }
}

TEST(common, q2Contents)
{
    auto *game_q2 = bspver_q2.game;

    struct before_after_t
    {
        int32_t before;
        int32_t after;
    };

    {
        SCOPED_TRACE("solid combined with others");
        const std::vector<before_after_t> before_after_adding_solid{
            {Q2_CONTENTS_EMPTY, Q2_CONTENTS_SOLID}, {Q2_CONTENTS_SOLID, Q2_CONTENTS_SOLID},
            {Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER, Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER},
            {Q2_CONTENTS_WINDOW, Q2_CONTENTS_SOLID | Q2_CONTENTS_WINDOW},
            {Q2_CONTENTS_AUX, Q2_CONTENTS_SOLID | Q2_CONTENTS_AUX},
            {Q2_CONTENTS_LAVA, Q2_CONTENTS_SOLID | Q2_CONTENTS_LAVA},
            {Q2_CONTENTS_SLIME, Q2_CONTENTS_SOLID | Q2_CONTENTS_SLIME},
            {Q2_CONTENTS_WATER, Q2_CONTENTS_SOLID | Q2_CONTENTS_WATER},
            {Q2_CONTENTS_MIST, Q2_CONTENTS_SOLID | Q2_CONTENTS_MIST},
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_SOLID, Q2_CONTENTS_SOLID}, // detail flag gets erased
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW,
                Q2_CONTENTS_SOLID | Q2_CONTENTS_WINDOW}, // detail flag gets erased
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_AUX, Q2_CONTENTS_SOLID | Q2_CONTENTS_AUX}, // detail flag gets erased
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_LAVA, Q2_CONTENTS_SOLID | Q2_CONTENTS_LAVA}, // detail flag gets erased
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_SLIME, Q2_CONTENTS_SOLID | Q2_CONTENTS_SLIME}, // detail flag gets erased
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_WATER, Q2_CONTENTS_SOLID | Q2_CONTENTS_WATER}, // detail flag gets erased
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_MIST, Q2_CONTENTS_SOLID | Q2_CONTENTS_MIST} // detail flag gets erased
        };

        auto solid = contentflags_t::make(EWT_VISCONTENTS_SOLID);
        EXPECT_EQ(game_q2->contents_to_native(solid), Q2_CONTENTS_SOLID);

        for (const auto &[before, after] : before_after_adding_solid) {

            auto combined = game_q2->contents_remap_for_export(
                contentflags_t::combine_contents(game_q2->create_contents_from_native(before), solid),
                gamedef_t::remap_type_t::leaf);

            EXPECT_EQ(game_q2->contents_to_native(combined), after);
            EXPECT_TRUE(combined.is_solid());
            EXPECT_FALSE(combined.is_any_detail());
        }
    }

    {
        SCOPED_TRACE("water combined with others");
        contentflags_t water = game_q2->create_contents_from_native(Q2_CONTENTS_WATER);

        const std::vector<before_after_t> before_after_adding_water{{Q2_CONTENTS_EMPTY, Q2_CONTENTS_WATER},
            {Q2_CONTENTS_SOLID, Q2_CONTENTS_WATER | Q2_CONTENTS_SOLID},
            {Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER, Q2_CONTENTS_WATER | Q2_CONTENTS_SOLID | Q2_CONTENTS_LADDER},
            {Q2_CONTENTS_WINDOW, Q2_CONTENTS_WATER | Q2_CONTENTS_WINDOW},
            {Q2_CONTENTS_AUX, Q2_CONTENTS_WATER | Q2_CONTENTS_AUX},
            {Q2_CONTENTS_LAVA, Q2_CONTENTS_WATER | Q2_CONTENTS_LAVA},
            {Q2_CONTENTS_SLIME, Q2_CONTENTS_WATER | Q2_CONTENTS_SLIME}, {Q2_CONTENTS_WATER, Q2_CONTENTS_WATER},
            {Q2_CONTENTS_MIST, Q2_CONTENTS_WATER | Q2_CONTENTS_MIST},
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_SOLID, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_SOLID},
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW},
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_AUX, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_AUX},
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_LAVA, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_LAVA},
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_SLIME, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_SLIME},
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_WATER, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL},
            {Q2_CONTENTS_DETAIL | Q2_CONTENTS_MIST, Q2_CONTENTS_WATER | Q2_CONTENTS_DETAIL | Q2_CONTENTS_MIST}};
        for (const auto &[before, after] : before_after_adding_water) {
            auto combined = contentflags_t::combine_contents(game_q2->create_contents_from_native(before), water);

            {
                SCOPED_TRACE(
                    fmt::format("water combined with {}", game_q2->create_contents_from_native(before).to_string())
                        .c_str());
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

        SCOPED_TRACE(fmt::format("contents bit {}", i));
        EXPECT_EQ(test_out, test_in);
    }
}

TEST(common, jsonContentsEmpty)
{
    contentflags_t contents{};
    EXPECT_EQ(Json::Value(Json::arrayValue), contents.to_json());

    contentflags_t roundtrip = contentflags_t::from_json(Json::Value(Json::arrayValue));
    EXPECT_EQ(roundtrip, contents);
}

TEST(common, jsonContentsDetailSolid)
{
    contentflags_t contents = contentflags_t::make(EWT_VISCONTENTS_SOLID | EWT_CFLAG_DETAIL | EWT_CFLAG_Q2_UNUSED_31);

    auto expected_json = json_array({"SOLID", "DETAIL", "Q2_UNUSED_31"});
    EXPECT_EQ(expected_json, contents.to_json());

    contentflags_t roundtrip = contentflags_t::from_json(expected_json);
    EXPECT_EQ(roundtrip, contents);
}

TEST(common, q2PortalCanSeeThrough)
{
    auto *game_q2 = bspver_q2.game;

    EXPECT_TRUE(
        contentflags_t::portal_can_see_through(contentflags_t::make(EWT_VISCONTENTS_DETAIL_WALL | EWT_CFLAG_DETAIL),
            contentflags_t::make(EWT_INVISCONTENTS_PLAYERCLIP)));
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

TEST(string, strcasecmp)
{
    EXPECT_EQ('x', Q_tolower('X'));
    EXPECT_EQ('"', Q_tolower('"'));

    const char *test = "abcA**";

    // lhs < rhs
    EXPECT_LT(Q_strcasecmp("a", "aa"), 0);
    EXPECT_LT(Q_strcasecmp("aaa", "BBB"), 0);
    EXPECT_LT(Q_strcasecmp("AAA", "bbb"), 0);

    // lhs == rhs
    EXPECT_EQ(Q_strcasecmp(std::string_view(&test[0], 1), std::string_view(&test[3], 1)), 0);
    EXPECT_EQ(Q_strcasecmp("test", "TEST"), 0);
    EXPECT_EQ(Q_strcasecmp("test", "test"), 0);

    // lhs > rhs
    EXPECT_GT(Q_strcasecmp("test", "aaaa"), 0);
    EXPECT_GT(Q_strcasecmp("test", "AAAA"), 0);
    EXPECT_GT(Q_strcasecmp("test", "tes"), 0);
    EXPECT_GT(Q_strcasecmp("TEST", "T"), 0);
}

TEST(string, strncasecmp)
{
    EXPECT_EQ(Q_strncasecmp("*lava123", "*LAVA", 5), 0);
    EXPECT_EQ(Q_strncasecmp("*lava123", "*LAVA", 8), 1);
}

TEST(surfflags, jsonEmpty)
{
    surfflags_t flags;
    EXPECT_EQ(Json::Value(Json::objectValue), flags.to_json());

    surfflags_t roundtrip = surfflags_t::from_json(Json::Value(Json::objectValue));
    EXPECT_EQ(roundtrip, flags);
}

TEST(surfflags, jsonAllQ2)
{
    surfflags_t flags;
    flags.native_q2 = static_cast<q2_surf_flags_t>(Q2_SURF_ALL);

    Json::Value json = flags.to_json();
    surfflags_t roundtrip = surfflags_t::from_json(json);

    EXPECT_EQ(roundtrip.native_q2, Q2_SURF_ALL);
    EXPECT_EQ(roundtrip, flags);
}

TEST(surfflags, jsonAllQ1)
{
    surfflags_t flags;
    flags.native_q1 = TEX_SPECIAL;

    Json::Value json = flags.to_json();
    surfflags_t roundtrip = surfflags_t::from_json(json);

    EXPECT_EQ(roundtrip.native_q1, TEX_SPECIAL);
    EXPECT_EQ(roundtrip, flags);
}

TEST(surfflags, jsonAllExtended)
{
    surfflags_t flags{.native_q2 = static_cast<q2_surf_flags_t>(Q2_SURF_ALL),
        .native_q1 = TEX_SPECIAL,
        .no_dirt = true,
        .no_shadow = true,
        .no_bounce = true,
        .no_minlight = true,
        .no_expand = true,
        .light_ignore = true,
        .noambient = true,
        .surflight_rescale = std::optional<bool>{true},
        .surflight_style = std::optional<int32_t>{3},
        .surflight_targetname = std::optional<std::string>{"test"},
        .surflight_color = std::optional<qvec3b>{{0, 1, 255}},
        .surflight_minlight_scale = std::optional<float>{0.345f},
        .surflight_atten = std::optional<float>{123.456f},
        .phong_angle = 65.4f,
        .phong_angle_concave = 32.1f,
        .phong_group = 5,
        .minlight = std::optional<float>{3.1f},
        .minlight_color = qvec3b(10, 20, 30),
        .light_alpha = std::optional<float>{2.3f},
        .light_twosided = std::optional<bool>{true},
        .maxlight = 200.4f,
        .lightcolorscale = 1.7,
        .surflight_group = 4,
        .world_units_per_luxel = std::optional<float>{15.0f},
        .object_channel_mask = std::optional<int32_t>{323}};

    Json::Value json = flags.to_json();
    surfflags_t roundtrip = surfflags_t::from_json(json);

    EXPECT_EQ(roundtrip, flags);
}

TEST(surfflags, jsonAllFalse)
{
    surfflags_t flags{};

    Json::Value json = flags.to_json();
    surfflags_t roundtrip = surfflags_t::from_json(json);

    EXPECT_EQ(roundtrip, flags);
}
