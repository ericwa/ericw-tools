#include <doctest/doctest.h>

#include <common/fs.hh>
#include <common/bspfile.hh>
#include <common/decompile.hh>
#include <qbsp/map.hh>

#include "testmaps.hh"
#include "test_qbsp.hh"

TEST_SUITE("bsputil")
{
    TEST_CASE("q1_decompiler_test" * doctest::may_fail())
    {
        bspdata_t bspdata;
        std::filesystem::path path = std::filesystem::path(testmaps_dir) / ".." / "testbsps" / "q1_decompiler_test.bsp";
        LoadBSPFile(path, &bspdata);

        ConvertBSPFormat(&bspdata, &bspver_generic);

        path.replace_filename(path.stem().string() + "-decompile");
        path.replace_extension(".map");
        std::ofstream f(path);

        mbsp_t &bsp = std::get<mbsp_t>(bspdata.bsp);

        decomp_options options;
        DecompileBSP(&bsp, options, f);

        f.close();

        auto &entity = LoadMapPath(path);

        REQUIRE(entity.mapbrushes.size() == 14);
    }
}
