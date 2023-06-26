#include <doctest/doctest.h>

#include <common/fs.hh>
#include <common/decompile.hh>
#include <common/bsputils.hh>
#include <qbsp/map.hh>
#include <bsputil/bsputil.hh>

#include <fstream>

#include "testmaps.hh"
#include "test_qbsp.hh"

TEST_SUITE("bsputil")
{
    TEST_CASE("q1_decompiler_test")
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

        for (int i = 0; i < bsp.dmodels[0].numfaces; ++i) {
            auto *face = &bsp.dfaces[bsp.dmodels[0].firstface + i];
            auto *face_texinfo = Face_Texinfo(&bsp, face);
            const qvec3d face_centroid = Face_Centroid(&bsp, face);
            const qvec3d face_normal = Face_Normal(&bsp, face);

            auto *face2 = BSP_FindFaceAtPoint(&bsp2, &bsp2.dmodels[0], face_centroid, face_normal);
            REQUIRE(face2);

            auto *face2_texinfo = Face_Texinfo(&bsp2, face2);
            CHECK(face2_texinfo->vecs == face_texinfo->vecs);
        }
    }

    TEST_CASE("extract-textures")
    {
        const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_extract_textures.map");

        // extract .bsp textures to test.wad
        std::ofstream wadfile("test.wad", std::ios::binary);
        ExportWad(wadfile, &bsp);

        // reload .wad
        fs::clear();
        img::clear();

        auto ar = fs::addArchive("test.wad");
        REQUIRE(ar);

        for (std::string texname : {"*swater4", "bolt14", "sky3", "brownlight"}) {
            INFO(texname);
            fs::data data = ar->load(texname);
            REQUIRE(data);
            auto loaded_tex = img::load_mip(texname, data, false, bspver_q1.game);
            CHECK(loaded_tex);
        }
    }
}
