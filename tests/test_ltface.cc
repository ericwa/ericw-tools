#include <doctest/doctest.h>

#include <light/light.hh>
#include <qbsp/qbsp.hh>
#include <testmaps.hh>

static void LoadTestmap(const std::filesystem::path &name, std::vector<std::string> extra_args)
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
    for (auto &arg : extra_args) {
        args.push_back(arg);
    }
    args.push_back(map_path.string());
    args.push_back(bsp_path.string());

    // run qbsp

    InitQBSP(args);
    ProcessFile();

    // run light
    {
        std::vector<std::string> light_args{
            "", // the exe path, which we're ignoring in this case
            bsp_path.string()
        };
        light_main(light_args);
    }
}

TEST_CASE("TestLight") {
    LoadTestmap("q2_lightmap_custom_scale.map", {});
}
