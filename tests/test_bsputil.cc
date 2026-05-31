#include <gtest/gtest.h>

#include <common/fs.hh>
#include <common/decompile.hh>
#include <common/bsputils.hh>
#include <qbsp/map.hh>
#include <bsputil/bsputil.hh>

#include <fstream>

#include "testmaps.hh"
#include "test_qbsp.hh"

TEST(bsputil, q1DecompilerTest)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_decompiler_test.map");

    auto path = std::filesystem::path(testmaps_dir) / "q1_decompiler_test-decompile.map";
    std::ofstream f(path);

    decomp_options options;
    DecompileBSP(&bsp, options, f);

    f.close();

    // checks on the .map file
    auto &entity = LoadMapPath(path);
    EXPECT_EQ(entity.mapbrushes.size(), 7); // two floor brushes

    // qbsp the decompiled map
    const auto [bsp2, bspx2, prt2] = LoadTestmapQ1("q1_decompiler_test-decompile.map");

    EXPECT_EQ(bsp2.dmodels.size(), bsp.dmodels.size());
    EXPECT_EQ(bsp2.dleafs.size(), bsp.dleafs.size());
    EXPECT_EQ(bsp2.dnodes.size(), bsp.dnodes.size());

    for (int i = 0; i < bsp.dmodels[0].numfaces; ++i) {
        auto *face = &bsp.dfaces[bsp.dmodels[0].firstface + i];
        auto *face_texinfo = Face_Texinfo(&bsp, face);
        const qvec3d face_centroid = Face_Centroid(&bsp, face);
        const qvec3d face_normal = Face_Normal(&bsp, face);

        auto *face2 = BSP_FindFaceAtPoint(&bsp2, &bsp2.dmodels[0], face_centroid, face_normal);
        ASSERT_TRUE(face2);

        auto *face2_texinfo = Face_Texinfo(&bsp2, face2);
        EXPECT_EQ(face2_texinfo->vecs, face_texinfo->vecs);
    }
}

TEST(bsputil, extractTextures)
{
    const auto [bsp, bspx, prt] = LoadTestmapQ1("q1_extract_textures.map");

    // extract .bsp textures to test.wad
    std::ofstream wadfile("test.wad", std::ios::binary);
    ExportWad(wadfile, &bsp);

    // reload .wad
    fs::clear();
    img::clear();
    img::init_palette(bspver_q1.game);

    auto ar = fs::addArchive("test.wad");
    ASSERT_TRUE(ar);

    for (std::string texname : {"*swater4", "bolt14", "sky3", "brownlight"}) {
        fs::data data = ar->load(texname);
        ASSERT_TRUE(data);
        auto loaded_tex = img::load_mip(texname, data, false, bspver_q1.game);
        EXPECT_TRUE(loaded_tex);
    }
}

TEST(bsputil, parseExtractTextures)
{
    bsputil_settings settings;

    const char *arguments[] = {"bsputil.exe", "--extract-textures", "test.bsp"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);

    ASSERT_EQ(1, remainder.size());
    ASSERT_EQ("test.bsp", remainder[0]);
}

TEST(bsputil, parseExtractEntities)
{
    bsputil_settings settings;

    const char *arguments[] = {"bsputil.exe", "--extract-entities", "test.bsp"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);

    ASSERT_EQ(1, remainder.size());
    ASSERT_EQ("test.bsp", remainder[0]);
}

TEST(bsputil, parseSvg)
{
    bsputil_settings settings;

    const char *arguments[] = {"bsputil.exe", "--svg", "test.bsp"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);

    ASSERT_EQ(1, remainder.size());
    ASSERT_EQ("test.bsp", remainder[0]);

    ASSERT_EQ(1, settings.operations.size());

    settings::setting_base *svg_setting_base = settings.operations[0].get();
    ASSERT_EQ(svg_setting_base->primary_name(), "svg");

    settings::setting_bool *svg_bool = dynamic_cast<settings::setting_bool *>(svg_setting_base);
    ASSERT_TRUE(svg_bool);
    ASSERT_TRUE(svg_bool->value());
}

TEST(bsputil, svg)
{
    fs::path path = std::filesystem::path(testmaps_dir) / "compiled" / "q1_cube.bsp";
    std::string path_str = path.generic_string();
    const char *path_cstr = path_str.c_str();

    std::vector<const char *> args;
    args.push_back("bsputil.exe");
    args.push_back("--svg");
    args.push_back(path_cstr);

    ASSERT_EQ(0, bsputil_main(args.size(), args.data()));

    fs::path svg_path = path;
    svg_path.replace_extension(".svg");

    // read into a string
    std::ifstream svg_istream(svg_path);
    std::stringstream svg_sstream;
    svg_sstream << svg_istream.rdbuf();

    // TODO: the -nan
    EXPECT_EQ(svg_sstream.str(), R"-(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">
<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="112" height="160">
<defs><g id="bsp">
<polygon points="32,128 32,32 80,32 80,128 " fill="rgb(-nan, -nan, -nan)" />
</g></defs>
<use href="#bsp" fill="none" stroke="black" stroke-width="15" stroke-miterlimit="0" />
<use href="#bsp" fill="white" stroke="black" stroke-width="1" />
</svg>
)-");
}
