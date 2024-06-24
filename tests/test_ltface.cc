#include <gtest/gtest.h>

#include <light/light.hh>
#include <light/ltface.hh>
#include <light/surflight.hh>
#include <common/bspinfo.hh>
#include <common/litfile.hh>
#include <qbsp/qbsp.hh>
#include <testmaps.hh>
#include <vis/vis.hh>
#include "test_qbsp.hh"
#include "test_main.hh"

static testresults_t QbspVisLight_Common(const std::filesystem::path &name, std::vector<std::string> extra_qbsp_args,
    std::vector<std::string> extra_light_args, runvis_t run_vis)
{
    const bool is_q2 = std::find(extra_qbsp_args.begin(), extra_qbsp_args.end(), "-q2bsp") != extra_qbsp_args.end();
    const bool is_hl = std::find(extra_qbsp_args.begin(), extra_qbsp_args.end(), "-hlbsp") != extra_qbsp_args.end();

    auto map_path = std::filesystem::path(testmaps_dir) / name;

    fs::path bsp_dir;
    if (is_q2) {
        bsp_dir = fs::path(test_quake2_maps_dir);
    } else if (is_hl) {
        bsp_dir = fs::path(test_halflife_maps_dir);
    } else {
        bsp_dir = fs::path(test_quake_maps_dir);
    }
    // Try to get an absolute path, so our output .bsp (for qbsp) and input .bsp paths (for vis/light) are
    // absolute. Otherwise we risk light.exe picking up the wrong .bsp (especially if there are debug .bsp's in the
    // testmaps folder).
    if (bsp_dir.empty()) {
        bsp_dir = fs::current_path();
    } else {
        bsp_dir = fs::weakly_canonical(bsp_dir);
    }

    auto bsp_path = bsp_dir / name.filename();
    bsp_path.replace_extension(".bsp");

    auto wal_metadata_path = std::filesystem::path(testmaps_dir) / "q2_wal_metadata";

    std::vector<std::string> args{"", // the exe path, which we're ignoring in this case
        };

    if (!tests_verbose) {
        args.push_back("-noverbose");
    } else {
        args.push_back("-nopercent");
        args.push_back("-loghulls");
    }
    for (auto &extra : extra_qbsp_args) {
        args.push_back(extra);
    }
    args.push_back("-path");
    args.push_back(wal_metadata_path.string());
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
        std::vector<std::string> light_args{"", // the exe path, which we're ignoring in this case
            "-nodefaultpaths", // in case test_quake2_maps_dir is pointing at a real Q2 install, don't
                               // read texture data etc. from there - we want the tests to behave the same
                               // during development as they do on CI (which doesn't have a Q2 install).
            "-path", wal_metadata_path.string()};
        for (auto &arg : extra_light_args) {
            light_args.push_back(arg);
        }
        light_args.push_back(bsp_path.string());

        light_main(light_args);

        // ensure a .lit is never created in q2
        if (is_q2) {
            auto lit_check_path = bsp_path;
            lit_check_path.replace_extension(".lit");
            EXPECT_FALSE(fs::exists(lit_check_path));
        }
    }

    // serialize obj
    {
        bspdata_t bspdata;
        LoadBSPFile(bsp_path, &bspdata);

        ConvertBSPFormat(&bspdata, &bspver_generic);

        // write to .json for inspection
        serialize_bsp(
            bspdata, std::get<mbsp_t>(bspdata.bsp), fs::path(qbsp_options.bsp_path).replace_extension(".bsp.json"));

        return {std::move(std::get<mbsp_t>(bspdata.bsp)), std::move(bspdata.bspx.entries)};
    }
}

testresults_lit_t QbspVisLight_Q1(
    const std::filesystem::path &name, std::vector<std::string> extra_light_args, runvis_t run_vis)
{
    auto res = QbspVisLight_Common(name, {}, extra_light_args, run_vis);

    // load .lit file
    auto lit_path = fs::path(test_quake_maps_dir) / name.filename();
    lit_path.replace_extension(".lit");

    auto lit_variant = LoadLitFile(lit_path);

    return testresults_lit_t{.bsp = std::move(res.bsp),
                             .bspx = std::move(res.bspx),
                             .lit = std::move(lit_variant)};
}

testresults_t QbspVisLight_Q2(
    const std::filesystem::path &name, std::vector<std::string> extra_light_args, runvis_t run_vis)
{
    return QbspVisLight_Common(name, {"-q2bsp"}, extra_light_args, run_vis);
}

testresults_t QbspVisLight_HL(
        const std::filesystem::path &name, std::vector<std::string> extra_light_args, runvis_t run_vis)
{
    return QbspVisLight_Common(name, {"-hlbsp"}, extra_light_args, run_vis);
}

TEST(lightgridsample, styleEquality)
{
        lightgrid_sample_t a {.used = true, .style = 4, .color = {}};
        lightgrid_sample_t b = a;
        EXPECT_EQ(a, b);

        b.style = 6;
        EXPECT_NE(a, b);
}

TEST(lightgridsample, colorEquality) {
    lightgrid_sample_t a {.used = true, .style = 4, .color = {1,2,3}};
    lightgrid_sample_t b = a;
    EXPECT_EQ(a, b);

    b.color = {6,5,4};
    EXPECT_NE(a, b);
}

TEST(lightgridsample, nanColors) {
    lightgrid_sample_t a {.used = true, .style = 4, .color = {std::numeric_limits<double>::quiet_NaN(), 1.0, 1.0}};
    lightgrid_sample_t b = a;
    EXPECT_EQ(a, b);

    b.color = { 0,0,0};
    EXPECT_NE(a, b);
}

TEST(lightgridsample, unusedEqualityDoesntConsiderOtherAttributes) {
    lightgrid_sample_t a, b;
    EXPECT_FALSE(a.used);
    EXPECT_EQ(a, b);

    b.style = 5;
    EXPECT_EQ(a, b);

    b.color = {1, 0, 0};
    EXPECT_EQ(a, b);
}


TEST(worldunitsperluxel, lightgrid)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_lightmap_custom_scale.map", {"-lightgrid"});

    {
        SCOPED_TRACE("back wall has texture scale 8 but still gets a luxel every 8 units");
        auto *back_wall = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {448, -84, 276}, {-1, 0, 0});
        auto back_wall_info = BSPX_DecoupledLM(bspx, Face_GetNum(&bsp, back_wall));
        auto back_wall_extents = faceextents_t(
            *back_wall, bsp, back_wall_info.lmwidth, back_wall_info.lmheight, back_wall_info.world_to_lm_space);

        // NOTE: the exact values are not critical (depends on BSP splitting) but they should be relatively large
        EXPECT_EQ(75, back_wall_extents.width());
        EXPECT_EQ(43, back_wall_extents.height());
    }

    {
        SCOPED_TRACE("side wall func_group has _world_units_per_luxel 48, small lightmap");

        auto *side_wall = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {384, 240, 84}, {0, -1, 0});
        auto side_wall_info = BSPX_DecoupledLM(bspx, Face_GetNum(&bsp, side_wall));
        auto side_wall_extents = faceextents_t(
            *side_wall, bsp, side_wall_info.lmwidth, side_wall_info.lmheight, side_wall_info.world_to_lm_space);

        EXPECT_EQ(4, side_wall_extents.width());
        EXPECT_EQ(5, side_wall_extents.height());
    }

    {
        SCOPED_TRACE("sky gets an optimized lightmap");
        auto *sky_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {256, 240, 84}, {0, -1, 0});
        EXPECT_EQ(sky_face->styles[0], 255);

        auto sky_face_info = BSPX_DecoupledLM(bspx, Face_GetNum(&bsp, sky_face));
        EXPECT_EQ(sky_face_info.lmwidth, 0);
        EXPECT_EQ(sky_face_info.lmheight, 0);
    }
}

TEST(ltfaceQ2, emissiveCubeArtifacts)
{
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
    auto [bsp, bspx] =
        QbspVisLight_Q2("light_q2_emissive_cube.map", {"-threads", "1", "-world_units_per_luxel", "4", "-novanilla"});

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

        auto sample = LM_Sample(&bsp, floor, nullptr, extents, lm_info.offset, lm_coord);
        EXPECT_GE(sample[0], previous_sample[0]);

        // logging::print("world: {} lm_coord: {} sample: {} lm size: {}x{}\n", pos, lm_coord, sample, lm_info.lmwidth,
        // lm_info.lmheight);

        previous_sample = sample;
    }
}

TEST(ltfaceQ2, novanillaWorldUnitsPerLuxel)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_lightmap_custom_scale.map", {"-novanilla"});

    for (auto &face : bsp.dfaces) {
        EXPECT_EQ(face.lightofs, -1);
    }

    // make sure no other bspx lumps are written
    EXPECT_EQ(bspx.size(), 1);
    EXPECT_NE(bspx.find("DECOUPLED_LM"), bspx.end());

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
        int samples_per_face = align_value<4>(extents.numsamples() * face_used_styles);
        int bytes_per_face = 3 * samples_per_face;
        expected_dlightdata_bytes += bytes_per_face;
    }
    EXPECT_EQ(bsp.dlightdata.size(), expected_dlightdata_bytes);
}

template<class L>
static void CheckFaceLuxels(
    const mbsp_t &bsp, const mface_t &face, L &&lambda, const lit_variant_t *lit = nullptr)
{
    // FIXME: assumes no DECOUPLED_LM lump

    const faceextents_t extents(face, bsp, LMSCALE_DEFAULT);

    for (int x = 0; x < extents.width(); ++x) {
        for (int y = 0; y < extents.height(); ++y) {
            const qvec3b sample = LM_Sample(&bsp, &face, lit, extents, face.lightofs, {x, y});
            SCOPED_TRACE(fmt::format("sample {}, {}", x, y));
            lambda(sample);
        }
    }
}

static void CheckFaceLuxelsNonBlack(const mbsp_t &bsp, const mface_t &face)
{
    CheckFaceLuxels(bsp, face, [](qvec3b sample) { EXPECT_GT(sample[0], 0); });
}

static void CheckFaceLuxelAtPoint(const mbsp_t *bsp, const dmodelh2_t *model, const qvec3b &expected_color,
    const qvec3d &point, const qvec3d &normal = {0, 0, 0}, const lit_variant_t *lit = nullptr,
    const bspxentries_t *bspx = nullptr)
{
    auto *face = BSP_FindFaceAtPoint(bsp, model, point, normal);
    ASSERT_TRUE(face);

    faceextents_t extents;
    int offset;

    if (bspx && bspx->find("DECOUPLED_LM") != bspx->end()) {
        auto lm_info = BSPX_DecoupledLM(*bspx, Face_GetNum(bsp, face));
        extents = faceextents_t(*face, *bsp, lm_info.lmwidth, lm_info.lmheight, lm_info.world_to_lm_space);
        offset = lm_info.offset;
    } else {
        // vanilla lightmap
        extents = faceextents_t(*face, *bsp, LMSCALE_DEFAULT);
        offset = face->lightofs;
    }

    const auto coord = extents.worldToLMCoord(point);
    const auto int_coord = qvec2i(round(coord[0]), round(coord[1]));

    const qvec3b sample = LM_Sample(bsp, face, lit, extents, offset, int_coord);
    SCOPED_TRACE(fmt::format("world point: {}", point));
    SCOPED_TRACE(fmt::format("lm coord: {}", coord));
    SCOPED_TRACE(fmt::format("lm int_coord: {}", int_coord));
    SCOPED_TRACE(fmt::format("face num: {}", Face_GetNum(bsp, face)));
    SCOPED_TRACE(fmt::format("actual sample: {}", sample));

    qvec3i delta = qv::abs(qvec3i{sample} - qvec3i{expected_color});
    EXPECT_LE(delta[0], 1);
    EXPECT_LE(delta[1], 1);
    EXPECT_LE(delta[2], 1);
}

static void CheckFaceLuxelAtPoint_HDR(const mbsp_t *bsp, const dmodelh2_t *model, const qvec3f &expected_color,
                                      const qvec3f &allowed_delta,
                                      const qvec3d &point, const qvec3d &normal = {0, 0, 0}, const lit_variant_t *lit = nullptr,
                                      const bspxentries_t *bspx = nullptr)
{
    auto *face = BSP_FindFaceAtPoint(bsp, model, point, normal);
    ASSERT_TRUE(face);

    faceextents_t extents;
    int offset;

    if (bspx && bspx->find("DECOUPLED_LM") != bspx->end()) {
        auto lm_info = BSPX_DecoupledLM(*bspx, Face_GetNum(bsp, face));
        extents = faceextents_t(*face, *bsp, lm_info.lmwidth, lm_info.lmheight, lm_info.world_to_lm_space);
        offset = lm_info.offset;
    } else {
        // vanilla lightmap
        extents = faceextents_t(*face, *bsp, LMSCALE_DEFAULT);
        offset = face->lightofs;
    }

    const auto coord = extents.worldToLMCoord(point);
    const auto int_coord = qvec2i(round(coord[0]), round(coord[1]));

    const qvec3f sample = LM_Sample_HDR(bsp, face, extents, offset, int_coord, lit, bspx);
    SCOPED_TRACE(fmt::format("world point: {}", point));
    SCOPED_TRACE(fmt::format("lm coord: {}", coord));
    SCOPED_TRACE(fmt::format("lm int_coord: {}", int_coord));
    SCOPED_TRACE(fmt::format("face num: {}", Face_GetNum(bsp, face)));
    SCOPED_TRACE(fmt::format("actual sample: {}", sample));

    qvec3f delta = qv::abs(sample - expected_color);
    EXPECT_LE(delta[0], allowed_delta[0]);
    EXPECT_LE(delta[1], allowed_delta[1]);
    EXPECT_LE(delta[2], allowed_delta[2]);
}

TEST(ltfaceQ2, emissiveLights)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_light_flush.map", {});
    ASSERT_TRUE(bspx.empty());

    {
        SCOPED_TRACE("the angled face on the right should not have any full black luxels");
        auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {244, -92, 92});
        ASSERT_TRUE(face);
        CheckFaceLuxelsNonBlack(bsp, *face);
    }

    {
        SCOPED_TRACE("the angled face on the left should not have any full black luxels");
        auto *left_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {470.4, 16, 112});
        ASSERT_TRUE(left_face);
        CheckFaceLuxelsNonBlack(bsp, *left_face);
    }
}

TEST(ltfaceQ2, phongDoesntCrossContents)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_phong_doesnt_cross_contents.map", {"-wrnormals"});
}

TEST(ltfaceQ2, minlightNomottle)
{
    SCOPED_TRACE("_minlightMottle 0 works on worldspawn");

    auto [bsp, bspx] = QbspVisLight_Q2("q2_minlight_nomottle.map", {});

    auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {276, 84, 32});
    ASSERT_TRUE(face);

    CheckFaceLuxels(bsp, *face, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(33, 33, 33)); });
}

TEST(ltfaceQ2, dirt)
{
    SCOPED_TRACE("liquids don't cast dirt");

    auto [bsp, bspx] = QbspVisLight_Q2("q2_dirt.map", {});

    auto *face_under_lava = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {104, 112, 48});
    ASSERT_TRUE(face_under_lava);

    CheckFaceLuxels(bsp, *face_under_lava, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(96)); });
}

TEST(ltfaceQ2, dirtDebug)
{
    SCOPED_TRACE("dirtdebug works in q2");

    auto [bsp, bspx] = QbspVisLight_Q2("q2_dirt.map", {"-dirtdebug"});

    auto *face_under_lava = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {104, 112, 48});
    ASSERT_TRUE(face_under_lava);

    CheckFaceLuxels(bsp, *face_under_lava, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(255)); });

    // check floor in the corner
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}, {-124, 300, 32});
}

TEST(ltfaceQ2, lightTranslucency)
{
    SCOPED_TRACE("liquids cast translucent colored shadows (sampling texture) by default");

    auto [bsp, bspx] = QbspVisLight_Q2("q2_light_translucency.map", {});

    {
        auto *face_under_water = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {152, -96, 32});
        ASSERT_TRUE(face_under_water);

        CheckFaceLuxels(bsp, *face_under_water, [](qvec3b sample) {
            SCOPED_TRACE("green color from the texture");
            EXPECT_EQ(sample, qvec3b(100, 150, 100));
        });
    }

    {
        SCOPED_TRACE("under _light_alpha 0 is not tinted");

        auto *under_alpha_0_glass = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-296, -96, 40});
        ASSERT_TRUE(under_alpha_0_glass);

        CheckFaceLuxels(bsp, *under_alpha_0_glass, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(150)); });
    }

    {
        SCOPED_TRACE("under _light_alpha 1 is fully tinted");

        auto *under_alpha_1_glass = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-616, -96, 40});
        ASSERT_TRUE(under_alpha_1_glass);

        CheckFaceLuxels(bsp, *under_alpha_1_glass, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(0, 150, 0)); });
    }

    {
        SCOPED_TRACE("alpha test works");

        auto *in_light = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-976, -316, 184});
        ASSERT_TRUE(in_light);

        CheckFaceLuxels(bsp, *in_light, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(150)); });

        auto *in_shadow = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-976, -316, 88});
        ASSERT_TRUE(in_shadow);

        CheckFaceLuxels(bsp, *in_shadow, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(0)); });
    }

    {
        SCOPED_TRACE("opaque liquids are lit twosided");

        const qvec3d point {-616, 592, 224};

        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {150, 150, 150}, point, {0, 0, 1});
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {150, 150, 150}, point, {0, 0, -1});
    }
}

TEST(ltfaceQ2, visapproxVisWithOpaqueLiquids)
{
    SCOPED_TRACE("opaque liquids block vis, but don't cast shadows by default.");
    SCOPED_TRACE("make sure '-visapprox vis' doesn't wrongly cull rays that should illuminate the level.");

    const std::vector<std::string> maps{
        "q2_light_visapprox.map", // light in liquid
        "q2_light_visapprox2.map" // light outside of liquid
    };

    for (const auto &map : maps) {
        SCOPED_TRACE(map);
        auto [bsp, bspx] = QbspVisLight_Q2(map, {"-visapprox", "vis"}, runvis_t::yes);

        auto *ceil_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {968, 1368, 1248});
        ASSERT_TRUE(ceil_face);

        CheckFaceLuxels(bsp, *ceil_face, [](qvec3b sample) {
            SCOPED_TRACE("ceiling above player start receiving light");
            ASSERT_GT(sample[0], 200);
        });
    }
}

TEST(ltfaceQ2, negativeLightsWork)
{
    const std::vector<std::string> maps{"q2_light_negative.map", "q2_light_negative_bounce.map"};

    for (const auto &map : maps) {
        SCOPED_TRACE(map);
        auto [bsp, bspx] = QbspVisLight_Q2(map, {});

        auto *face_under_negative_light = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {632, 1304, 960});
        ASSERT_TRUE(face_under_negative_light);

        CheckFaceLuxels(bsp, *face_under_negative_light, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(0)); });
    }
}

TEST(ltfaceQ2, lightChannelMask)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_light_group.map", {});
    ASSERT_EQ(4, bsp.dmodels.size());

    {
        SCOPED_TRACE("world doesn't receive light from the light ent with _light_channel_mask 2");

        auto *face_under_light = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {680, 1224, 944});
        ASSERT_TRUE(face_under_light);

        CheckFaceLuxels(bsp, *face_under_light, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(64)); });
    }

    {
        SCOPED_TRACE("pillar with _object_channel_mask 2 is receiving light");

        auto *face_on_pillar = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[1], {680, 1248, 1000});
        ASSERT_TRUE(face_on_pillar);

        CheckFaceLuxels(bsp, *face_on_pillar, [](qvec3b sample) {
            EXPECT_GE(sample[0], 254);
            EXPECT_EQ(sample[1], 0);
            EXPECT_EQ(sample[2], 0);
        });
    }

    {
        SCOPED_TRACE("_object_channel_mask 2 implicitly makes bmodels cast shadow in channel 2");

        auto *occluded_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[1], {680, 1280, 1000});
        ASSERT_TRUE(occluded_face);

        CheckFaceLuxels(bsp, *occluded_face, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(0)); });
    }

    {
        SCOPED_TRACE("ensure AABB culling isn't breaking light channels");

        auto *unoccluded_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[1], {680, 1280, 1088});
        ASSERT_TRUE(unoccluded_face);

        CheckFaceLuxels(bsp, *unoccluded_face, [](qvec3b sample) { EXPECT_GT(sample[0], 100); });
    }

    {
        SCOPED_TRACE("sunlight doesn't cast on _object_channel_mask 4 bmodel");

        auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[2], {904, 1248, 1016});
        ASSERT_TRUE(face);

        CheckFaceLuxels(bsp, *face, [](qvec3b sample) {
            EXPECT_EQ(sample[0], 0);
            EXPECT_GE(sample[1], 254);
            EXPECT_EQ(sample[2], 0);
        });
    }

    {
        SCOPED_TRACE("surface light doesn't cast on _object_channel_mask 8 bmodel");

        auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[3], {1288, 1248, 1016});
        ASSERT_TRUE(face);

        CheckFaceLuxels(bsp, *face, [](qvec3b sample) {
            EXPECT_EQ(sample[0], 0);
            EXPECT_EQ(sample[1], 0);
            EXPECT_GE(sample[2], 254);
        });
    }

    {
        SCOPED_TRACE("_object_channel_mask 8 bmodel doesn't occlude luxels of a (channel 1) worldspawn brush touching it");

        auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {1290, 1264, 1014});
        ASSERT_TRUE(face);

        SCOPED_TRACE("should be receiving orange light from surface light");
        CheckFaceLuxels(bsp, *face, [](qvec3b sample) {
            qvec3i delta = qv::abs(qvec3i(sample) - qvec3i{255, 127, 64});
            EXPECT_LE(delta[0], 2);
            EXPECT_LE(delta[1], 2);
            EXPECT_LE(delta[2], 2);
        });
    }

    {
        SCOPED_TRACE("check that _object_channel_mask 8 func_group receives _light_channel_mask 8");

        auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {1480, 1248, 1004});
        ASSERT_TRUE(face);

        CheckFaceLuxels(bsp, *face, [](qvec3b sample) {
            EXPECT_EQ(sample[0], 0);
            EXPECT_EQ(sample[1], 0);
            EXPECT_GE(sample[2], 254);
        });
    }

    {
        SCOPED_TRACE("_object_channel_mask 8 func_group doesn't cast shadow on default channel");

        auto *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {1484, 1280, 1016});
        ASSERT_TRUE(face);

        CheckFaceLuxels(bsp, *face, [](qvec3b sample) {
            qvec3i delta = qv::abs(qvec3i(sample) - qvec3i{255, 127, 64});
            EXPECT_LE(delta[0], 2);
            EXPECT_LE(delta[1], 2);
            EXPECT_LE(delta[2], 2);
        });
    }
}

TEST(ltfaceQ2, lightChannelMaskDirtInteraction)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_light_group_dirt.map", {});

    ASSERT_EQ(2, bsp.dmodels.size());

    SCOPED_TRACE("worldspawn has dirt in the corner");
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {26, 26, 26}, {1432, 1480, 944});

    SCOPED_TRACE("worldspawn not receiving dirt from func_wall on different channel");
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {60, 60, 60}, {1212, 1272, 1014});

    SCOPED_TRACE("func_wall on different channel not receiving dirt from worldspawn");
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[1], {64, 64, 64}, {1216, 1266, 1014});

    SCOPED_TRACE("func_wall on different channel is receiving dirt from itself");
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[1], {19, 19, 19}, {1236, 1308, 960});
}

TEST(ltfaceQ2, surfaceLightsMinlight)
{
    GTEST_SKIP();

    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_surflight_minlight.map", {});

    {
        SCOPED_TRACE("there's a point entity in the void, but it has _nofill 1 so it should be ignored by filling");
        CheckFilled(bsp);
    }

    auto *surflight = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-3264, -1664, -560});
    ASSERT_TRUE(surflight);

    const auto l = [](qvec3b sample) {
        // "light" key is 100, color is (1, 0.5, 0), but values get halved due to overbright

        EXPECT_LE(sample[0], 75);
        EXPECT_GE(sample[0], 50);

        EXPECT_LE(sample[1], 35);
        EXPECT_GE(sample[1], 25);

        EXPECT_EQ(sample[2], 0);
    };

    CheckFaceLuxels(bsp, *surflight, l, &lit);

    SCOPED_TRACE("same but with liquid");

    auto *liquid_face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-3264, -1456, -560}, {-1, 0, 0});
    ASSERT_TRUE(liquid_face);

    CheckFaceLuxels(bsp, *liquid_face, l, &lit);
}

static void CheckSpotCutoff(const mbsp_t &bsp, const qvec3d &position)
{
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}, position + qvec3d{16, 0, 0});
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {243, 243, 243}, position - qvec3d{16, 0, 0});
}

TEST(ltfaceQ2, lightCone)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_light_cone.map", {});

    // lights are 256 units from wall
    // all 3 lights have a 10 degree cone radius
    // radius on wall should be 256 * sin(10 degrees) = 44.45 units

    CheckSpotCutoff(bsp, {948, 1472, 952});
    CheckSpotCutoff(bsp, {1092, 1472, 952});
    CheckSpotCutoff(bsp, {1236, 1472, 952});
}

TEST(ltfaceQ2, lightSunlightDefaultMangle)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_light_sunlight_default_mangle.map", {});

    SCOPED_TRACE("sunlight should be shining directly down if unspecified");
    const qvec3d shadow_pos{1112, 1248, 944};
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}, shadow_pos);

    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {100, 100, 100}, shadow_pos + qvec3d{48, 0, 0});
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {100, 100, 100}, shadow_pos + qvec3d{-48, 0, 0});
}

TEST(ltfaceQ2, lightSun)
{
    const std::vector<std::string> maps{"q2_light_sun.map", "q2_light_sun_mangle.map"};

    for (const auto &map : maps) {
        SCOPED_TRACE(map);
        auto [bsp, bspx] = QbspVisLight_Q2(map, {});

        SCOPED_TRACE("sun entity shines at target (q2_light_sun.map) or uses given mangle (q2_light_sun_mangle.map)");
        const qvec3d shadow_pos{1084, 1284, 944};
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}, shadow_pos);

        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {220, 0, 0}, shadow_pos + qvec3d{128, 0, 0});
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {220, 0, 0}, shadow_pos + qvec3d{-128, 0, 0});
    }
}

TEST(ltfaceQ2, lightOriginBrushShadow)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_light_origin_brush_shadow.map", {});

    const qvec3d under_shadow_bmodel{-320, 176, 1};
    const qvec3d under_nonshadow_bmodel{-432, 176, 1};

    const qvec3d under_nodraw_shadow_bmodel = under_shadow_bmodel - qvec3d(0, 96, 0);
    const qvec3d under_nodraw_nonshadow_bmodel = under_nonshadow_bmodel - qvec3d(0, 96, 0);

    const qvec3d at_origin{0, 0, 1};

    SCOPED_TRACE("ensure expected shadow");
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}, under_shadow_bmodel);
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}, under_nodraw_shadow_bmodel);

    SCOPED_TRACE("ensure no spurious shadow under non-_shadow 1 bmodel");
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {100, 100, 100}, under_nonshadow_bmodel);
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {100, 100, 100}, under_nodraw_nonshadow_bmodel);

    SCOPED_TRACE("ensure no spurious shadow at the world origin (would happen if we didn't apply model offset)");
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {100, 100, 100}, at_origin);
}

TEST(ltfaceQ2, surfaceLightsCulling)
{
    GTEST_SKIP();

    auto [bsp, bspx] = QbspVisLight_Q2("q2_surface_lights_culling.map", {});

    EXPECT_EQ(7, GetSurflightPoints());

    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {155, 78, 39}, {-480, 168, 64});
}

TEST(ltfaceQ1, lightignore)
{
    GTEST_SKIP();

    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_lightignore.map", {"-bounce"});

    {
        SCOPED_TRACE("func_wall");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[1], {0, 0, 0}, {-48, 144, 48}, {0, 0, 1}, &lit);
    }
    {
        SCOPED_TRACE("func_detail");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}, {72, 144, 48}, {0, 0, 1}, &lit);
    }
    {
        SCOPED_TRACE("worldspawn (receives light)");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {55, 69, 83}, {-128, 144, 32}, {0, 0, 1}, &lit);
    }
}

TEST(ltfaceQ2, lowLuxelRes)
{
    auto [bsp, bspx] = QbspVisLight_Q2(
        "q2_light_low_luxel_res.map", {"-world_units_per_luxel", "32", "-dirt", "-debugface", "2164", "712", "-968"});

    {
        SCOPED_TRACE("non-sloped cube");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {232, 185, 0}, {2138, 712, -968}, {0, 1, 0}, nullptr, &bspx);
    }
    {
        SCOPED_TRACE("sloped cube");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {232, 185, 0}, {2164, 712, -968}, {0, 1, 0}, nullptr, &bspx);
    }
}

TEST(ltfaceQ2, lowLuxelRes2)
{
    GTEST_SKIP();

    auto [bsp, bspx] = QbspVisLight_Q2(
        "q2_light_low_luxel_res2.map", {"-world_units_per_luxel", "32", "-debugface", "2964", "1020", "-696"});

    SCOPED_TRACE("should be a smooth transition across these points");
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {49, 49, 49}, {2964, 1046, -694}, {-1, 0, 0}, nullptr, &bspx);
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {25, 25, 25}, {2964, 1046, -706}, {-1, 0, 0}, nullptr, &bspx);
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {1, 1, 1}, {2964, 1046, -716}, {-1, 0, 0}, nullptr, &bspx);
}

TEST(ltfaceQ2, minlightInherited)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_minlight_inherited.map", {});

    {
        SCOPED_TRACE("check worldspawn minlight");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {64, 0, 0}, {456, 196, 0}, {0, 0, 1}, nullptr, &bspx);
    }

    {
        SCOPED_TRACE("check that func_group inherits worldspawn minlight");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {64, 0, 0}, {360, 72, 16}, {0, 0, 1}, nullptr, &bspx);
    }
    {
        SCOPED_TRACE("check that func_wall inherits worldspawn minlight");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[1], {64, 0, 0}, {208, 72, 16}, {0, 0, 1}, nullptr, &bspx);
    }

    {
        SCOPED_TRACE("check that func_group can override worldspawn minlight");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {128, 0, 0}, {360, -84, 16}, {0, 0, 1}, nullptr, &bspx);
    }
    {
        SCOPED_TRACE("check that func_wall can override worldspawn minlight");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[2], {128, 0, 0}, {208, -84, 16}, {0, 0, 1}, nullptr, &bspx);
    }

    {
        SCOPED_TRACE("check that func_group can override worldspawn minlight color");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 64, 0}, {360, -248, 16}, {0, 0, 1}, nullptr, &bspx);
    }
    {
        SCOPED_TRACE("check that func_wall can override worldspawn minlight color");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[3], {0, 64, 0}, {208, -248, 16}, {0, 0, 1}, nullptr, &bspx);
    }
}

TEST(ltfaceQ2, minlightInheritedAndNoextendedsurfflags)
{
    auto [bsp, bspx] =
        QbspVisLight_Common("q2_minlight_inherited.map", {"-q2bsp", "-noextendedsurfflags"}, {}, runvis_t::no);

    {
        SCOPED_TRACE("check that func_wall inherits worldspawn minlight");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[1], {64, 0, 0}, {208, 72, 16}, {0, 0, 1}, nullptr, &bspx);
    }

    {
        SCOPED_TRACE("check that func_wall can override worldspawn minlight");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[2], {128, 0, 0}, {208, -84, 16}, {0, 0, 1}, nullptr, &bspx);
    }

    {
        SCOPED_TRACE("check that func_wall can override worldspawn minlight color");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[3], {0, 64, 0}, {208, -248, 16}, {0, 0, 1}, nullptr, &bspx);
    }
}

TEST(ltfaceQ1, litWater)
{
    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_litwater.map", {});

    {
        SCOPED_TRACE("cube 1: lava has blue lightmap");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 10, 171}, {-288, 120, 128}, {0, 0, 1}, &lit);
    }

    {
        SCOPED_TRACE("cube 2: non-lightmapped via _splitturb 0 func_group key");
        auto *f = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-160, 120, 128}, {0, 0, 1});
        auto *ti = Face_Texinfo(&bsp, f);
        EXPECT_EQ(ti->flags.native, TEX_SPECIAL);
    }

    {
        SCOPED_TRACE("cube 3: lightmapped, but using minlight only via _lightignore and _minlight func_group keys");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {50, 50, 50}, {-32, 120, 128}, {0, 0, 1}, &lit);
    }
}

TEST(ltfaceQ1, litWaterOptIn)
{
    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_litwater_opt_in.map", {});

    {
        SCOPED_TRACE("cube 1: lava has blue lightmap (opt-in via _litwater 1)");
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 162}, {-288, 120, 128}, {0, 0, 1}, &lit);
    }

    {
        SCOPED_TRACE("cube 2: non-lightmapped");
        auto *f = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], {-160, 120, 128}, {0, 0, 1});
        auto *ti = Face_Texinfo(&bsp, f);
        EXPECT_EQ(ti->flags.native, TEX_SPECIAL);
    }
}

TEST(ltfaceQ2, lightDivZero)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_light_divzero.map", {"-world_units_per_luxel", "8"});

    SCOPED_TRACE("should not have a black spot in the center of the light face");
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {255, 127, 63}, {-992, 0, -480}, {0, 0, -1}, nullptr, &bspx);
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {255, 127, 63}, {-984, 8, -480}, {0, 0, -1}, nullptr, &bspx);
}

TEST(ltfaceQ1, minlightDoesntBounce)
{
    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_minlight_nobounce.map", {"-lit"});
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {50, 50, 50}, {0, 0, 0}, {0, 0, 1}, &lit);
}

TEST(ltfaceQ1, sunlight)
{
    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_sunlight.map", {"-lit"});
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {49, 49, 49}, {0, 0, 0}, {0, 0, 1}, &lit);
}

TEST(ltfaceQ1, suntexture)
{
    SCOPED_TRACE("different _sun 1 entities can emit from specific texture names using _suntexture");

    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_light_suntexture.map", {});
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {35, 0, 0}, {504, 1288, 944}, {0, 0, 1}, &lit);
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 142}, {1000, 1288, 944}, {0, 0, 1}, &lit);
}

TEST(ltfaceQ1, lightSunArtifact)
{
    SCOPED_TRACE("sun rays can hit cracks if RTC_SCENE_FLAG_ROBUST is not used");

    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_light_sun_artifact.map", {"-lit"});

    for (const auto &face : bsp.dfaces) {
        if (Face_Normal(&bsp, &face) == qvec3d(0, 0, 1)) {
            CheckFaceLuxels(bsp, face, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(128, 0, 0)); }, &lit);
        }
    }
}

TEST(ltfaceQ1, lightInvalidDelay)
{
    SCOPED_TRACE("invalid light formulas are ignored, not a fatal error");

    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_light_invalid_delay.map", {"-lit"});

    for (const auto &face : bsp.dfaces) {
        CheckFaceLuxels(bsp, face, [](qvec3b sample) { EXPECT_EQ(sample, qvec3b(0, 0, 0)); }, &lit);
    }
}

TEST(ltfaceQ1, bounceLitwaterWithoutTheWater)
{
    auto [bsp, bspx] = QbspVisLight_Common("q1_light_bounce_litwater.map", {"-omitdetail"}, {"-lit", "-bounce", "4"}, runvis_t::no);
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {118, 118, 118}, {128, 12, 156}, {-1, 0, 0});
}

TEST(ltfaceQ1, bounceLitwater)
{
    SCOPED_TRACE("adding a water plane should not affect the amount of light bounced on to the walls");

    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_light_bounce_litwater.map", {"-lit", "-bounce", "4"});
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {118, 118, 118}, {128, 12, 156}, {-1, 0, 0});
}

TEST(ltfaceQ1, bounceNoshadow)
{
    SCOPED_TRACE("make sure light doesn't both pass through and bounce off of a face with _shadow -1");

    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_light_bounce_noshadow.map", {"-lit", "-bounce", "4"});
    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {118, 118, 118}, {128, 12, 156}, {-1, 0, 0});
}

TEST(ltfaceQ2, lightBlack)
{
    auto [bsp, bspx] = QbspVisLight_Q2("q2_light_black.map", {});

    const qvec3d point {1056, 1300, 972};

    SCOPED_TRACE("ensure completely black lightmaps are written out as style 0 in Q2 mode");

    const mface_t *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], point, {-1, 0, 0});
    ASSERT_TRUE(face);
    EXPECT_EQ(face->styles[0], 0);
    EXPECT_EQ(face->styles[1], 255);
    EXPECT_EQ(face->styles[2], 255);
    EXPECT_EQ(face->styles[3], 255);

    CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}, point, {-1, 0, 0});
}

TEST(ltfaceQ1, lightBlack)
{
    auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_light_black.map", {"-lit"});

    {
        const qvec3d point{1056, 1300, 972};

        SCOPED_TRACE("ensure completely black lightmaps are written out as style 255 / lightofs -1 in Q1 mode");

        const mface_t *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], point, {-1, 0, 0});
        ASSERT_TRUE(face);
        EXPECT_EQ(face->styles[0], 255);
        EXPECT_EQ(face->styles[1], 255);
        EXPECT_EQ(face->styles[2], 255);
        EXPECT_EQ(face->styles[3], 255);
        EXPECT_EQ(face->lightofs, -1);

        // this is consistent with original tools, see:
        // https://github.com/id-Software/Quake-Tools/blob/master/qutils/LIGHT/LTFACE.C#L542
    }
    {
        SCOPED_TRACE("ensure lit water receiving no light is also written out as style 255 / lightofs -1");

        const qvec3d point{568, 1288, 976};

        const mface_t *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], point, {0, 0, 1});
        ASSERT_TRUE(face);
        auto *texinfo = Face_Texinfo(&bsp, face);
        ASSERT_TRUE(texinfo);

        EXPECT_EQ(texinfo->flags.native, 0); // i.e. TEX_SPECIAL is not set because it's lit water
        EXPECT_EQ(face->styles[0], 255);
        EXPECT_EQ(face->styles[1], 255);
        EXPECT_EQ(face->styles[2], 255);
        EXPECT_EQ(face->styles[3], 255);
        EXPECT_EQ(face->lightofs, -1);

        // Note, this liquid face is rendering as fullbright (incorrect) in: QS 0.96.0 and Ironwail 0.7.0
        // and rendering as solid black (correct) in vkQuake 1.30.1, FTEQW Mar 1 2022
    }
}

TEST(ltfaceHL, lightBlack)
{
    auto [bsp, bspx] = QbspVisLight_HL("hl_light_black.map", {});

    {
        const qvec3d point{1056, 1300, 972};

        SCOPED_TRACE("ensure completely black lightmaps are written out as style 255 / lightofs -1 in HL mode");

        const mface_t *face = BSP_FindFaceAtPoint(&bsp, &bsp.dmodels[0], point, {-1, 0, 0});
        ASSERT_TRUE(face);
        EXPECT_EQ(face->styles[0], 255);
        EXPECT_EQ(face->styles[1], 255);
        EXPECT_EQ(face->styles[2], 255);
        EXPECT_EQ(face->styles[3], 255);
        EXPECT_EQ(face->lightofs, -1);

        // confirmed that this renders as expected (black lightmaps) in the Dec 2023 HL build
    }
}

TEST(ltfaceQ1, hdr)
{
    // center of the room on the floor.
    // in the non-HDR lightmap this is pure black (0, 0, 0), but in the HDR one it's still receiving a bit of light
    const qvec3f testpoint = {0, 0, 48};
    const qvec3f testnormal = {0, 0, 1};
    const qvec3f expected_hdr_color = {0.00215912, 0.0018692, 0.00126648};

    {
        SCOPED_TRACE("lit");

        auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_hdrtest.map", {"-hdr"});

        EXPECT_TRUE(bspx.empty());
        EXPECT_TRUE(std::holds_alternative<lit_hdr>(lit));

        // check hdr .lit file
        CheckFaceLuxelAtPoint_HDR(&bsp, &bsp.dmodels[0], expected_hdr_color, {1e-5, 1e-5, 1e-5}, testpoint, testnormal,
                                  &lit, &bspx);

        // check internal lightmap - greyscale, since Q1
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}, testpoint, testnormal);
    }

    {
        SCOPED_TRACE("bspx");

        auto [bsp, bspx, lit] = QbspVisLight_Q1("q1_hdrtest.map", {"-bspxhdr"});

        EXPECT_EQ(bspx.size(), 1);
        EXPECT_NE(bspx.find("LIGHTING_E5BGR9"), bspx.end());
        EXPECT_TRUE(std::holds_alternative<lit_none>(lit));

        // check hdr BSPX lump
        CheckFaceLuxelAtPoint_HDR(&bsp, &bsp.dmodels[0], expected_hdr_color, {1e-5, 1e-5, 1e-5}, testpoint, testnormal,
                                  &lit, &bspx);

        // check internal lightmap - greyscale, since Q1
        CheckFaceLuxelAtPoint(&bsp, &bsp.dmodels[0], {0, 0, 0}, testpoint, testnormal);
    }
}