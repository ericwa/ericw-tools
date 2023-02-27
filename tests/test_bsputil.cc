#include <doctest/doctest.h>

#include <common/fs.hh>
#include <common/decompile.hh>
#include <qbsp/map.hh>

#include "testmaps.hh"
#include "test_qbsp.hh"

TEST_SUITE("bsputil")
{
    TEST_CASE("q1_decompiler_test" * doctest::may_fail())
    {
        const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_decompiler_test.map");

        auto path = std::filesystem::path(testmaps_dir) / "q1_decompiler_test-decompile.map";
        std::ofstream f(path);

        decomp_options options;
        DecompileBSP(&bsp, options, f);

        f.close();

        // checks on the .map file
        auto &entity = LoadMapPath(path);
        CHECK(entity.mapbrushes.size() == 7); // two floor brushes

        // qbsp the decompiled map
        const auto [bsp2, bspx2, prt2] = LoadTestmapQ1("q1_decompiler_test-decompile.map");

        CHECK(bsp2.dmodels.size() == bsp.dmodels.size());
        CHECK(bsp2.dleafs.size() == bsp.dleafs.size());
        CHECK(bsp2.dnodes.size() == bsp.dnodes.size());

    }
}
