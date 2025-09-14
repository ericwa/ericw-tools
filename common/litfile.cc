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

#include "common/bspfile_generic.hh"

#include <common/litfile.hh>
#include <common/cmdlib.hh>

#include <algorithm>
#include <fstream>

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

/**
 * Packs a float3 into a 32-bit integer.
 *
 * Follows the OpenGL 4.6 Core spec, section 8.5.2 Encoding of Special Internal Formats.
 *
 * See HDR_UnpackE5BRG9 for the format description.
 */
uint32_t HDR_PackE5BRG9(qvec3f rgb)
{
    constexpr int N = 9; // bits per component
    constexpr int B = 15; // exponent bias
    constexpr int Emax = 31; // max allowed exponent bias value

    // slightly under 2^16
    constexpr float max_representable =
        (static_cast<float>((1 << N) - 1) / static_cast<float>(1 << N)) * static_cast<float>(1 << (Emax - B));

    // clamp inputs
    const float r = std::max(0.0f, std::min(rgb[0], max_representable));
    const float g = std::max(0.0f, std::min(rgb[1], max_representable));
    const float b = std::max(0.0f, std::min(rgb[2], max_representable));

    const float max_comp = std::max(std::max(r, g), b);

    // avoid division by 0 below if the input is (0, 0, 0)
    if (max_comp == 0.0f)
        return 0;

    // preliminary shared exponent
    const int prelim_exponent = std::max(-B - 1, (int)std::floor(std::log2(max_comp))) + 1 + B;

    // refined shared exponent
    const int max_s = (int)std::floor((max_comp / std::pow(2.0f, prelim_exponent - B - N)) + 0.5f);

    int refined_exponent = std::clamp((max_s < (1 << N)) ? prelim_exponent : prelim_exponent + 1, 0, 0x1f);

    const float scale = std::pow(2.0f, refined_exponent - B - N);

    int r_integer = std::clamp((int)std::floor((r / scale) + 0.5), 0, 0x1ff);
    int g_integer = std::clamp((int)std::floor((g / scale) + 0.5), 0, 0x1ff);
    int b_integer = std::clamp((int)std::floor((b / scale) + 0.5), 0, 0x1ff);

    return (refined_exponent << 27) | (b_integer << 18) | (g_integer << 9) | (r_integer << 0);
}

/**
 * Takes a e5bgr9 value as used in the LIGHTING_E5BGR9 lump and unpacks it into a float3
 * in the order (red, green, blue).
 *
 * The packed format is, from highest-order to lowest-order bits:
 *
 * - top 5 bits: biased_exponent in [0, 31]
 * - next 9 bits: blue_int in [0, 511]
 * - next 9 bits: green_int in [0, 511]
 * - bottom 9 bits: red_int in [0, 511]
 *
 * the conversion to floating point goes like:
 *
 * blue_float = 2^(biased_exponent - 24) * blue_int
 *
 * this is following OpenGL 4.6 Core spec, section 8.25 Shared Exponent Texture Color Conversion
 */
qvec3f HDR_UnpackE5BRG9(uint32_t packed)
{
    // grab the top 5 bits. this is a value in [0, 31].
    const uint32_t biased_exponent = packed >> 27;
    // the actual exponent gets remapped to the range [-24, 7].
    const int exponent = static_cast<int>(biased_exponent) - 24;

    const uint32_t blue_int = (packed >> 18) & 0x1ff;
    const uint32_t green_int = (packed >> 9) & 0x1ff;
    const uint32_t red_int = packed & 0x1ff;

    const float multiplier = std::pow(2.0f, static_cast<float>(exponent));

    return qvec3f(red_int, green_int, blue_int) * multiplier;
}

lit_variant_t LoadLitFile(const fs::path &path, const mbsp_t &bsp)
{
    std::ifstream stream(path, std::ios_base::in | std::ios_base::binary);
    if (!stream.good()) {
        return {lit_none()};
    }

    stream >> endianness<std::endian::little>;

    std::array<char, 4> ident;
    stream >= ident;
    if (ident != std::array<char, 4>{'Q', 'L', 'I', 'T'}) {
        throw std::runtime_error("invalid lit ident");
    }

    int version;
    stream >= version;
    if (version == LIT_VERSION) {
        std::vector<uint8_t> litdata;
        uint8_t b;
        while (stream >= b && stream.good()) {
            litdata.push_back(b);
        }

        // validate data length
        if (litdata.size() != bsp.lightsamples() * 3) {
            throw std::runtime_error("incorrect lit size");
        }

        return {lit1_t{.rgbdata = std::move(litdata)}};
    } else if (version == LIT_VERSION_E5BGR9) {
        std::vector<uint32_t> hdrsamples;

        uint32_t sample;
        while (stream >= sample && stream.good()) {
            hdrsamples.push_back(sample);
        }

        // validate data length
        if (hdrsamples.size() != bsp.lightsamples()) {
            throw std::runtime_error("incorrect hdr lit size");
        }

        return {lit_hdr{.samples = std::move(hdrsamples)}};
    }

    throw std::runtime_error("invalid lit version");
}
