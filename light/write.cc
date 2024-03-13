/*  Copyright (C) 1996-1997  Id Software, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

#include <light/light.hh>
#include <light/ltface.hh>
#include <light/write.hh>

#include <common/log.hh>
#include <common/parallel.hh>

// litheader_t::v1_t

void litheader_t::v1_t::stream_write(std::ostream &s) const
{
    s <= std::tie(ident, version);
}

void litheader_t::v1_t::stream_read(std::istream &s)
{
    s >= std::tie(ident, version);
}

// litheader_t::v2_t

void litheader_t::v2_t::stream_write(std::ostream &s) const
{
    s <= std::tie(numsurfs, lmsamples);
}

void litheader_t::v2_t::stream_read(std::istream &s)
{
    s >= std::tie(numsurfs, lmsamples);
}

void WriteLitFile(const mbsp_t *bsp, const std::vector<facesup_t> &facesup, const fs::path &filename, int version, const std::vector<uint8_t> &lit_filebase, const std::vector<uint8_t> &lux_filebase)
{
    litheader_t header;

    fs::path litname = filename;
    litname.replace_extension("lit");

    header.v1.version = version;
    header.v2.numsurfs = bsp->dfaces.size();
    header.v2.lmsamples = bsp->dlightdata.size();

    logging::print("Writing {}\n", litname);
    std::ofstream litfile(litname, std::ios_base::out | std::ios_base::binary);
    litfile <= header.v1;
    if (version == 2) {
        unsigned int i, j;
        litfile <= header.v2;
        for (i = 0; i < bsp->dfaces.size(); i++) {
            litfile <= facesup[i].lightofs;
            for (int j = 0; j < 4; j++) {
                litfile <= facesup[i].styles[j];
            }
            for (int j = 0; j < 2; j++) {
                litfile <= facesup[i].extent[j];
            }
            j = 0;
            while (nth_bit(j) < facesup[i].lmscale)
                j++;
            litfile <= (uint8_t)j;
        }
        litfile.write((const char *)lit_filebase.data(), bsp->dlightdata.size() * 3);
        litfile.write((const char *)lux_filebase.data(), bsp->dlightdata.size() * 3);
    } else
        litfile.write((const char *)lit_filebase.data(), bsp->dlightdata.size() * 3);
}

void WriteLuxFile(const mbsp_t *bsp, const fs::path &filename, int version, const std::vector<uint8_t> &lux_filebase)
{
    litheader_t header;

    fs::path luxname = filename;
    luxname.replace_extension("lux");

    header.v1.version = version;

    std::ofstream luxfile(luxname, std::ios_base::out | std::ios_base::binary);
    luxfile <= header.v1;
    luxfile.write((const char *)lux_filebase.data(), bsp->dlightdata.size() * 3);
}


/*
 * Return space for the lightmap and colourmap at the same time so it can
 * be done in a thread-safe manner.
 *
 * size is the number of greyscale pixels = number of bytes to allocate
 * and return in *lightdata
 */
static inline int GetFileSpace(std::atomic_size_t &offset, size_t size)
{
    // if size isn't a multiple of 4, round up to the next multiple of 4
    size_t v = offset.fetch_add(size + (4 - (size % 4)));

    // early check
    if (v > std::numeric_limits<int>::max())
        FError("exceeded max lightmap space");

    return v;
}

std::atomic<uint32_t> fully_transparent_lightmaps;
static bool warned_about_light_map_overflow, warned_about_light_style_overflow;

static std::vector<qvec4f> LightmapColorsToGLMVector(const lightsurf_t *lightsurf, const lightmap_t *lm)
{
    std::vector<qvec4f> res;
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        const qvec3f &color = lm->samples[i].color;
        const float alpha = lightsurf->samples[i].occluded ? 0.0f : 1.0f;
        res.emplace_back(color[0], color[1], color[2], alpha);
    }
    return res;
}

static std::vector<qvec4f> LightmapNormalsToGLMVector(const lightsurf_t *lightsurf, const lightmap_t *lm)
{
    std::vector<qvec4f> res;
    for (int i = 0; i < lightsurf->samples.size(); i++) {
        const qvec3f &color = lm->samples[i].direction;
        const float alpha = lightsurf->samples[i].occluded ? 0.0f : 1.0f;
        res.emplace_back(color[0], color[1], color[2], alpha);
    }
    return res;
}

// Special handling of alpha channel:
// - "alpha channel" is expected to be 0 or 1. This gets set to 0 if the sample
// point is occluded (bmodel sticking outside of the world, or inside a shadow-
// casting bmodel that is overlapping a world face), otherwise it's 1.
//
// - If alpha is 0 the sample doesn't contribute to the filter kernel.
// - If all the samples in the filter kernel have alpha=0, write a sample with alpha=0
//   (but still average the colors, important so that minlight still works properly
//    for bmodels that go outside of the world).
static std::vector<qvec4f> IntegerDownsampleImage(const std::vector<qvec4f> &input, int w, int h, int factor)
{
    Q_assert(factor >= 1);
    if (factor == 1)
        return input;

    const int outw = w / factor;
    const int outh = h / factor;

    std::vector<qvec4f> res(static_cast<size_t>(outw * outh));

    for (int y = 0; y < outh; y++) {
        for (int x = 0; x < outw; x++) {

            float totalWeight = 0.0f;
            qvec3f totalColor{};

            // These are only used if all the samples in the kernel have alpha = 0
            float totalWeightIgnoringOcclusion = 0.0f;
            qvec3f totalColorIgnoringOcclusion{};

            const int extraradius = 0;
            const int kernelextent = factor + (2 * extraradius);

            for (int y0 = 0; y0 < kernelextent; y0++) {
                for (int x0 = 0; x0 < kernelextent; x0++) {
                    const int x1 = (x * factor) - extraradius + x0;
                    const int y1 = (y * factor) - extraradius + y0;

                    // check if the kernel goes outside of the source image
                    if (x1 < 0 || x1 >= w)
                        continue;
                    if (y1 < 0 || y1 >= h)
                        continue;

                    // read the input sample
                    const float weight = 1.0f;
                    const qvec4f &inSample = input.at((y1 * w) + x1);

                    totalColorIgnoringOcclusion += qvec3f(inSample) * weight;
                    totalWeightIgnoringOcclusion += weight;

                    // Occluded sample points don't contribute to the filter
                    if (inSample[3] == 0.0f)
                        continue;

                    totalColor += qvec3f(inSample) * weight;
                    totalWeight += weight;
                }
            }

            const int outIndex = (y * outw) + x;
            if (totalWeight > 0.0f) {
                const qvec3f tmp = totalColor / totalWeight;
                const qvec4f resultColor = qvec4f(tmp[0], tmp[1], tmp[2], 1.0f);
                res[outIndex] = resultColor;
            } else {
                const qvec3f tmp = totalColorIgnoringOcclusion / totalWeightIgnoringOcclusion;
                const qvec4f resultColor = qvec4f(tmp[0], tmp[1], tmp[2], 0.0f);
                res[outIndex] = resultColor;
            }
        }
    }

    return res;
}

static std::vector<qvec4f> FloodFillTransparent(const std::vector<qvec4f> &input, int w, int h)
{
    // transparent pixels take the average of their neighbours.

    std::vector<qvec4f> res(input);

    while (1) {
        int unhandled_pixels = 0;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const int i = (y * w) + x;
                const qvec4f &inSample = res.at(i);

                if (inSample[3] == 0) {
                    // average the neighbouring non-transparent samples

                    int opaque_neighbours = 0;
                    qvec3f neighbours_sum{};
                    for (int y0 = -1; y0 <= 1; y0++) {
                        for (int x0 = -1; x0 <= 1; x0++) {
                            const int x1 = x + x0;
                            const int y1 = y + y0;

                            if (x1 < 0 || x1 >= w)
                                continue;
                            if (y1 < 0 || y1 >= h)
                                continue;

                            const qvec4f neighbourSample = res.at((y1 * w) + x1);
                            if (neighbourSample[3] == 1) {
                                opaque_neighbours++;
                                neighbours_sum += qvec3f(neighbourSample);
                            }
                        }
                    }

                    if (opaque_neighbours > 0) {
                        neighbours_sum *= (1.0f / (float)opaque_neighbours);
                        res.at(i) = qvec4f(neighbours_sum[0], neighbours_sum[1], neighbours_sum[2], 1.0f);

                        // this sample is now opaque
                    } else {
                        unhandled_pixels++;

                        // all neighbours are transparent. need to perform more iterations (or the whole lightmap is
                        // transparent).
                    }
                }
            }
        }

        if (unhandled_pixels == input.size()) {
            // logging::funcprint("warning, fully transparent lightmap\n");
            fully_transparent_lightmaps++;
            break;
        }

        if (unhandled_pixels == 0)
            break; // all done
    }

    return res;
}

static std::vector<qvec4f> HighlightSeams(const std::vector<qvec4f> &input, int w, int h)
{
    std::vector<qvec4f> res(input);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int i = (y * w) + x;
            const qvec4f &inSample = res.at(i);

            if (inSample[3] == 0) {
                res.at(i) = qvec4f(255, 0, 0, 1);
            }
        }
    }

    return res;
}

static std::vector<qvec4f> BoxBlurImage(const std::vector<qvec4f> &input, int w, int h, int radius)
{
    std::vector<qvec4f> res(input.size());

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {

            float totalWeight = 0.0f;
            qvec3f totalColor{};

            // These are only used if all the samples in the kernel have alpha = 0
            float totalWeightIgnoringOcclusion = 0.0f;
            qvec3f totalColorIgnoringOcclusion{};

            for (int y0 = -radius; y0 <= radius; y0++) {
                for (int x0 = -radius; x0 <= radius; x0++) {
                    const int x1 = std::clamp(x + x0, 0, w - 1);
                    const int y1 = std::clamp(y + y0, 0, h - 1);

                    // check if the kernel goes outside of the source image

                    // 2017-09-16: this is a hack, but clamping the
                    // x/y instead of discarding the samples outside of the
                    // kernel looks better in some cases:
                    // https://github.com/ericwa/ericw-tools/issues/171
#if 0
                    if (x1 < 0 || x1 >= w)
                        continue;
                    if (y1 < 0 || y1 >= h)
                        continue;
#endif

                    // read the input sample
                    const float weight = 1.0f;
                    const qvec4f &inSample = input.at((y1 * w) + x1);

                    totalColorIgnoringOcclusion += qvec3f(inSample) * weight;
                    totalWeightIgnoringOcclusion += weight;

                    // Occluded sample points don't contribute to the filter
                    if (inSample[3] == 0.0f)
                        continue;

                    totalColor += qvec3f(inSample) * weight;
                    totalWeight += weight;
                }
            }

            const int outIndex = (y * w) + x;
            if (totalWeight > 0.0f) {
                const qvec3f tmp = totalColor / totalWeight;
                const qvec4f resultColor = qvec4f(tmp[0], tmp[1], tmp[2], 1.0f);
                res[outIndex] = resultColor;
            } else {
                const qvec3f tmp = totalColorIgnoringOcclusion / totalWeightIgnoringOcclusion;
                const qvec4f resultColor = qvec4f(tmp[0], tmp[1], tmp[2], 0.0f);
                res[outIndex] = resultColor;
            }
        }
    }

    return res;
}

/**
 * - Writes (actual_width * actual_height) bytes to `out`
 * - Writes (actual_width * actual_height * 3) bytes to `lit`
 * - Writes (actual_width * actual_height * 3) bytes to `lux`
 */
static void WriteSingleLightmap(const mbsp_t *bsp, const mface_t *face, const lightsurf_t *lightsurf,
    const lightmap_t *lm, const int actual_width, const int actual_height, uint8_t *out, uint8_t *lit, uint8_t *lux,
    const faceextents_t &output_extents)
{
    const int oversampled_width = actual_width * light_options.extra.value();
    const int oversampled_height = actual_height * light_options.extra.value();

    // allocate new float buffers for the output colors and directions
    // these are the actual output width*height, without oversampling.

    std::vector<qvec4f> fullres = LightmapColorsToGLMVector(lightsurf, lm);

    if (light_options.highlightseams.value()) {
        fullres = HighlightSeams(fullres, oversampled_width, oversampled_height);
    }

    // removes all transparent pixels by averaging from adjacent pixels
    fullres = FloodFillTransparent(fullres, oversampled_width, oversampled_height);

    if (light_options.soft.value() > 0) {
        fullres = BoxBlurImage(fullres, oversampled_width, oversampled_height, light_options.soft.value());
    }

    const std::vector<qvec4f> output_color =
        IntegerDownsampleImage(fullres, oversampled_width, oversampled_height, light_options.extra.value());
    std::optional<std::vector<qvec4f>> output_dir;

    if (lux) {
        output_dir = IntegerDownsampleImage(LightmapNormalsToGLMVector(lightsurf, lm), oversampled_width,
            oversampled_height, light_options.extra.value());
    }

    // copy from the float buffers to byte buffers in .bsp / .lit / .lux
    const int output_width = output_extents.width();
    const int output_height = output_extents.height();

    for (int t = 0; t < output_height; t++) {
        for (int s = 0; s < output_width; s++) {
            const int input_sample_s = (s / (float)output_width) * actual_width;
            const int input_sample_t = (t / (float)output_height) * actual_height;
            const int sampleindex = (input_sample_t * actual_width) + input_sample_s;

            if (lit || out) {
                const qvec4f &color = output_color.at(sampleindex);

                if (lit) {
                    *lit++ = color[0];
                    *lit++ = color[1];
                    *lit++ = color[2];
                }

                if (out) {
                    /* Take the max() of the 3 components to get the value to write to the
                    .bsp lightmap. this avoids issues with some engines
                    that require the lit and internal lightmap to have the same
                    intensity. (MarkV, some QW engines)

                    This must be max(), see LightNormalize in MarkV 1036.
                    */
                    float light = std::max({color[0], color[1], color[2]});
                    if (light < 0)
                        light = 0;
                    if (light > 255)
                        light = 255;
                    *out++ = light;
                }
            }

            if (lux) {
                qvec3f direction = output_dir->at(sampleindex).xyz();
                qvec3f temp = {qv::dot(direction, lightsurf->snormal), qv::dot(direction, lightsurf->tnormal),
                    qv::dot(direction, lightsurf->plane.normal)};

                if (qv::emptyExact(temp))
                    temp = {0, 0, 1};
                else
                    qv::normalizeInPlace(temp);

                int v = (temp[0] + 1) * 128;
                *lux++ = (v > 255) ? 255 : v;
                v = (temp[1] + 1) * 128;
                *lux++ = (v > 255) ? 255 : v;
                v = (temp[2] + 1) * 128;
                *lux++ = (v > 255) ? 255 : v;
            }
        }
    }
}

/**
 * - Writes (output_width * output_height) bytes to `out`
 * - Writes (output_width * output_height * 3) bytes to `lit`
 * - Writes (output_width * output_height * 3) bytes to `lux`
 */
static void WriteSingleLightmap_FromDecoupled(const mbsp_t *bsp, const mface_t *face, const lightsurf_t *lightsurf,
    const lightmap_t *lm, const int output_width, const int output_height, uint8_t *out, uint8_t *lit, uint8_t *lux)
{
    // this is the lightmap data in the "decoupled" coordinate system
    std::vector<qvec4f> fullres = LightmapColorsToGLMVector(lightsurf, lm);

    // maps a luxel in the vanilla lightmap to the corresponding position in the decoupled lightmap
    const qmat4x4f vanillaLMToDecoupled =
        lightsurf->extents.worldToLMMatrix * lightsurf->vanilla_extents.lmToWorldMatrix;

    // samples the "decoupled" lightmap at an integer coordinate, with clamping
    auto tex = [&lightsurf, &fullres](int x, int y) -> qvec4f {
        const int x_clamped = std::clamp(x, 0, lightsurf->width - 1);
        const int y_clamped = std::clamp(y, 0, lightsurf->height - 1);

        const int sampleindex = (y_clamped * lightsurf->width) + x_clamped;
        assert(sampleindex >= 0);
        assert(sampleindex < fullres.size());

        return fullres[sampleindex];
    };

    for (int t = 0; t < output_height; t++) {
        for (int s = 0; s < output_width; s++) {
            // convert from vanilla lm coord to decoupled lm coord
            qvec2f decoupled_lm_coord = vanillaLMToDecoupled * qvec4f(s, t, 0, 1);

            decoupled_lm_coord = decoupled_lm_coord * light_options.extra.value();

            // split into integer/fractional part for bilinear interpolation
            const int coord_floor_x = (int)decoupled_lm_coord[0];
            const int coord_floor_y = (int)decoupled_lm_coord[1];

            const float coord_frac_x = decoupled_lm_coord[0] - coord_floor_x;
            const float coord_frac_y = decoupled_lm_coord[1] - coord_floor_y;

            // 2D bilinear interpolation
            const qvec4f color =
                mix(mix(tex(coord_floor_x, coord_floor_y), tex(coord_floor_x + 1, coord_floor_y), coord_frac_x),
                    mix(tex(coord_floor_x, coord_floor_y + 1), tex(coord_floor_x + 1, coord_floor_y + 1), coord_frac_x),
                    coord_frac_y);

            if (lit || out) {
                if (lit) {
                    *lit++ = color[0];
                    *lit++ = color[1];
                    *lit++ = color[2];
                }

                if (out) {
                    // FIXME: implement
                    *out++ = 0;
                }
            }

            if (lux) {
                // FIXME: implement
                *lux++ = 0;
                *lux++ = 0;
                *lux++ = 0;
            }
        }
    }
}

// clamps negative values. applies gamma and rangescale. clamps values over 255
// N.B. we want to do this before smoothing / downscaling, so huge values don't mess up the averaging.
inline void LightFace_ScaleAndClamp(lightsurf_t *lightsurf)
{
    const settings::worldspawn_keys &cfg = *lightsurf->cfg;

    for (lightmap_t &lightmap : lightsurf->lightmapsByStyle) {
        for (int i = 0; i < lightsurf->samples.size(); i++) {
            qvec3f &color = lightmap.samples[i].color;

            /* Fix any negative values */
            color = qv::max(color, {0});

            // before any other scaling, apply maxlight
            if (lightsurf->maxlight || cfg.maxlight.value()) {
                float maxcolor = qv::max(color);
                // FIXME: for colored lighting, this doesn't seem to generate the right values...
                float maxval = (lightsurf->maxlight ? lightsurf->maxlight : cfg.maxlight.value()) * 2.0f;

                if (maxcolor > maxval) {
                    color *= (maxval / maxcolor);
                }
            }

            // color scaling
            if (lightsurf->lightcolorscale != 1.0f) {
                qvec3f grayscale{qv::max(color)};
                color = mix(grayscale, color, lightsurf->lightcolorscale);
            }

            /* Scale and handle gamma adjustment */
            color *= cfg.rangescale.value();

            if (cfg.lightmapgamma.value() != 1.0f) {
                for (auto &c : color) {
                    c = pow(c / 255.0f, 1.0f / cfg.lightmapgamma.value()) * 255.0f;
                }
            }

            // clamp
            // FIXME: should this be a brightness clamp?
            float maxcolor = qv::max(color);

            if (maxcolor > 255.0f) {
                color *= (255.0f / maxcolor);
            }
        }
    }
}

void FinishLightmapSurface(const mbsp_t *bsp, lightsurf_t *lightsurf)
{
    /* Apply gamma, rangescale, and clamp */
    LightFace_ScaleAndClamp(lightsurf);
}

static float Lightmap_AvgBrightness(const lightmap_t *lm, const lightsurf_t *lightsurf)
{
    float avgb = 0;
    for (int j = 0; j < lightsurf->samples.size(); j++) {
        avgb += LightSample_Brightness(lm->samples[j].color);
    }
    avgb /= lightsurf->samples.size();
    return avgb;
}

static float Lightmap_MaxBrightness(const lightmap_t *lm, const lightsurf_t *lightsurf)
{
    float maxb = 0;
    for (int j = 0; j < lightsurf->samples.size(); j++) {
        const float b = LightSample_Brightness(lm->samples[j].color);
        if (b > maxb) {
            maxb = b;
        }
    }
    return maxb;
}

static void SaveLitOnlyLightmapSurface(const mbsp_t *bsp, mface_t *face,
    lightsurf_t *lightsurf, const faceextents_t &extents,
    const faceextents_t &output_extents, std::vector<uint8_t> &filebase, std::vector<uint8_t> &lit_filebase, std::vector<uint8_t> &lux_filebase)
{
    lightmapdict_t &lightmaps = lightsurf->lightmapsByStyle;
    const int actual_width = extents.width();
    const int actual_height = extents.height();
    const int size = output_extents.numsamples();

    // special case for writing a .lit for a bsp without modifying the bsp.
    // involves looking at which styles were written to the bsp in the previous lighting run, and then
    // writing the same styles to the same offsets in the .lit file.

    if (face->lightofs == -1) {
        // nothing to write for this face
        return;
    }

    uint8_t *out = nullptr, *lit = nullptr, *lux = nullptr;

    Q_assert(face->lightofs >= 0);

    if (!filebase.empty()) {
        out = filebase.data() + face->lightofs;
    }

    if (!lit_filebase.empty()) {
        lit = lit_filebase.data() + (face->lightofs * 3);
    }

    if (!lux_filebase.empty()) {
        lux = lux_filebase.data() + (face->lightofs * 3);
    }

    // NOTE: file_p et. al. are not updated, since we're not dynamically allocating the lightmaps

    for (int mapnum = 0; mapnum < MAXLIGHTMAPS; mapnum++) {
        const int style = face->styles[mapnum];

        if (style == 255) {
            break; // all done for this face
        }

        // see if we have computed lighting for this style
        for (const lightmap_t &lm : lightmaps) {
            if (lm.style == style) {
                WriteSingleLightmap(
                    bsp, face, lightsurf, &lm, actual_width, actual_height, out, lit, lux, output_extents);
                break;
            }
        }
        // if we didn't find a matching lightmap, just don't write anything

        if (out) {
            out += size;
        }
        if (lit) {
            lit += (size * 3);
        }
        if (lux) {
            lux += (size * 3);
        }
    }
}

// data stored from Calculate into Save
struct lightmap_intermediate_data_t
{
    std::vector<const lightmap_t *> sorted;
    int lightofs = -1, vanilla_lightofs = -1;
};

// temp
extern std::vector<facesup_t> faces_sup; // lit2/bspx stuff
extern std::vector<bspx_decoupled_lm_perface> facesup_decoupled_global;

int CalculateLightmapStyles(const mbsp_t *bsp, mface_t *face, facesup_t *facesup,
    lightsurf_t *lightsurf, const faceextents_t &extents,
    std::atomic_size_t &lightmap_size,
    lightmap_intermediate_data_t &id)
{
    lightmapdict_t &lightmaps = lightsurf->lightmapsByStyle;

    size_t maxfstyles = std::min((size_t)light_options.facestyles.value(), facesup ? MAXLIGHTMAPSSUP : MAXLIGHTMAPS);
    int maxstyle = facesup ? INVALID_LIGHTSTYLE : INVALID_LIGHTSTYLE_OLD;

    // intermediate collection for sorting lightmaps
    std::vector<std::pair<float, const lightmap_t *>> sortable;

    for (const lightmap_t &lightmap : lightmaps) {
        // skip un-saved lightmaps
        if (lightmap.style == INVALID_LIGHTSTYLE)
            continue;
        if (lightmap.style > maxstyle || (facesup && lightmap.style > INVALID_LIGHTSTYLE_OLD)) {
            if (!warned_about_light_style_overflow) {
                if (IsOutputtingSupplementaryData()) {
                    logging::print(
                        "INFO: a face has exceeded max light style id ({});\n LMSTYLE16 will be output to hold the non-truncated data.\n Use -verbose to find which faces.\n",
                        maxstyle, lightsurf->samples[0].point);
                } else {
                    logging::print(
                        "WARNING: a face has exceeded max light style id ({}). Use -verbose to find which faces.\n",
                        maxstyle, lightsurf->samples[0].point);
                }
                warned_about_light_style_overflow = true;
            }
            logging::print(logging::flag::VERBOSE, "WARNING: Style {} too high on face near {}\n", lightmap.style,
                lightsurf->samples[0].point);
            continue;
        }

        // skip lightmaps where all samples have brightness below 1
        if (bsp->loadversion->game->id != GAME_QUAKE_II) { // HACK: don't do this on Q2. seems if all styles are 0xff,
                                                           // the face is drawn fullbright instead of black (Q1)
            const float maxb = Lightmap_MaxBrightness(&lightmap, lightsurf);
            if (maxb < 1)
                continue;
        }

        const float avgb = Lightmap_AvgBrightness(&lightmap, lightsurf);
        sortable.emplace_back(avgb, &lightmap);
    }

    // HACK: in Q2, if lightofs is -1, then it's drawn fullbright,
    // so we can't optimize away unused portions of the lightmap.
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        if (!sortable.size()) {
            lightmap_t *lm = Lightmap_ForStyle(&lightmaps, 0, lightsurf);
            lm->style = 0;
            for (auto &sample : lightsurf->samples) {
                sample.occluded = false;
            }
            sortable.emplace_back(0, lm);
        }
    }

    // sort in descending order of average brightness
    std::sort(sortable.begin(), sortable.end());
    std::reverse(sortable.begin(), sortable.end());

    for (const auto &pair : sortable) {
        if (id.sorted.size() == maxfstyles) {
            if (!warned_about_light_map_overflow) {
                if (IsOutputtingSupplementaryData()) {
                    logging::print(
                        "INFO: a face has exceeded max light styles ({});\n LMSTYLE/LMSTYLE16 will be output to hold the non-truncated data.\n Use -verbose to find which faces.\n",
                        maxfstyles, lightsurf->samples[0].point);
                } else {
                    logging::print(
                        "WARNING: a face has exceeded max light styles ({}). Use -verbose to find which faces.\n",
                        maxfstyles, lightsurf->samples[0].point);
                }
                warned_about_light_map_overflow = true;
            }
            logging::print(logging::flag::VERBOSE,
                "WARNING: {} light styles (max {}) on face near {}; styles: ", sortable.size(), maxfstyles,
                lightsurf->samples[0].point);
            for (auto &p : sortable) {
                logging::print(logging::flag::VERBOSE, "{} ", p.second->style);
            }
            logging::print(logging::flag::VERBOSE, "\n");
            break;
        }

        id.sorted.push_back(pair.second);
    }

    /* final number of lightmaps */
    const int numstyles = static_cast<int>(id.sorted.size());
    Q_assert(numstyles <= MAXLIGHTMAPSSUP);

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        Q_assert(numstyles > 0);
    }

    return numstyles;
}

void SaveLightmapSurface(const mbsp_t *bsp, mface_t *face, facesup_t *facesup,
    bspx_decoupled_lm_perface *facesup_decoupled, lightsurf_t *lightsurf, const faceextents_t &extents,
    const faceextents_t &output_extents,
    std::vector<uint8_t> &filebase, std::vector<uint8_t> &lit_filebase, std::vector<uint8_t> &lux_filebase,
    lightmap_intermediate_data_t &id)
{
    const int output_width = output_extents.width();
    const int output_height = output_extents.height();

    Q_assert(id.lightofs >= 0);

    /* update face info (either core data or supplementary stuff) */
    if (facesup) {
        facesup->extent[0] = output_width;
        facesup->extent[1] = output_height;
        int mapnum;
        for (mapnum = 0; mapnum < id.sorted.size() && mapnum < MAXLIGHTMAPSSUP; mapnum++) {
            facesup->styles[mapnum] = id.sorted.at(mapnum)->style;
        }
        for (; mapnum < MAXLIGHTMAPSSUP; mapnum++) {
            facesup->styles[mapnum] = INVALID_LIGHTSTYLE;
        }
        facesup->lmscale = lightsurf->lightmapscale;
    } else {
        int mapnum;
        for (mapnum = 0; mapnum < id.sorted.size() && mapnum < MAXLIGHTMAPS; mapnum++) {
            face->styles[mapnum] = id.sorted.at(mapnum)->style;
        }
        for (; mapnum < MAXLIGHTMAPS; mapnum++) {
            face->styles[mapnum] = INVALID_LIGHTSTYLE_OLD;
        }

        if (facesup_decoupled) {
            facesup_decoupled->lmwidth = output_width;
            facesup_decoupled->lmheight = output_height;
            for (size_t i = 0; i < 2; ++i) {
                facesup_decoupled->world_to_lm_space.set_row(i, output_extents.worldToLMMatrix.row(i));
            }
        }
    }

    uint8_t *out = nullptr, *lit = nullptr, *lux = nullptr;

    if (!filebase.empty()) {
        out = filebase.data() + id.lightofs;
    }

    if (!lit_filebase.empty()) {
        lit = lit_filebase.data() + (id.lightofs * 3);
    }

    if (!lux_filebase.empty()) {
        lux = lux_filebase.data() + (id.lightofs * 3);
    }

    int lightofs;
    
    // Q2/HL native colored lightmaps
    if (bsp->loadversion->game->has_rgb_lightmap) {
        lightofs = lit - lit_filebase.data();
    } else {
        lightofs = out - filebase.data();
    }

    if (facesup_decoupled) {
        facesup_decoupled->offset = lightofs;
        face->lightofs = -1;
    } else if (facesup) {
        facesup->lightofs = lightofs;
    } else {
        face->lightofs = lightofs;
    }

    // sanity check that we don't save a lightmap for a non-lightmapped face
    Q_assert(Face_IsLightmapped(bsp, face));

    const int actual_width = extents.width();
    const int actual_height = extents.height();
    const int size = output_extents.numsamples();

    if (out) {
        Q_assert((out - filebase.data()) + (size * id.sorted.size()) <= filebase.size());
    }

    if (lit) {
        Q_assert((lit - lit_filebase.data()) + (size * 3 * id.sorted.size()) <= lit_filebase.size());
    }

    if (lux) {
        Q_assert((lux - lux_filebase.data()) + (size * 3 * id.sorted.size()) <= lux_filebase.size());
    }

    for (int mapnum = 0; mapnum < id.sorted.size(); mapnum++) {
        const lightmap_t *lm = id.sorted.at(mapnum);

        WriteSingleLightmap(bsp, face, lightsurf, lm, actual_width, actual_height, out, lit, lux, output_extents);

        if (out) {
            out += size;
        }
        if (lit) {
            lit += (size * 3);
        }
        if (lux) {
            lux += (size * 3);
        }
    }

    // write vanilla lightmap if -world_units_per_luxel is in use but not -novanilla
    if (facesup_decoupled && !light_options.novanilla.value()) {
        size_t vanilla_size = lightsurf->vanilla_extents.numsamples();

        Q_assert(id.vanilla_lightofs >= 0);

        if (!filebase.empty()) {
            out = filebase.data() + id.vanilla_lightofs;
        }

        if (!lit_filebase.empty()) {
            lit = lit_filebase.data() + (id.vanilla_lightofs * 3);
        }

        if (!lux_filebase.empty()) {
            lux = lux_filebase.data() + (id.vanilla_lightofs * 3);
        }

        // Q2/HL native colored lightmaps
        if (bsp->loadversion->game->has_rgb_lightmap) {
            lightofs = lit - lit_filebase.data();
        } else {
            lightofs = out - filebase.data();
        }
        face->lightofs = lightofs;

        for (int mapnum = 0; mapnum < id.sorted.size(); mapnum++) {
            const lightmap_t *lm = id.sorted.at(mapnum);

            WriteSingleLightmap_FromDecoupled(bsp, face, lightsurf, lm, lightsurf->vanilla_extents.width(),
                lightsurf->vanilla_extents.height(), out, lit, lux);

            if (out) {
                out += vanilla_size;
            }
            if (lit) {
                lit += (vanilla_size * 3);
            }
            if (lux) {
                lux += (vanilla_size * 3);
            }
        }
    }
}

void SaveLightmapSurfaces(bspdata_t *bspdata, const fs::path &source)
{
    mbsp_t *bsp = &std::get<mbsp_t>(bspdata->bsp);

    logging::funcheader();

    warned_about_light_map_overflow = warned_about_light_style_overflow = false;
    fully_transparent_lightmaps = 0;

    // lightmap data storage
    std::vector<uint8_t> filebase, lit_filebase, lux_filebase;

    if (light_options.litonly.value()) {

        if (bsp->dlightdata.empty()) {
            Error("no light data, but litonly was specified");
        } else if (bsp->loadversion->game->has_rgb_lightmap) {
            Error("litonly is only useful for non-RGB lightmap games (Quake)");
        }

        filebase.resize(bsp->dlightdata.size());

        if (light_options.write_litfile) {
            lit_filebase.resize(filebase.size() * 3);
        }

        if (light_options.write_luxfile) {
            lux_filebase.resize(filebase.size() * 3);
        }
        
        logging::parallel_for(static_cast<size_t>(0), bsp->dfaces.size(), [&](size_t i) {
            auto &surf = LightSurfaces()[i];

            if (surf.samples.empty()) {
                return;
            }

            FinishLightmapSurface(bsp, &surf);

            auto f = &bsp->dfaces[i];

            SaveLitOnlyLightmapSurface(bsp, f, &surf, surf.extents, surf.extents, filebase, lit_filebase, lux_filebase);
        });
    } else {
        std::atomic_size_t lightmap_size = 0;
        std::vector<lightmap_intermediate_data_t> intermediate_data;
        intermediate_data.resize(bsp->dfaces.size());

        // calculate finish lightmaps and calculate lightofs for each face.
        // the lightofs will be set to the size in bytes.
        logging::parallel_for(static_cast<size_t>(0), bsp->dfaces.size(), [&](size_t i) {
            auto &surf = LightSurfaces()[i];

            if (surf.samples.empty()) {
                return;
            }

            FinishLightmapSurface(bsp, &surf);

            auto f = &bsp->dfaces[i];
            const modelinfo_t *face_modelinfo = ModelInfoForFace(bsp, i);
            int num_styles;

            if (!facesup_decoupled_global.empty()) {
                num_styles = CalculateLightmapStyles(
                    bsp, f, nullptr, &surf, surf.extents, lightmap_size, intermediate_data[i]);

                if (!light_options.novanilla.value()) {
                    intermediate_data[i].vanilla_lightofs = GetFileSpace(lightmap_size, surf.vanilla_extents.numsamples() * num_styles);
                }
            } else if (faces_sup.empty()) {
                num_styles = CalculateLightmapStyles(bsp, f, nullptr, &surf, surf.extents, lightmap_size, intermediate_data[i]);
            } else if (light_options.novanilla.value() || faces_sup[i].lmscale == face_modelinfo->lightmapscale) {
                num_styles = CalculateLightmapStyles(bsp, f, &faces_sup[i], &surf, surf.extents, lightmap_size, intermediate_data[i]);
            } else {
                num_styles = CalculateLightmapStyles(bsp, f, nullptr, &surf, surf.extents, lightmap_size, intermediate_data[i]);
                intermediate_data[i].vanilla_lightofs = GetFileSpace(lightmap_size, surf.vanilla_extents.numsamples() * num_styles);
            }
            
            if (num_styles) {
                intermediate_data[i].lightofs = GetFileSpace(lightmap_size, surf.extents.numsamples() * num_styles);
            }
        });

        // allocate required space
        if (!bsp->loadversion->game->has_rgb_lightmap) {
            filebase.resize(lightmap_size);
        }

        if (bsp->loadversion->game->has_rgb_lightmap || light_options.write_litfile) {
            lit_filebase.resize(lightmap_size * 3);
        }

        if (light_options.write_luxfile) {
            lux_filebase.resize(lightmap_size * 3);
        }

        logging::print(logging::flag::STAT, "lightmap size (total): {}\n", filebase.size() + lit_filebase.size() + lux_filebase.size());

        logging::parallel_for(static_cast<size_t>(0), bsp->dfaces.size(), [&](size_t i) {
            auto &surf = LightSurfaces()[i];

            if (surf.samples.empty()) {
                return;
            }

            auto f = &bsp->dfaces[i];
            const modelinfo_t *face_modelinfo = ModelInfoForFace(bsp, i);

            if (!facesup_decoupled_global.empty()) {
                SaveLightmapSurface(
                    bsp, f, nullptr, &facesup_decoupled_global[i], &surf, surf.extents, surf.extents, filebase, lit_filebase, lux_filebase, intermediate_data[i]);
            } else if (faces_sup.empty()) {
                SaveLightmapSurface(bsp, f, nullptr, nullptr, &surf, surf.extents, surf.extents, filebase, lit_filebase, lux_filebase, intermediate_data[i]);
            } else if (light_options.novanilla.value() || faces_sup[i].lmscale == face_modelinfo->lightmapscale) {
                if (faces_sup[i].lmscale == face_modelinfo->lightmapscale) {
                    f->lightofs = faces_sup[i].lightofs;
                } else {
                    f->lightofs = -1;
                }
                SaveLightmapSurface(bsp, f, &faces_sup[i], nullptr, &surf, surf.extents, surf.extents, filebase, lit_filebase, lux_filebase, intermediate_data[i]);
                for (int j = 0; j < MAXLIGHTMAPS; j++) {
                    f->styles[j] =
                        faces_sup[i].styles[j] == INVALID_LIGHTSTYLE ? INVALID_LIGHTSTYLE_OLD : faces_sup[i].styles[j];
                }
            } else {
                SaveLightmapSurface(bsp, f, nullptr, nullptr, &surf, surf.extents, surf.vanilla_extents, filebase, lit_filebase, lux_filebase, intermediate_data[i]);
                SaveLightmapSurface(bsp, f, &faces_sup[i], nullptr, &surf, surf.extents, surf.extents, filebase, lit_filebase, lux_filebase, intermediate_data[i]);
            }
        });
    }

    logging::print("Lighting Completed.\n\n");

    if (light_options.write_litfile == lightfile::lit2) {
        WriteLitFile(bsp, faces_sup, source, 2, lit_filebase, lux_filebase);
        return; // run away before any files are written
    }

    // Transfer greyscale lightmap (or color lightmap for Q2/HL) to the bsp and update lightdatasize
    // NOTE: bsp.lightdatasize is already valid in the -litonly case
    if (!light_options.litonly.value()) {
        if (bsp->loadversion->game->has_rgb_lightmap) {
            bsp->dlightdata = lit_filebase; // not moved, because it's used below too
        } else {
            bsp->dlightdata = std::move(filebase);
        }
    }

    bspdata->bspx.entries.erase("RGBLIGHTING");
    bspdata->bspx.entries.erase("LIGHTINGDIR");

    /*fixme: add a new per-surface offset+lmscale lump for compat/versitility?*/
    if (light_options.write_litfile & lightfile::external) {
        WriteLitFile(bsp, faces_sup, source, LIT_VERSION, lit_filebase, lux_filebase);
    }
    if (light_options.write_litfile & lightfile::bspx) {
        lit_filebase.resize(bsp->dlightdata.size() * 3);
        bspdata->bspx.transfer("RGBLIGHTING", lit_filebase);
    }
    if (light_options.write_luxfile & lightfile::external) {
        WriteLuxFile(bsp, source, LIT_VERSION, lux_filebase);
    }
    if (light_options.write_luxfile & lightfile::bspx) {
        lux_filebase.resize(bsp->dlightdata.size() * 3);
        bspdata->bspx.transfer("LIGHTINGDIR", lux_filebase);
    }
}