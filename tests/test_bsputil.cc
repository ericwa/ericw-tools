#include <gtest/gtest.h>

#include <common/fs.hh>
#include <common/decompile.hh>
#include <common/bsputils.hh>
#include <qbsp/map.hh>
#include <bsputil/bsputil.hh>

#include <fstream>
#include <fmt/format.h>
#include <sstream>
#include <limits>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <array>

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

// runs `bsputil --svg` on a .bsp and returns the text of the .svg it writes beside it
static std::string RunSvg(const fs::path &bsp_path)
{
    std::string path_str = bsp_path.generic_string();
    std::vector<const char *> args{"bsputil.exe", "--svg", path_str.c_str()};

    EXPECT_EQ(0, bsputil_main(args.size(), args.data()));

    fs::path svg_path = bsp_path;
    svg_path.replace_extension(".svg");

    std::ifstream svg_istream(svg_path);
    std::stringstream svg_sstream;
    svg_sstream << svg_istream.rdbuf();
    return svg_sstream.str();
}

// the three components of every polygon's fill="rgb(r, g, b)", in document order.
// also checks that the document holds no NaN or infinity anywhere, in any spelling, and that
// each component is a plain number in the range of a colour.
static std::vector<std::array<double, 3>> SvgFillColors(const std::string &svg)
{
    std::string lower = svg;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return std::tolower(c); });
    EXPECT_EQ(std::string::npos, lower.find("nan")) << svg;
    EXPECT_EQ(std::string::npos, lower.find("inf")) << svg;

    std::vector<std::array<double, 3>> result;
    const std::string open = "fill=\"rgb(";

    for (size_t at = svg.find(open); at != std::string::npos; at = svg.find(open, at + 1)) {
        size_t start = at + open.size();
        size_t close = svg.find(')', start);
        EXPECT_NE(std::string::npos, close);
        if (close == std::string::npos) {
            break;
        }

        std::array<double, 3> color{};
        std::stringstream components(svg.substr(start, close - start));
        std::string component;
        size_t count = 0;

        while (std::getline(components, component, ',')) {
            // only digits and at most one decimal point: no sign, exponent, "nan" or "inf"
            size_t first = component.find_first_not_of(' ');
            component = first == std::string::npos ? "" : component.substr(first);
            EXPECT_FALSE(component.empty()) << svg;
            EXPECT_EQ(std::string::npos, component.find_first_not_of("0123456789.")) << component;
            EXPECT_LE(std::ranges::count(component, '.'), 1) << component;

            double value = component.empty() ? -1 : std::stod(component);
            EXPECT_GE(value, 0.0) << component;
            EXPECT_LE(value, 255.0) << component;

            if (count < 3) {
                color[count] = value;
            }
            count++;
        }

        EXPECT_EQ(3, count) << svg;
        result.push_back(color);
    }

    return result;
}

TEST(bsputil, svgHeightShade)
{
    // a sloped range: 0.5 at the bottom, 1 at the top, linear between
    EXPECT_FLOAT_EQ(0.5f, SvgHeightShade(64, 64, 128));
    EXPECT_FLOAT_EQ(0.75f, SvgHeightShade(96, 64, 128));
    EXPECT_FLOAT_EQ(1.0f, SvgHeightShade(128, 64, 128));

    // a range that crosses zero, and one that is entirely negative
    EXPECT_FLOAT_EQ(0.5f, SvgHeightShade(-96, -96, 160));
    EXPECT_FLOAT_EQ(0.75f, SvgHeightShade(32, -96, 160));
    EXPECT_FLOAT_EQ(1.0f, SvgHeightShade(160, -96, 160));
    EXPECT_FLOAT_EQ(0.75f, SvgHeightShade(-96, -128, -64));

    // a flat range has nothing to grade: full brightness, not 0/0
    EXPECT_FLOAT_EQ(1.0f, SvgHeightShade(0, 0, 0));
    EXPECT_FLOAT_EQ(1.0f, SvgHeightShade(112, 112, 112));
    EXPECT_FLOAT_EQ(1.0f, SvgHeightShade(-64, -64, -64));

    // a height outside the range is clamped to it
    EXPECT_FLOAT_EQ(0.5f, SvgHeightShade(0, 64, 128));
    EXPECT_FLOAT_EQ(1.0f, SvgHeightShade(200, 64, 128));

    // non-finite input never reaches the output
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    for (float shade : {SvgHeightShade(nan, 0, 1), SvgHeightShade(0, nan, 1), SvgHeightShade(0, 0, nan),
             SvgHeightShade(inf, 0, 1), SvgHeightShade(-inf, 0, 1), SvgHeightShade(0, -inf, inf),
             SvgHeightShade(0, inf, -inf), SvgHeightShade(nan, nan, nan)}) {
        EXPECT_TRUE(std::isfinite(shade));
        EXPECT_GE(shade, 0.5f);
        EXPECT_LE(shade, 1.0f);
    }
}

TEST(bsputil, svg)
{
    // a flat drawing: the only upward face of the cube. its height range is empty, which used
    // to print the result of 0/0 ("-nan" or "nan", depending on the platform) as the colour.
    const std::string svg = RunSvg(std::filesystem::path(testmaps_dir) / "compiled" / "q1_cube.bsp");

    EXPECT_EQ(svg, R"-(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">
<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="112" height="160">
<defs><g id="bsp">
<polygon points="32,128 32,32 80,32 80,128 " fill="rgb(68, 33, 16)" />
</g></defs>
<use href="#bsp" fill="none" stroke="black" stroke-width="15" stroke-miterlimit="0" />
<use href="#bsp" fill="white" stroke="black" stroke-width="1" />
</svg>
)-");

    EXPECT_EQ(1, SvgFillColors(svg).size());

    // the same input gives the same bytes again
    EXPECT_EQ(svg, RunSvg(std::filesystem::path(testmaps_dir) / "compiled" / "q1_cube.bsp"));
}

TEST(bsputil, svgHeightRanges)
{
    // two slabs with the same texture; the lower one is drawn first
    struct range_case_t
    {
        const char *map;
        double low_shade; // brightness of the lower slab relative to the higher one
    };

    for (const range_case_t &range_case : {range_case_t{"q1_svg_positive_heights.map", 0.5},
             range_case_t{"q1_svg_negative_to_positive_heights.map", 0.5},
             range_case_t{"q1_svg_equal_negative_heights.map", 1.0}}) {
        SCOPED_TRACE(range_case.map);

        LoadTestmapQ1(range_case.map);

        fs::path bsp_path = std::filesystem::path(testmaps_dir) / range_case.map;
        bsp_path.replace_extension(".bsp");

        const std::string svg = RunSvg(bsp_path);
        const auto colors = SvgFillColors(svg);

        ASSERT_EQ(2, colors.size()) << svg;
        // the texture's own average colour, at full brightness, on the highest slab
        EXPECT_EQ((std::array<double, 3>{68, 33, 16}), colors[1]);
        for (size_t i = 0; i < 3; i++) {
            EXPECT_DOUBLE_EQ(colors[1][i] * range_case.low_shade, colors[0][i]);
        }

        EXPECT_NE(std::string::npos, svg.find(R"(<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="256" height="128">)"));
        EXPECT_EQ(svg, RunSvg(bsp_path));
    }
}

TEST(bsputil, svgNonFiniteInput)
{
    // a brush model whose "origin" is "nan 0 inf": its faces cannot be placed, so they are left
    // out and the rest of the drawing is unaffected
    {
        LoadTestmapQ1("q1_svg_nonfinite_origin.map");

        const std::string svg = RunSvg(std::filesystem::path(testmaps_dir) / "q1_svg_nonfinite_origin.bsp");
        const auto colors = SvgFillColors(svg);

        ASSERT_EQ(1, colors.size()) << svg;
        EXPECT_EQ((std::array<double, 3>{68, 33, 16}), colors[0]);
        EXPECT_NE(std::string::npos, svg.find(R"(width="128" height="128")")) << svg;
    }

    // non-finite vertices in the BSP itself: one corner, then every corner
    LoadTestmapQ1("q1_svg_positive_heights.map");

    std::ifstream bsp_istream(std::filesystem::path(testmaps_dir) / "q1_svg_positive_heights.bsp", std::ios::binary);
    std::vector<char> clean_bsp((std::istreambuf_iterator<char>(bsp_istream)), std::istreambuf_iterator<char>());

    // BSP29: int32 version, then 15 (offset, length) lumps; lump 3 is the vertexes, 3 floats each
    int32_t version, vertex_offset, vertex_length;
    ASSERT_GE(clean_bsp.size(), 4 + 15 * 8);
    memcpy(&version, clean_bsp.data(), 4);
    memcpy(&vertex_offset, clean_bsp.data() + 4 + 3 * 8, 4);
    memcpy(&vertex_length, clean_bsp.data() + 4 + 3 * 8 + 4, 4);
    ASSERT_EQ(29, version);
    ASSERT_EQ(0, vertex_length % 12);
    ASSERT_GE(vertex_length / 12, 16);
    ASSERT_LE(static_cast<size_t>(vertex_offset) + vertex_length, clean_bsp.size());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    struct damage_t
    {
        const char *name;
        float value;
        bool every_vertex;
    };

    for (const damage_t &damage : {damage_t{"one_nan", nan, false}, damage_t{"one_inf", -inf, false},
             damage_t{"all_nan", nan, true}, damage_t{"all_inf", inf, true}}) {
        SCOPED_TRACE(damage.name);

        std::vector<char> bsp = clean_bsp;
        const int32_t vertex_count = vertex_length / 12;

        for (int32_t v = 0; v < (damage.every_vertex ? vertex_count : 1); v++) {
            for (int32_t axis = 0; axis < 3; axis++) {
                memcpy(bsp.data() + vertex_offset + v * 12 + axis * 4, &damage.value, 4);
            }
        }

        fs::path bsp_path = std::filesystem::path(testmaps_dir) / fmt::format("q1_svg_nonfinite_{}.bsp", damage.name);
        {
            std::ofstream bsp_ostream(bsp_path, std::ios::binary);
            bsp_ostream.write(bsp.data(), bsp.size());
        }

        const std::string svg = RunSvg(bsp_path);
        const auto colors = SvgFillColors(svg);

        if (damage.every_vertex) {
            // nothing left to draw: an empty image of the margin's size
            EXPECT_EQ(0, colors.size()) << svg;
            EXPECT_NE(std::string::npos, svg.find(R"(width="64" height="64")")) << svg;
        } else {
            EXPECT_LE(colors.size(), 2) << svg;
        }

        EXPECT_NE(std::string::npos, svg.find("</svg>"));
    }
}
