#include "test_main.hh"
#include "common/mapfile.hh"

#include "testmaps.hh"
#include "common/settings.hh"

#include <gtest/gtest.h>

#include <common/log.hh>
#include <common/fs.hh>


TEST(maputil, convertQ2QuakeEdToValve)
{
    std::filesystem::path path = std::filesystem::path(testmaps_dir) / "q2_light_sun_mangle.map";
    fs::data data = fs::load(path);
    mapfile::map_file_t map_file = mapfile::parse(data, parser_source_location());

    EXPECT_EQ(4, map_file.entities.size());

    const auto &first_brush = map_file.entities.at(0).brushes.at(0);
    EXPECT_EQ(first_brush.base_format, mapfile::texcoord_style_t::quaked);

    const auto &first_side = first_brush.faces.at(0);
    EXPECT_EQ(std::get<mapfile::texdef_quake_ed_t>(first_side.raw),
        (mapfile::texdef_quake_ed_t{.shift = {0, 32}, .rotate = 0, .scale = {1, 1}}));

    settings::common_settings settings;
    map_file.convert_to(mapfile::texcoord_style_t::valve_220, bspver_q2.game, settings);

    EXPECT_EQ(std::get<mapfile::texdef_valve_t>(first_side.raw),
        (mapfile::texdef_valve_t{.shift = {0, 32},
            .rotate = 0,
            .scale = {1, 1},
            .axis = qmat<double, 2, 3>::row_major({0, 1, 0, 0, 0, -1})}));
}
