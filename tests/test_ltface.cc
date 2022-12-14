#include <doctest/doctest.h>

#include <light/light.hh>
#include <common/bspinfo.hh>
#include <qbsp/qbsp.hh>
#include <testmaps.hh>
#include <vis/vis.hh>

struct testresults_t {
    mbsp_t bsp;
    bspxentries_t bspx;
};

enum class runvis_t {
    no, yes
};

static testresults_t LoadTestmap(const std::filesystem::path &name, std::vector<std::string> extra_args,
    runvis_t run_vis = runvis_t::no)
{
    auto map_path = std::filesystem::path(testmaps_dir) / name;

    auto bsp_path = fs::path(test_quake2_maps_dir) / name.filename();
    bsp_path.replace_extension(".bsp");

    auto wal_metadata_path = std::filesystem::path(testmaps_dir) / "q2_wal_metadata";

    std::vector<std::string> args{
        "", // the exe path, which we're ignoring in this case
        "-noverbose",
        "-q2bsp",
        "-path",
        wal_metadata_path.string()
    };
    args.push_back(map_path.string());
    args.push_back(bsp_path.string());

    // run qbsp

    InitQBSP(args);
    ProcessFile();

    // run vis
    if (run_vis == runvis_t::yes) {
        std::vector<std::string> vis_args{
            "", // the exe path, which we're ignoring in this case
        };
        vis_args.push_back(bsp_path.string());
        vis_main(vis_args);
    }

    // run light
    {
        std::vector<std::string> light_args{
            "", // the exe path, which we're ignoring in this case
            "-nodefaultpaths", // in case test_quake2_maps_dir is pointing at a real Q2 install, don't
                               // read texture data etc. from there - we want the tests to behave the same
                               // during development as they do on CI (which doesn't have a Q2 install).
            "-path",
            wal_metadata_path.string()
        };
        for (auto &arg : extra_args) {
            light_args.push_back(arg);
        }
        light_args.push_back(bsp_path.string());

        light_main(light_args);
    }

    // serialize obj
    {
        bspdata_t bspdata;
        LoadBSPFile(bsp_path, &bspdata);

        ConvertBSPFormat(&bspdata, &bspver_generic);

        // write to .json for inspection
        serialize_bsp(bspdata, std::get<mbsp_t>(bspdata.bsp),
            fs::path(qbsp_options.bsp_path).replace_extension(".bsp.json"));

        return {std::move(std::get<mbsp_t>(bspdata.bsp)),
                std::move(bspdata.bspx.entries)};
    }
}

TEST_CASE("-world_units_per_luxel") {
    LoadTestmap("q2_lightmap_custom_scale.map", {"-world_units_per_luxel", "8"});
}

TEST_CASE("emissive cube artifacts") {
    // A cube with surface flags "light", value "100", placed in a hallway.
    //
    // Generates harsh lines on the walls/ceiling due to a hack in `light` allowing
    // surface lights to emit 50% at 90 degrees off their surface normal (when physically it should be 0%).
    //
    // It's wanted in some cases (base1.map sewer lights flush with the wall, desired for them to
    // emit some lights on to their adjacent wall faces.)
    //
    // To disable the behaviour in this case with the cube lighting a hallway we have a entity key:
    //
    //     "_surflight_rescale" "0"
    //
    auto [bsp, bspx] = LoadTestmap("light_q2_emissive_cube.map", {"-threads", "1", "-world_units_per_luxel", "4", "-novanilla"});

    const auto start = qvec3d{1044, -1244, 880};
    const auto end = qvec3d{1044, -1272, 880};

    auto *floor = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], start, {0, 0, 1});
    auto lm_info = BSPX_DecoupledLM(bspx, Face_GetNum(&bsp, floor));

    const faceextents_t extents(*floor, bsp, lm_info.lmwidth, lm_info.lmheight, lm_info.world_to_lm_space);

    // sample the lightmap along the floor, approaching the glowing cube
    // should get brighter
    qvec3b previous_sample{};
    for (int y = start[1]; y >= end[1]; y -= 4) {
        qvec3d pos = start;
        pos[1] = y;

        auto lm_coord = extents.worldToLMCoord(pos);

        auto sample = LM_Sample(&bsp, extents, lm_info.offset, lm_coord);
        CHECK(sample[0] >= previous_sample[0]);

        //logging::print("world: {} lm_coord: {} sample: {} lm size: {}x{}\n", pos, lm_coord, sample, lm_info.lmwidth, lm_info.lmheight);

        previous_sample = sample;
    }
}

TEST_CASE("-novanilla + -world_units_per_luxel")
{
    auto [bsp, bspx] = LoadTestmap("q2_lightmap_custom_scale.map", {"-novanilla", "-world_units_per_luxel", "8"});

    for (auto &face : bsp.dfaces) {
        CHECK(face.lightofs == -1);
    }

    // make sure no other bspx lumps are written
    CHECK(bspx.size() == 1);
    CHECK(bspx.find("DECOUPLED_LM") != bspx.end());

    // make sure all dlightdata bytes are accounted for by the DECOUPLED_LM lump
    // and no extra was written.
    size_t expected_dlightdata_bytes = 0;
    for (auto &face : bsp.dfaces) {
        // count used styles
        size_t face_used_styles = 0;
        for (auto style : face.styles) {
            if (style != 255) {
                ++face_used_styles;
            }
        }

        // count used pixels per style
        auto lm_info = BSPX_DecoupledLM(bspx, Face_GetNum(&bsp, &face));
        const faceextents_t extents(face, bsp, lm_info.lmwidth, lm_info.lmheight, lm_info.world_to_lm_space);
        int samples_per_face = extents.numsamples() * face_used_styles;

        // round up to multiple of 4
        if (samples_per_face % 4) {
            samples_per_face += (4 - (samples_per_face % 4));
        }

        int bytes_per_face = 3 * samples_per_face;
        expected_dlightdata_bytes += bytes_per_face;
    }
    CHECK(bsp.dlightdata.size() == expected_dlightdata_bytes);
}

template <class L>
static void CheckFaceLuxels(const mbsp_t &bsp, const mface_t &face, L&& lambda)
{
    const faceextents_t extents(face, bsp, LMSCALE_DEFAULT);

    for (int x = 0; x < extents.width(); ++x) {
        for (int y = 0; y < extents.height(); ++y) {
            const qvec3b sample = LM_Sample(&bsp, extents, face.lightofs, {x, y});
            INFO("sample ", x, ", ", y);
            lambda(sample);
        }
    }
}

static void CheckFaceLuxelsNonBlack(const mbsp_t &bsp, const mface_t &face)
{
    CheckFaceLuxels(bsp, face, [](qvec3b sample){
        CHECK(sample[0] > 0);
    });
}

TEST_CASE("emissive lights") {
    auto [bsp, bspx] = LoadTestmap("q2_light_flush.map", {});
    REQUIRE(bspx.empty());

    {
        INFO("the angled face on the right should not have any full black luxels");
        auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {244, -92, 92});
        REQUIRE(face);
        CheckFaceLuxelsNonBlack(bsp, *face);
    }

    {
        INFO("the angled face on the left should not have any full black luxels");
        auto *left_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {470.4, 16, 112});
        REQUIRE(left_face);
        CheckFaceLuxelsNonBlack(bsp, *left_face);
    }
}

TEST_CASE("q2_phong_doesnt_cross_contents") {
    auto [bsp, bspx] = LoadTestmap("q2_phong_doesnt_cross_contents.map", {"-wrnormals"});
}

TEST_CASE("q2_minlight_nomottle") {
    INFO("_minlightMottle 0 works on worldspawn");

    auto [bsp, bspx] = LoadTestmap("q2_minlight_nomottle.map", {});

    auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {276, 84, 32});
    REQUIRE(face);

    CheckFaceLuxels(bsp, *face, [](qvec3b sample){
        CHECK(sample == qvec3b(33, 33, 33));
    });
}

TEST_CASE("q2_dirt") {
    INFO("liquids don't cast dirt");

    auto [bsp, bspx] = LoadTestmap("q2_dirt.map", {});

    auto *face_under_lava = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {104, 112, 48});
    REQUIRE(face_under_lava);

    CheckFaceLuxels(bsp, *face_under_lava, [](qvec3b sample){
        CHECK(sample == qvec3b(96));
    });
}

TEST_CASE("q2_light_translucency") {
    INFO("liquids cast translucent colored shadows (sampling texture) by default");

    auto [bsp, bspx] = LoadTestmap("q2_light_translucency.map", {});

    auto *face_under_water = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {152, -96, 32});
    REQUIRE(face_under_water);

    CheckFaceLuxels(bsp, *face_under_water, [](qvec3b sample){
        INFO("green color from the texture");
        CHECK(sample == qvec3b(100, 150, 100));
    });
}

TEST_CASE("-visapprox vis with opaque liquids") {
    INFO("opaque liquids block vis, but don't cast shadows by default.");
    INFO("make sure '-visapprox vis' doesn't wrongly cull rays that should illuminate the level.");

    const std::vector<std::string> maps{
        "q2_light_visapprox.map", // light in liquid
        "q2_light_visapprox2.map" // light outside of liquid
    };

    for (const auto& map : maps) {
        SUBCASE(map.c_str()) {
            auto [bsp, bspx] = LoadTestmap(map, {"-visapprox", "vis"}, runvis_t::yes);

            auto *ceil_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {968, 1368, 1248});
            REQUIRE(ceil_face);

            CheckFaceLuxels(bsp, *ceil_face, [](qvec3b sample){
                INFO("ceiling above player start receiving light");
                REQUIRE(sample[0] > 200);
            });
        }
    }
}

TEST_CASE("negative lights work") {
    const std::vector<std::string> maps{
        "q2_light_negative.map",
        "q2_light_negative_bounce.map"
    };

    for (const auto& map : maps) {
        SUBCASE(map.c_str()) {
            auto [bsp, bspx] = LoadTestmap(map, {});

            auto *face_under_negative_light = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {632, 1304, 960});
            REQUIRE(face_under_negative_light);

            CheckFaceLuxels(bsp, *face_under_negative_light, [](qvec3b sample) { CHECK(sample == qvec3b(0)); });
        }
    }
}

TEST_CASE("light channel mask (_object_channel_mask, _light_channel_mask, _shadow_channel_mask)") {
    auto [bsp, bspx] = LoadTestmap("q2_light_group.map", {});
    REQUIRE(2 == bsp.dmodels.size());

    {
        INFO("world doesn't receive light from the light ent with _light_channel_mask 2");

        auto *face_under_light = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {680, 1224, 944});
        REQUIRE(face_under_light);

        CheckFaceLuxels(bsp, *face_under_light, [](qvec3b sample) {
            CHECK(sample == qvec3b(64));
        });
    }

    {
        INFO("pillar with _object_channel_mask 2 is receiving light");

        auto *face_on_pillar = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[1], {680, 1248, 1000});
        REQUIRE(face_on_pillar);

        CheckFaceLuxels(bsp, *face_on_pillar, [](qvec3b sample) {
            CHECK(sample[0] > 100);
        });
    }

    {
        INFO("_object_channel_mask 2 implicitly makes bmodels cast shadow in channel 2");

        auto *occluded_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[1], {680, 1280, 1000});
        REQUIRE(occluded_face);

        CheckFaceLuxels(bsp, *occluded_face, [](qvec3b sample) {
            CHECK(sample[0] == 0);
        });
    }

    {
        INFO("ensure AABB culling isn't breaking light channels");

        auto *unoccluded_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[1], {680, 1280, 1088});
        REQUIRE(unoccluded_face);

        CheckFaceLuxels(bsp, *unoccluded_face, [](qvec3b sample) {
            CHECK(sample[0] > 100);
        });
    }
}
