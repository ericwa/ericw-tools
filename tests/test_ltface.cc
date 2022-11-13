#include <doctest/doctest.h>

#include <light/light.hh>
#include <common/bspinfo.hh>
#include <qbsp/qbsp.hh>
#include <testmaps.hh>

struct testresults_t {
    mbsp_t bsp;
    bspxentries_t bspx;
};

static testresults_t LoadTestmap(const std::filesystem::path &name, std::vector<std::string> extra_args)
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

    // run light
    {
        std::vector<std::string> light_args{
            "", // the exe path, which we're ignoring in this case
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

TEST_CASE("TestLight") {
    LoadTestmap("q2_lightmap_custom_scale.map", {"-threads", "1", "-world_units_per_luxel", "8"});
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
    auto [bsp, bspx] = LoadTestmap("light_q2_emissive_cube.map", {"-threads", "1", "-world_units_per_luxel", "4"});

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
