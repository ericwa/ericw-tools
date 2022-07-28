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

#pragma once

#include <common/bspfile.hh>
#include <common/mathlib.hh>
#include <common/qvec.hh>
#include <common/polylib.hh>

#include <string>
#include <vector>

const dmodelh2_t *BSP_GetWorldModel(const mbsp_t *bsp);
int Face_GetNum(const mbsp_t *bsp, const mface_t *f);

// bounds-checked array access (assertion failure on out-of-bounds)
const bsp2_dnode_t *BSP_GetNode(const mbsp_t *bsp, int nodenum);
const mleaf_t *BSP_GetLeaf(const mbsp_t *bsp, int leafnum);
const mleaf_t *BSP_GetLeafFromNodeNum(const mbsp_t *bsp, int nodenum);
const dplane_t *BSP_GetPlane(const mbsp_t *bsp, int planenum);
const mface_t *BSP_GetFace(const mbsp_t *bsp, int fnum);
const mtexinfo_t *BSP_GetTexinfo(const mbsp_t *bsp, int texinfo);
mface_t *BSP_GetFace(mbsp_t *bsp, int fnum);

int Face_VertexAtIndex(const mbsp_t *bsp, const mface_t *f, int v);
const qvec3f &Face_PointAtIndex(const mbsp_t *bsp, const mface_t *f, int v);
qplane3d Face_Plane(const mbsp_t *bsp, const mface_t *f);
const mtexinfo_t *Face_Texinfo(const mbsp_t *bsp, const mface_t *face);
const miptex_t *Face_Miptex(const mbsp_t *bsp, const mface_t *face);
const char *Face_TextureName(const mbsp_t *bsp, const mface_t *face);
const qvec3f &GetSurfaceVertexPoint(const mbsp_t *bsp, const mface_t *f, int v);
bool ContentsOrSurfaceFlags_IsTranslucent(const mbsp_t *bsp, int contents_or_surf_flags); // mxd
bool Face_IsTranslucent(const mbsp_t *bsp, const mface_t *face); // mxd
int Face_ContentsOrSurfaceFlags(
    const mbsp_t *bsp, const mface_t *face); // mxd. Returns CONTENTS_ value for Q1, Q2_SURF_ bitflags for Q2...
const dmodelh2_t *BSP_DModelForModelString(const mbsp_t *bsp, const std::string &submodel_str);
bool Light_PointInSolid(const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point);
bool Light_PointInWorld(const mbsp_t *bsp, const qvec3d &point);

std::vector<const mface_t *> BSP_FindFacesAtPoint(
    const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point, const qvec3d &wantedNormal = qvec3d(0, 0, 0));
/**
 * Searches for a face touching a point and facing a certain way.
 * Sometimes (water, sky?) there will be 2 overlapping candidates facing opposite ways, the provided normal
 * is used to disambiguate these.
 */
const mface_t *BSP_FindFaceAtPoint(
    const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point, const qvec3d &wantedNormal);
/**
 * Searches for a decision node in hull0 that contains `point`, and has a plane normal of either
 * wanted_normal or -wanted_normal.
 */
const bsp2_dnode_t *BSP_FindNodeAtPoint(
    const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point, const qvec3d &wanted_normal);

const mleaf_t *BSP_FindLeafAtPoint(const mbsp_t *bsp, const dmodelh2_t *model, const qvec3d &point);
int BSP_FindContentsAtPoint(const mbsp_t *bsp, int hull, const dmodelh2_t *model, const qvec3d &point);

std::vector<const mface_t *> Leaf_Markfaces(const mbsp_t *bsp, const mleaf_t *leaf);
std::vector<const dbrush_t *> Leaf_Brushes(const mbsp_t *bsp, const mleaf_t *leaf);
const qvec3f &Face_PointAtIndex(const mbsp_t *bsp, const mface_t *f);
const qvec3f &Vertex_GetPos(const mbsp_t *bsp, int num);
qvec3d Face_Normal(const mbsp_t *bsp, const mface_t *f);
std::vector<qvec3f> GLM_FacePoints(const mbsp_t *bsp, const mface_t *face);
polylib::winding_t Face_Winding(const mbsp_t *bsp, const mface_t *face);
qvec3f Face_Centroid(const mbsp_t *bsp, const mface_t *face);
void Face_DebugPrint(const mbsp_t *bsp, const mface_t *face);

#include <vector>

void CompressRow(const uint8_t *vis, const size_t numbytes, std::back_insert_iterator<std::vector<uint8_t>> it);
void DecompressRow(const uint8_t *in, const int numbytes, uint8_t *decompressed);

/* ======================================================================== */

inline qvec2d WorldToTexCoord(const qvec3d &world, const mtexinfo_t *tex)
{
    /*
     * The (long double) casts below are important: The original code
     * was written for x87 floating-point which uses 80-bit floats for
     * intermediate calculations. But if you compile it without the
     * casts for modern x86_64, the compiler will round each
     * intermediate result to a 32-bit float, which introduces extra
     * rounding error.
     *
     * This becomes a problem if the rounding error causes the light
     * utilities and the engine to disagree about the lightmap size
     * for some surfaces.
     *
     * Casting to (long double) keeps the intermediate values at at
     * least 64 bits of precision, probably 128.
     */
    return tex->vecs.uvs<long double>(world);
}

inline qvec2f Face_WorldToTexCoord(const mbsp_t *bsp, const mface_t *face, const qvec3f &world)
{
    const mtexinfo_t *tex = Face_Texinfo(bsp, face);

    if (tex == nullptr)
        return {};

    return WorldToTexCoord(world, tex);
}

inline qmat4x4f WorldToTexSpace(const mbsp_t *bsp, const mface_t *f)
{
    const mtexinfo_t *tex = Face_Texinfo(bsp, f);
    if (tex == nullptr) {
        Q_assert_unreachable();
        return qmat4x4f();
    }
    const qplane3d plane = Face_Plane(bsp, f);

    //           [s]
    // T * vec = [t]
    //           [distOffPlane]
    //           [?]

    qmat4x4f T{
        tex->vecs.at(0, 0), tex->vecs.at(1, 0), static_cast<float>(plane.normal[0]), 0, // col 0
        tex->vecs.at(0, 1), tex->vecs.at(1, 1), static_cast<float>(plane.normal[1]), 0, // col 1
        tex->vecs.at(0, 2), tex->vecs.at(1, 2), static_cast<float>(plane.normal[2]), 0, // col 2
        tex->vecs.at(0, 3), tex->vecs.at(1, 3), static_cast<float>(-plane.dist), 1 // col 3
    };
    return T;
}

inline qmat4x4f TexSpaceToWorld(const mbsp_t *bsp, const mface_t *f)
{
    return qv::inverse(WorldToTexSpace(bsp, f));
}

/* for vanilla this would be 18. some engines allow higher limits though, which will be needed if we're scaling lightmap
 * resolution. */
/*with extra sampling, lit+lux etc, we need at least 46mb space per thread. yes, that's a lot. on the plus side,
 * it doesn't affect bsp complexity (actually, can simplify it a little)*/
constexpr size_t MAXDIMENSION = 255 + 1;

class faceextents_t
{
public:
    qvec2i texmins;
    qvec2i texextents;
    float lightmapshift;
    qmat4x4f worldToTexCoordMatrix;
    qmat4x4f texCoordToWorldMatrix;

    qvec3d origin;
    vec_t radius;
    aabb3d bounds;
    qvec2d exact_mid;

    faceextents_t() = default;

    inline faceextents_t(const mface_t &face, const mbsp_t &bsp, float lmshift) : lightmapshift(lmshift)
    {
        worldToTexCoordMatrix = WorldToTexSpace(&bsp, &face);
        texCoordToWorldMatrix = TexSpaceToWorld(&bsp, &face);

        aabb2d tex_bounds;

        for (int i = 0; i < face.numedges; i++) {
            const qvec3f &worldpoint = Face_PointAtIndex(&bsp, &face, i);
            const qvec2f texcoord = Face_WorldToTexCoord(&bsp, &face, worldpoint);

#ifdef PARANOID
            // self test
            auto texcoordRT = this->worldToTexCoord(worldpoint);
            auto worldpointRT = this->texCoordToWorld(texcoord);
            Q_assert(qv::epsilonEqual(texcoordRT, texcoord, 0.1f));
            Q_assert(qv::epsilonEqual(worldpointRT, worldpoint, 0.1f));
            // end self test
#endif

            tex_bounds += texcoord;
            bounds += worldpoint;
        }

        for (int i = 0; i < 2; i++) {
            tex_bounds[0][i] = floor(tex_bounds[0][i] / lightmapshift);
            tex_bounds[1][i] = ceil(tex_bounds[1][i] / lightmapshift);
            texmins[i] = static_cast<int>(tex_bounds[0][i]);
            texextents[i] = static_cast<int>(tex_bounds[1][i] - tex_bounds[0][i]);

            if (texextents[i] >= MAXDIMENSION * (16.0 / lightmapshift)) {
                const qplane3d plane = Face_Plane(&bsp, &face);
                const qvec3f &point = Face_PointAtIndex(&bsp, &face, 0); // grab first vert
                const char *texname = Face_TextureName(&bsp, &face);

                logging::print("WARNING: Bad surface extents (may not load in vanilla Q1 engines):\n"
                               "   surface {}, {} extents = {}, shift = {}\n"
                               "   texture {} at ({})\n"
                               "   surface normal ({})\n",
                    Face_GetNum(&bsp, &face), i ? "t" : "s", texextents[i], lightmapshift, texname, point,
                    plane.normal);
            }
        }

        exact_mid = Face_WorldToTexCoord(&bsp, &face, Face_Centroid(&bsp, &face));

        // calculate a bounding sphere for the face
        qvec3d radius = (bounds.maxs() - bounds.mins()) * 0.5;

        origin = bounds.mins() + radius;
        this->radius = qv::length(radius);
    }

    constexpr int width() const
    {
        return texextents[0] + 1;
    }
    constexpr int height() const
    {
        return texextents[1] + 1;
    }
    constexpr int numsamples() const
    {
        return width() * height();
    }
    constexpr qvec2i texsize() const
    {
        return {width(), height()};
    }

    constexpr qvec2f lightmapCoordToTexCoord(const qvec2f &LMCoord) const
    {
        return {lightmapshift * (texmins[0] + LMCoord[0]), lightmapshift * (texmins[1] + LMCoord[1])};
    }

    constexpr qvec2f texCoordToLightmapCoord(const qvec2f &tc) const
    {
        return {(tc[0] / lightmapshift) - texmins[0], (tc[1] / lightmapshift) - texmins[1]};
    }

    inline qvec2f worldToTexCoord(qvec3f world) const
    {
        const qvec4f worldPadded(world, 1.0f);
        const qvec4f res = worldToTexCoordMatrix * worldPadded;

        Q_assert(res[3] == 1.0f);

        return res;
    }

    inline qvec3f texCoordToWorld(qvec2f tc) const
    {
        const qvec4f tcPadded(tc[0], tc[1], 0.0f, 1.0f);
        const qvec4f res = texCoordToWorldMatrix * tcPadded;

        Q_assert(fabs(res[3] - 1.0f) < 0.01f);

        return res;
    }

    inline qvec2f worldToLMCoord(qvec3f world) const
    {
        return texCoordToLightmapCoord(worldToTexCoord(world));
    }

    inline qvec3f LMCoordToWorld(qvec2f lm) const
    {
        return texCoordToWorld(lightmapCoordToTexCoord(lm));
    }
};