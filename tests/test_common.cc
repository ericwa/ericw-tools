#include <catch2/catch_all.hpp>

#include <filesystem>
#include <string>
#include <common/cmdlib.hh>
#include <common/bspfile.hh>
#include <common/bspfile_q1.hh>
#include <common/bspfile_q2.hh>

TEST_CASE("StripFilename", "[common]")
{
    REQUIRE("/home/foo" == fs::path("/home/foo/bar.txt").parent_path());
    REQUIRE("" == fs::path("bar.txt").parent_path());
}

TEST_CASE("q1 contents", "[common]")
{
    auto* game_q1 = bspver_q1.game;

    const std::array test_contents{
        contentflags_t{CONTENTS_EMPTY},
        contentflags_t{CONTENTS_SOLID},
        contentflags_t{CONTENTS_WATER},
        contentflags_t{CONTENTS_SLIME},
        contentflags_t{CONTENTS_LAVA},
        contentflags_t{CONTENTS_SKY},
    };

    SECTION("solid combined with others"){
        auto solid = game_q1->create_solid_contents();
        CHECK(solid.native == CONTENTS_SOLID);
        CHECK(!solid.game_data.has_value());

        for (const auto &c : test_contents) {
            auto combined = game_q1->combine_contents(solid, c);

            CHECK(combined.native == CONTENTS_SOLID);
            CHECK(combined.is_solid(game_q1));
        }
    }
}

TEST_CASE("q2 contents", "[common]")
{
    const std::array test_contents {
        contentflags_t{Q2_CONTENTS_EMPTY},
        contentflags_t{Q2_CONTENTS_SOLID},
        contentflags_t{Q2_CONTENTS_WINDOW},
        contentflags_t{Q2_CONTENTS_AUX},
        contentflags_t{Q2_CONTENTS_LAVA},
        contentflags_t{Q2_CONTENTS_SLIME},
        contentflags_t{Q2_CONTENTS_WATER},
        contentflags_t{Q2_CONTENTS_MIST},

        contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_SOLID},
        contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_WINDOW},
        contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_AUX},
        contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_LAVA},
        contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_SLIME},
        contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_WATER},
        contentflags_t{Q2_CONTENTS_DETAIL | Q2_CONTENTS_MIST}
    };

    auto* game_q2 = bspver_q2.game;

    SECTION("solid combined with others"){
        auto solid = game_q2->create_solid_contents();
        CHECK(solid.native == Q2_CONTENTS_SOLID);
        CHECK(!solid.game_data.has_value());

        for (const auto &c : test_contents) {
            // solid is treated specially in Q2 and wipes out any other content
            // flags when combined
            auto combined = game_q2->combine_contents(solid, c);

            CHECK(combined.native == Q2_CONTENTS_SOLID);
            CHECK(!combined.game_data.has_value());
            CHECK(combined.is_solid(game_q2));
        }
    }

    SECTION("water combined with others"){
        contentflags_t water{Q2_CONTENTS_WATER};

        for (const auto &c : test_contents) {
            auto combined = game_q2->combine_contents(water, c);
            CHECK(!combined.game_data.has_value());

            DYNAMIC_SECTION("water combined with " << c.to_string(game_q2)) {
                if (!(c.native & Q2_CONTENTS_SOLID)) {
                    CHECK(combined.native == (Q2_CONTENTS_WATER | c.native));
                }
            }
        }
    }
}
