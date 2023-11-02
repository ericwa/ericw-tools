/*
    Copyright (C) 2021       Eric Wasylishen

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

#include <common/decompile.hh>

#include <common/entdata.h>
#include <common/cmdlib.hh>
#include <common/bspfile.hh>
#include <common/bsputils.hh>
#include <common/mathlib.hh>
#include <common/polylib.hh>
#include <common/fs.hh>
#include <common/log.hh>
#include <common/ostream.hh>

#include <fstream>
#include <vector>
#include <cstdio>
#include <string>
#include <utility>
#include <tuple>

#include <fmt/core.h>

#include "tbb/parallel_for.h"

// texturing

struct texdef_valve_t
{
    qmat<vec_t, 2, 3> axis{};
    qvec2d scale{1.0};
    qvec2d shift{};

    constexpr texdef_valve_t() = default;

    // create a base/default texdef
    inline texdef_valve_t(const qvec3d &normal)
    {
        const size_t normalAxis = qv::indexOfLargestMagnitudeComponent(normal);

        if (normalAxis == 2) {
            axis.set_row(0, qv::normalize(qv::cross(qvec3d{0, 1, 0}, normal)));
        } else {
            axis.set_row(0, qv::normalize(qv::cross(qvec3d{0, 0, 1}, normal)));
        }

        axis.set_row(1, qv::normalize(qv::cross(axis.row(0), normal)));
    }

    // FIXME: merge with map.cc copy
    inline texdef_valve_t(const texvecf &in_vecs)
    {
        // From the valve -> bsp code,
        //        out->vecs[n].xyz = axis[n].xyz / scale[n];
        // We'll generate axis vectors of length 1 and pick the necessary scale

        for (int i = 0; i < 2; i++) {
            qvec3d xyz = in_vecs.row(i).xyz();
            const vec_t length = qv::normalizeInPlace(xyz);
            // avoid division by 0
            if (length != 0.0) {
                scale[i] = 1.0 / length;
            } else {
                scale[i] = 0.0;
            }
            shift[i] = in_vecs.at(i, 3);
            axis.set_row(i, xyz);
        }
    }
};

struct compiled_brush_side_t
{
    qplane3d plane;
    std::string texture_name;
    texdef_valve_t valve;
    std::optional<polylib::winding_t> winding;

    // only for Q2
    surfflags_t flags;
    int32_t value;

    const q2_dbrushside_qbism_t *source = nullptr;
};

struct planepoints : std::array<qvec3d, 3>
{
    qplane3d plane() const
    {
        /* calculate the normal/dist plane equation */
        qvec3d ab = at(0) - at(1);
        qvec3d cb = at(2) - at(1);
        qvec3d normal = qv::normalize(qv::cross(ab, cb));
        return {normal, qv::dot(at(1), normal)};
    }
};

template<typename T>
static planepoints NormalDistanceToThreePoints(const qplane3<T> &plane)
{
    std::tuple<qvec3d, qvec3d> tanBitan = qv::MakeTangentAndBitangentUnnormalized(plane.normal);

    qvec3d point0 = plane.normal * plane.dist;

    return {point0, point0 + std::get<1>(tanBitan), point0 + std::get<0>(tanBitan)};
}

static planepoints WindingToThreePoints(const polylib::winding_t &winding)
{
    Q_assert(winding.size() >= 3);
    return {winding[0], winding[1], winding[2]};
}

struct wal_metadata_t
{
    int32_t flags, contents, value;

    auto stream_data() { return std::tie(flags, contents, value); }
};

static std::unordered_map<std::string, wal_metadata_t> wals;

struct compiled_brush_t
{
    const dbrush_t *source = nullptr;
    std::vector<compiled_brush_side_t> sides;
    std::optional<qvec3d> brush_offset;
    contentflags_t contents;

    inline void write(const mbsp_t *bsp, std::ofstream &stream)
    {
        if (!sides.size()) {
            return;
        }

        if (source) {
            ewt::print(stream, "// generated from brush #{}\n", static_cast<ptrdiff_t>(source - bsp->dbrushes.data()));
        }

        ewt::print(stream, "{{\n");

        for (auto &side : sides) {
            planepoints p;
            // HACK: area() test: if winding is tiny, don't trust it to generate a reasonable normal, just
            // use the known normal/distance
            if (side.winding && side.winding->size() && side.winding->area() > 1) {
                p = WindingToThreePoints(*side.winding);
            } else {
                p = NormalDistanceToThreePoints(side.plane);
            }

            if (brush_offset.has_value()) {
                for (auto &v : p) {
                    v += brush_offset.value();
                }

                side.valve.shift[0] -= qv::dot(brush_offset.value(), side.valve.axis.row(0));
                side.valve.shift[1] -= qv::dot(brush_offset.value(), side.valve.axis.row(1));
            }

#if 0
            ewt::print(stream, "// side #{}: {} {}\n", static_cast<ptrdiff_t>(side.source -
                bsp->dbrushsides.data()), side.plane.normal, side.plane.dist);
#endif

            ewt::print(stream, "( {} ) ( {} ) ( {} ) {} [ {} {} {} {} ] [ {} {} {} {} ] {} {} {}", p[0], p[1], p[2],
                side.texture_name, side.valve.axis.at(0, 0), side.valve.axis.at(0, 1), side.valve.axis.at(0, 2),
                side.valve.shift[0], side.valve.axis.at(1, 0), side.valve.axis.at(1, 1), side.valve.axis.at(1, 2),
                side.valve.shift[1], 0.0, side.valve.scale[0], side.valve.scale[1]);

            if (bsp->loadversion->game->id == GAME_QUAKE_II && (contents.native || side.flags.native || side.value)) {
                wal_metadata_t *meta = nullptr;

                auto it = wals.find(side.texture_name);

                if (it != wals.end()) {
                    meta = &it->second;
                } else {
                    auto wal = fs::load((fs::path("textures") / side.texture_name) += ".wal");

                    if (wal) {
                        imemstream stream(wal->data(), wal->size(), std::ios_base::in | std::ios_base::binary);
                        stream >> endianness<std::endian::little>;
                        stream.seekg(88);

                        meta = &wals.emplace(side.texture_name, wal_metadata_t{}).first->second;
                        stream >= *meta;
                    }
                }

                if (!meta || !((meta->contents & ~(Q2_CONTENTS_SOLID | Q2_CONTENTS_WINDOW)) ==
                                     (contents.native & ~(Q2_CONTENTS_SOLID | Q2_CONTENTS_WINDOW)) &&
                                 meta->flags == side.flags.native && meta->value == side.value)) {
                    ewt::print(stream, " {} {} {}", contents.native, side.flags.native, side.value);
                }
            }

            ewt::print(stream, "\n");
        }

        ewt::print(stream, "}}\n");
    }
};

// this should be an outward-facing plane
struct decomp_plane_t : qplane3d
{
    const bsp2_dnode_t *node = nullptr; // can be nullptr
    const q2_dbrushside_qbism_t *source = nullptr;
    const bsp2_dclipnode_t *clipnode = nullptr; // can be nullptr
};

// brush creation

template<typename T>
void RemoveRedundantPlanes(std::vector<T> &planes)
{
    auto removed = std::remove_if(planes.begin(), planes.end(), [&planes](const T &plane) {
        // outward-facing plane
        std::optional<polylib::winding_t> winding = polylib::winding_t::from_plane(plane, 10e6);

        // clip `winding` by all of the other planes, flipped
        for (const T &plane2 : planes) {
            if (&plane2 == &plane)
                continue;

            // get flipped plane
            // frees winding.
            // discard the back, continue clipping the front part
            winding = winding->clip_front(-plane2);

            // check if everything was clipped away
            if (!winding)
                break;
        }

        return !winding;
    });
    planes.erase(removed, planes.end());
}

// structures representing a brush

struct decomp_brush_face_t
{
    /**
     * The currently clipped section of the face.
     * May be nullopt to indicate it was clipped away.
     */
    std::optional<polylib::winding_t> winding;
    /**
     * The face we were originally derived from
     */
    const mface_t *original_face;

    std::vector<qvec4f> inwardFacingEdgePlanes;

private:
    void buildInwardFacingEdgePlanes()
    {
        if (!winding) {
            return;
        }
        inwardFacingEdgePlanes = MakeInwardFacingEdgePlanes(winding->glm_winding_points());
    }

public:
    decomp_brush_face_t()
        : winding(std::nullopt),
          original_face(nullptr)
    {
    }

    decomp_brush_face_t(const mbsp_t *bsp, const mface_t *face)
        : winding(polylib::winding_t::from_face(bsp, face)),
          original_face(face)
    {
        buildInwardFacingEdgePlanes();
    }

    decomp_brush_face_t(std::optional<polylib::winding_t> &&windingToTakeOwnership, const mface_t *face)
        : winding(std::move(windingToTakeOwnership)),
          original_face(face)
    {
        buildInwardFacingEdgePlanes();
    }

    // FIXME
    decomp_brush_face_t(const decomp_brush_face_t &face)
        : winding(face.winding ? decltype(winding)(face.winding->clone()) : std::nullopt),
          original_face(face.original_face),
          inwardFacingEdgePlanes(face.inwardFacingEdgePlanes)
    {
    }

    decomp_brush_face_t &operator=(const decomp_brush_face_t &copy)
    {
        winding = copy.winding ? decltype(winding)(copy.winding->clone()) : std::nullopt;
        original_face = copy.original_face;
        inwardFacingEdgePlanes = copy.inwardFacingEdgePlanes;
        return *this;
    }

public:
    /**
     * Returns the { front, back } after the clip.
     */
    std::pair<decomp_brush_face_t, decomp_brush_face_t> clipToPlane(const qplane3d &plane) const
    {
        auto clipped = winding->clip(plane);

        // front or back may be null (if fully clipped).
        // these constructors take ownership of the winding.
        return std::make_pair(decomp_brush_face_t(std::move(clipped[0]), original_face),
            decomp_brush_face_t(std::move(clipped[1]), original_face));
    }

    qvec3d normal() const { return winding->plane().normal; }
};

struct leaf_decompile_task
{
    std::vector<decomp_plane_t> allPlanes;
    const mleaf_t *leaf = nullptr;
    const dbrush_t *brush = nullptr;
    const dmodelh2_t *model = nullptr;
    std::optional<int32_t> contents = std::nullopt; // for clipnodes
};

/**
 * Builds the initial list of faces on the node
 */
static std::vector<decomp_brush_face_t> BuildDecompFacesOnPlane(
    const mbsp_t *bsp, const leaf_decompile_task &task, const decomp_plane_t &plane)
{
    if (plane.node == nullptr) {
        return {};
    }

    std::vector<decomp_brush_face_t> result;

    const bsp2_dnode_t *node = plane.node;

    result.reserve(static_cast<size_t>(node->numfaces));

    for (int i = 0; i < node->numfaces; i++) {
        const mface_t *face = BSP_GetFace(bsp, static_cast<int>(node->firstface) + i);

        decomp_brush_face_t decompFace(bsp, face);

        const double dp = qv::dot(plane.normal, decompFace.normal());

        if (dp < 0.9) {
            continue;
        }

        result.emplace_back(bsp, face);
    }

    return result;
}

struct decomp_brush_side_t
{
    /**
     * During decompilation, we can have multiple faces on a single plane of the brush.
     * All vertices of these should lie on the plane.
     */
    std::vector<decomp_brush_face_t> faces;
    decomp_plane_t plane;
    // for Q2 path
    polylib::winding_t winding;

    decomp_brush_side_t(const mbsp_t *bsp, const leaf_decompile_task &model, const decomp_plane_t &planeIn)
        : faces(BuildDecompFacesOnPlane(bsp, model, planeIn)),
          plane(planeIn)
    {
    }

    decomp_brush_side_t(const std::vector<decomp_brush_face_t> &facesIn, const decomp_plane_t &planeIn)
        : faces(facesIn),
          plane(planeIn)
    {
    }

    // FIXME
    decomp_brush_side_t(const decomp_brush_side_t &copy)
        : faces(copy.faces),
          plane(copy.plane),
          winding(copy.winding.clone())
    {
    }

    decomp_brush_side_t &operator=(const decomp_brush_side_t &copy)
    {
        faces = copy.faces;
        plane = copy.plane;
        winding = copy.winding.clone();
        return *this;
    }

    /**
     * Construct a new side with no faces on it, with the given outward-facing plane
     */
    decomp_brush_side_t(const qvec3d &normal, double distance)
        : faces(),
          plane({{normal, distance}})
    {
    }

    /**
     * Returns the { front, back } after the clip.
     */
    std::tuple<decomp_brush_side_t, decomp_brush_side_t> clipToPlane(const qplane3d &plane) const
    {
        // FIXME: assert normal/distance are not our plane

        std::vector<decomp_brush_face_t> frontfaces, backfaces;

        for (auto &face : faces) {
            auto [faceFront, faceBack] = face.clipToPlane(plane);
            if (faceFront.winding) {
                frontfaces.emplace_back(std::move(faceFront));
            }
            if (faceBack.winding) {
                backfaces.emplace_back(std::move(faceBack));
            }
        }

        return {decomp_brush_side_t(std::move(frontfaces), this->plane),
            decomp_brush_side_t(std::move(backfaces), this->plane)};
    }
};

struct decomp_brush_t
{
    std::vector<decomp_brush_side_t> sides;

    decomp_brush_t(std::vector<decomp_brush_side_t> sidesIn)
        : sides(std::move(sidesIn))
    {
    }

    /**
     * Returns the front and back side after clipping to the given plane.
     */
    std::tuple<decomp_brush_t, decomp_brush_t> clipToPlane(const qplane3d &plane) const
    {
        // FIXME: this won't handle the case where the given plane is one of the brush planes

        std::vector<decomp_brush_side_t> frontSides, backSides;

        for (const auto &side : sides) {
            auto [frontSide, backSide] = side.clipToPlane(plane);
            frontSides.emplace_back(frontSide);
            backSides.emplace_back(backSide);
        }

        // NOTE: the frontSides, backSides vectors will have redundant planes at this point. Should be OK..

        // Now we need to add the splitting plane itself to the sides vectors
        frontSides.emplace_back(-plane.normal, -plane.dist);
        backSides.emplace_back(plane.normal, plane.dist);

        return {decomp_brush_t(frontSides), decomp_brush_t(backSides)};
    }

    bool checkPoints() const
    {
        for (auto &side : sides) {
            for (auto &face : side.faces) {
                for (auto &point : face.winding.value()) {
                    // check against all planes
                    for (auto &otherSide : sides) {
                        float distance = DistAbovePlane(qvec4f(otherSide.plane.normal, otherSide.plane.dist), point);
                        if (distance > 0.1) {
                            return false;
                        }
                    }
                }
            }
        }
        return true;
    }
};

static const char *DefaultSkipTexture(const mbsp_t *bsp)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        return "e1u1/skip";
    } else {
        return "skip";
    }
}

static void DefaultSkipSide(compiled_brush_side_t &side, const mbsp_t *bsp)
{
    side.texture_name = DefaultSkipTexture(bsp);

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        side.flags = {Q2_SURF_NODRAW};
    }
}

static const char *DefaultTriggerTexture(const mbsp_t *bsp)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        return "e1u1/trigger";
    } else {
        return "trigger";
    }
}

static void DefaultTriggerSide(compiled_brush_side_t &side, const mbsp_t *bsp)
{
    side.texture_name = DefaultTriggerTexture(bsp);

    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        side.flags = {Q2_SURF_NODRAW};
    }
}

static const char *DefaultOriginTexture(const mbsp_t *bsp)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        return "e1u1/origin";
    } else {
        return "origin";
    }
}

static const char *DefaultTextureForContents(const mbsp_t *bsp, const contentflags_t &contents)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        int visible = contents.native & Q2_ALL_VISIBLE_CONTENTS;

        if (visible & Q2_CONTENTS_WATER) {
            return "e1u1/water4";
        } else if (visible & Q2_CONTENTS_SLIME) {
            return "e1u1/sewer1";
        } else if (visible & Q2_CONTENTS_LAVA) {
            return "e1u1/brlava";
        } else if (contents.native & Q2_CONTENTS_PLAYERCLIP) {
            return "e1u1/clip";
        } else if (contents.native & Q2_CONTENTS_MONSTERCLIP) {
            return "e1u1/clip_mon";
        } else if (contents.native & Q2_CONTENTS_AREAPORTAL) {
            return "e1u1/trigger";
        }

        return "e1u1/skip";
    } else {
        switch (contents.native) {
            case CONTENTS_WATER: return "*waterskip";
            case CONTENTS_SLIME: return "*slimeskip";
            case CONTENTS_LAVA: return "*lavaskip";
            case CONTENTS_SKY: return "skyskip";
            default: return "skip";
        }
    }
}

// some faces can be given an incorrect-but-matching texture if they
// don't actually have a rendered face to pull in, so we're gonna
// replace the texture here with something more appropriate.
static void OverrideTextureForContents(
    compiled_brush_side_t &side, const mbsp_t *bsp, const char *name, const contentflags_t &contents)
{
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {

        if (contents.native & (Q2_CONTENTS_PLAYERCLIP | Q2_CONTENTS_MONSTERCLIP)) {
            if (!(contents.native & Q2_CONTENTS_PLAYERCLIP)) {
                side.texture_name = "e1u1/clip_mon";
            } else {
                side.texture_name = "e1u1/clip";
            }

            side.flags = {Q2_SURF_NODRAW};
            return;
        }
    }

    side.texture_name = name;
}

/***
 * Preconditions: planes are exactly the planes that define the brush
 *
 * @returns a brush object which has the faces from the .bsp clipped to
 * the parts that lie on the brush.
 */
static decomp_brush_t BuildInitialBrush(
    const mbsp_t *bsp, const leaf_decompile_task &task, const std::vector<decomp_plane_t> &planes)
{
    std::vector<decomp_brush_side_t> sides;

    for (const decomp_plane_t &plane : planes) {
        decomp_brush_side_t side(bsp, task, plane);

        // clip `side` by all of the other planes, and keep the back portion
        for (const decomp_plane_t &plane2 : planes) {
            if (&plane2 == &plane)
                continue;

            auto [front, back] = side.clipToPlane(plane2);

            side = back;
        }

        // NOTE: side may have had all of its faces clipped away, but we still need to keep it
        // as it's one of the final boundaries of the brush

        sides.emplace_back(std::move(side));
    }

    return decomp_brush_t(sides);
}

static decomp_brush_t BuildInitialBrush_Q2(
    const mbsp_t *bsp, const leaf_decompile_task &task, const std::vector<decomp_plane_t> &planes)
{
    std::vector<decomp_brush_side_t> sides;

    // flag for whether a given index in `planes` gets fully clipped away
    std::vector<bool> clipped_away;
    clipped_away.resize(planes.size(), false);

    for (int i = planes.size() - 1; i >= 0; --i) {
        const decomp_plane_t &plane = planes[i];

        // FIXME: use a better max
        auto winding = std::make_optional(polylib::winding_t::from_plane(plane, 10e6));

        // clip `winding` by all of the other planes, and keep the back portion
        for (size_t j = 0; j < planes.size(); ++j) {
            const decomp_plane_t &plane2 = planes[j];

            if (i == j)
                continue;

            if (clipped_away[j]) {
                // once a plane gets fully clipped away, don't use it for further clips.
                // this ensures that e.g. if a brush contains 2 +X faces, the second one "wins",
                // and we output a properly formed brush (not an "open" brush).
                continue;
            }

            if (!winding)
                break;

            // FIXME: epsilon is a lot larger than what qbsp uses
            winding = winding->clip_front(-plane2, DEFAULT_ON_EPSILON, false);
        }

        if (!winding) {
            // this shouldn't normally happen, means the brush contains redundant planes
            clipped_away[i] = true;
            continue;
        }

        winding->remove_colinear();

        if (winding->size() < 3)
            continue;

        auto side = decomp_brush_side_t(bsp, task, plane);
        side.winding = std::move(*winding);

        sides.emplace_back(side);
    }

    return decomp_brush_t(sides);
}

static bool SideNeedsSplitting(const mbsp_t *bsp, const decomp_brush_side_t &side)
{
    if (side.faces.size() <= 1) {
        return false;
    }

    const auto &firstFace = side.faces[0];
    for (size_t i = 1; i < side.faces.size(); ++i) {
        const auto &thisFace = side.faces[i];

        if (firstFace.original_face->texinfo != thisFace.original_face->texinfo) {
            return true;
        }
    }

    return false;
}

static qvec4f SuggestSplit(const mbsp_t *bsp, const decomp_brush_side_t &side)
{
    assert(SideNeedsSplitting(bsp, side));

    size_t bestFaceCount = SIZE_MAX;
    qvec4f bestPlane{};

    // for all possible splits:
    for (const auto &face : side.faces) {
        for (const qvec4f &split : face.inwardFacingEdgePlanes) {
            // this is a potential splitting plane.

            auto [front, back] = side.clipToPlane({split.xyz(), split[3]});

            // we only consider splits that have at least 1 face on the front and back
            if (front.faces.empty()) {
                continue;
            }
            if (back.faces.empty()) {
                continue;
            }

            const size_t totalFaceCountWithThisSplit = front.faces.size() + back.faces.size();

            if (totalFaceCountWithThisSplit < bestFaceCount) {
                bestFaceCount = totalFaceCountWithThisSplit;
                bestPlane = split;
            }
        }
    }

    // FIXME: this hits on a Q2 map. need to figure out why. works
    // fine without it though.
    // assert(bestFaceCount != SIZE_MAX);
    return bestPlane;
}

static void SplitDifferentTexturedPartsOfBrush_R(
    const mbsp_t *bsp, const decomp_brush_t &brush, std::vector<decomp_brush_t> &out)
{
    for (auto &side : brush.sides) {
        if (SideNeedsSplitting(bsp, side)) {
            qvec4f split = SuggestSplit(bsp, side);

            if (qv::emptyExact(split)) {
                return;
            }

            auto [front, back] = brush.clipToPlane({split.xyz(), split[3]});

            SplitDifferentTexturedPartsOfBrush_R(bsp, front, out);
            SplitDifferentTexturedPartsOfBrush_R(bsp, back, out);
            return;
        }
    }

    // nothing needed splitting
    out.push_back(brush);
}

static std::vector<decomp_brush_t> SplitDifferentTexturedPartsOfBrush(const mbsp_t *bsp, const decomp_brush_t &brush)
{
    // Quake II maps include brushes, so we shouldn't ever run into
    // a case where a brush has faces split up beyond the brush bounds.
    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
        return {brush};
    }

    std::vector<decomp_brush_t> result;
    SplitDifferentTexturedPartsOfBrush_R(bsp, brush, result);

    //    printf("SplitDifferentTexturedPartsOfBrush: %d sides in. split into %d brushes\n",
    //           (int)brush.sides.size(),
    //           (int)result.size());

    return result;
}

/**
 * Preconditions:
 *  - The existing path of plane side choices have been pushed onto `planestack`
 *  - We've arrived at a leaf
 */
static void DecompileLeaf(const std::vector<decomp_plane_t> &planestack, const mbsp_t *bsp, const mleaf_t *leaf,
    std::vector<leaf_decompile_task> &result)
{
    if (leaf->contents == CONTENTS_EMPTY) {
        return;
    }

    // NOTE: copies the whole plane stack
    result.push_back({planestack, leaf});
}

static std::vector<compiled_brush_t> DecompileLeafTaskGeometryOnly(
    const mbsp_t *bsp, const leaf_decompile_task &task, const std::optional<qvec3d> &brush_offset)
{
    compiled_brush_t brush;
    brush.source = task.brush;
    brush.brush_offset = brush_offset;
    brush.contents = {task.brush ? task.brush->contents : task.leaf ? task.leaf->contents : task.contents.value()};

    brush.sides.reserve(task.allPlanes.size());

    for (const auto &plane : task.allPlanes) {
        compiled_brush_side_t &side = brush.sides.emplace_back();
        side.source = plane.source;
        side.plane = plane;
        DefaultSkipSide(side, bsp);
        side.valve = plane.normal;
    }

    std::vector<compiled_brush_t> result;
    result.push_back(std::move(brush));
    return result;
}

static std::vector<compiled_brush_t> DecompileLeafTask(
    const mbsp_t *bsp, const decomp_options &options, leaf_decompile_task &task, const std::optional<qvec3d> &brush_offset)
{
    std::vector<decomp_brush_t> finalBrushes;
    if (bsp->loadversion->game->id == GAME_QUAKE_II && !options.ignoreBrushes) {
        // Q2 doesn't need this - we assume each brush in the brush lump corresponds to exactly one .map file brush
        // and so each side of the brush can only have 1 texture at this point.
        finalBrushes = {BuildInitialBrush_Q2(bsp, task, task.allPlanes)};
    } else {
        // Q1 (or Q2, with options.ignoreBrushes)
        RemoveRedundantPlanes(task.allPlanes);

        if (task.allPlanes.empty()) {
            printf("warning, skipping empty brush\n");
            return {};
        }

        // fmt::print("before: {} after {}\n", task.allPlanes.size(), reducedPlanes.size());

        // At this point, we should gather all of the faces on `reducedPlanes` and clip away the
        // parts that are outside of our brush. (keeping track of which of the nodes they belonged to)
        // It's possible that the faces are half-overlapping the leaf, so we may have to cut the
        // faces in half.
        auto initialBrush = BuildInitialBrush(bsp, task, task.allPlanes);
        // assert(initialBrush.checkPoints());

        // Next, for each plane in reducedPlanes, if there are 2+ faces on the plane with non-equal
        // texinfo, we need to clip the brush perpendicular to the face until there are no longer
        // 2+ faces on a plane with non-equal texinfo.
        if (!options.ignoreBrushes) {
            finalBrushes = SplitDifferentTexturedPartsOfBrush(bsp, initialBrush);
        } else {
            // we don't really care about accurate textures with options.ignoreBrushes, just
            // want to reconstuct the leafs
            finalBrushes = {initialBrush};
        }
    }

    std::vector<compiled_brush_t> finalCompiledBrushes;
    for (decomp_brush_t &finalBrush : finalBrushes) {
        compiled_brush_t brush;
        brush.source = task.brush;
        brush.brush_offset = brush_offset;
        brush.contents = {task.brush ? task.brush->contents : task.leaf ? task.leaf->contents : task.contents.value()};

        for (auto &finalSide : finalBrush.sides) {
            compiled_brush_side_t &side = brush.sides.emplace_back();
            side.plane = finalSide.plane;
            side.winding = std::move(finalSide.winding);
            side.source = finalSide.plane.source;

            if (brush.contents.native == 0) {
                // hint brush
                side.texture_name = "e1u1/hint";

                if (bsp->loadversion->game->id == GAME_QUAKE_II) {
                    side.flags = {Q2_SURF_HINT};
                }

                side.valve = finalSide.plane.normal;
                continue;
            }

            // see if we have a face
            if (!finalSide.plane.source && finalSide.faces.empty()) {
                // print a default face
                side.valve = finalSide.plane.normal;
                DefaultSkipSide(side, bsp);
            } else {
                const char *name = nullptr;
                const mtexinfo_t *ti = nullptr;

                auto faces = finalSide.faces;

                if (!faces.empty()) {
                    const mface_t *face = faces[0].original_face;
                    name = Face_TextureName(bsp, face);
                    ti = Face_Texinfo(bsp, face);
                } else if (finalSide.plane.source) {
                    ti = BSP_GetTexinfo(bsp, finalSide.plane.source->texinfo);
                    if (ti) {
                        name = ti->texture.data();
                    }
                }

                if (!name || !name[0]) {
                    DefaultSkipSide(side, bsp);
                } else {
                    OverrideTextureForContents(side, bsp, name, brush.contents);
                }

                if (ti) {
                    side.valve = ti->vecs;

                    if (bsp->loadversion->game->id == GAME_QUAKE_II) {
                        side.flags = ti->flags;
                        side.value = ti->value;
                    }
                } else {
                    side.valve = finalSide.plane.normal;
                }
            }
        }

        finalCompiledBrushes.push_back(std::move(brush));
    }

    return finalCompiledBrushes;
}

static std::vector<compiled_brush_t> DecompileLeafTaskLeafVisualization(
    const mbsp_t *bsp, leaf_decompile_task &task, const std::optional<qvec3d> &brush_offset)
{
    std::vector<decomp_brush_t> finalBrushes;

    RemoveRedundantPlanes(task.allPlanes);

    if (task.allPlanes.empty()) {
        printf("warning, skipping empty brush\n");
        return {};
    }

    // fmt::print("before: {} after {}\n", task.allPlanes.size(), reducedPlanes.size());

    auto initialBrush = BuildInitialBrush_Q2(bsp, task, task.allPlanes);
    // assert(initialBrush.checkPoints());

    finalBrushes = {initialBrush};

    std::vector<compiled_brush_t> finalCompiledBrushes;
    for (decomp_brush_t &finalBrush : finalBrushes) {
        compiled_brush_t brush;
        brush.source = task.brush;
        brush.brush_offset = brush_offset;
        brush.contents = task.leaf ? contentflags_t{task.leaf->contents} : contentflags_t{task.contents.value()};

        for (auto &finalSide : finalBrush.sides) {
            compiled_brush_side_t &side = brush.sides.emplace_back();
            side.plane = finalSide.plane;
            side.winding = std::move(finalSide.winding);
            side.source = finalSide.plane.source;
        }

        finalCompiledBrushes.push_back(std::move(brush));
    }

    return finalCompiledBrushes;
}

/**
 * @param front whether we are visiting the front side of the node plane
 */
decomp_plane_t MakeDecompPlane(const mbsp_t *bsp, const bsp2_dnode_t *node, const bool front)
{
    const dplane_t &dplane = *BSP_GetPlane(bsp, node->planenum);

    return {// flip the plane if we went down the front side, since we want the outward-facing plane
        front ? -dplane : dplane, node};
}

decomp_plane_t MakeClipDecompPlane(const mbsp_t *bsp, const bsp2_dclipnode_t *clipnode, const bool front)
{
    const dplane_t &dplane = *BSP_GetPlane(bsp, clipnode->planenum);

    return {// flip the plane if we went down the front side, since we want the outward-facing plane
        front ? -dplane : dplane, nullptr, nullptr, clipnode};
}

/**
 * Preconditions:
 *  - The existing path of plane side choices have been pushed onto `planestack` (but not `node`)
 *  - We're presented with a new plane, `node`
 */
static void DecompileNode(std::vector<decomp_plane_t> &planestack, const mbsp_t *bsp, const bsp2_dnode_t *node,
    std::vector<leaf_decompile_task> &result)
{
    auto handleSide = [&](const bool front) {
        planestack.push_back(MakeDecompPlane(bsp, node, front));

        const int32_t child = node->children[front ? 0 : 1];

        if (child < 0) {
            // it's a leaf on this side
            DecompileLeaf(planestack, bsp, BSP_GetLeafFromNodeNum(bsp, child), result);
        } else {
            // it's another node - process it recursively
            DecompileNode(planestack, bsp, BSP_GetNode(bsp, child), result);
        }

        planestack.pop_back();
    };

    // handle the front and back
    handleSide(true);
    handleSide(false);
}

static void DecompileClipLeaf(const std::vector<decomp_plane_t> &planestack, const mbsp_t *bsp, const int32_t contents,
    std::vector<leaf_decompile_task> &result)
{
    if (contents == CONTENTS_EMPTY) {
        return;
    }

    // NOTE: copies the whole plane stack
    result.push_back({planestack, nullptr, nullptr, nullptr, contents});
}

static void DecompileClipNode(std::vector<decomp_plane_t> &planestack, const mbsp_t *bsp, const bsp2_dclipnode_t *node,
    std::vector<leaf_decompile_task> &result)
{
    auto handleSide = [&](const bool front) {
        planestack.push_back(MakeClipDecompPlane(bsp, node, front));

        const int32_t child = node->children[front ? 0 : 1];

        if (child < 0) {
            // it's a leaf on this side
            DecompileClipLeaf(planestack, bsp, child, result);
        } else {
            // it's another node - process it recursively
            DecompileClipNode(planestack, bsp, &bsp->dclipnodes[child], result);
        }

        planestack.pop_back();
    };

    // handle the front and back
    handleSide(true);
    handleSide(false);
}

static void AddMapBoundsToStack(std::vector<decomp_plane_t> &planestack, const mbsp_t *bsp, const aabb3d &bounds)
{
    for (int i = 0; i < 3; ++i) {
        for (int sign = 0; sign < 2; ++sign) {

            qvec3d normal{};
            normal[i] = (sign == 0) ? 1 : -1;

            double dist;
            if (sign == 0) {
                // positive
                dist = bounds.maxs()[i];
            } else {
                dist = -bounds.mins()[i];
            }

            // we want outward-facing planes
            planestack.push_back(decomp_plane_t{{normal, dist}});
        }
    }
}

static std::vector<compiled_brush_t> DecompileBrushTask(
    const mbsp_t *bsp, const decomp_options &options, leaf_decompile_task &task, const std::optional<qvec3d> &brush_offset)
{
    for (size_t i = 0; i < task.brush->numsides; i++) {
        const q2_dbrushside_qbism_t *side = &bsp->dbrushsides[task.brush->firstside + i];
        decomp_plane_t &plane = task.allPlanes.emplace_back(decomp_plane_t{{bsp->dplanes[side->planenum]}});
        plane.source = side;
    }

    if (options.geometryOnly) {
        return DecompileLeafTaskGeometryOnly(bsp, task, brush_offset);
    } else {
        return DecompileLeafTask(bsp, options, task, brush_offset);
    }
}

#include "common/parser.hh"

static void DecompileEntity(
    const mbsp_t *bsp, const decomp_options &options, std::ofstream &file, const entdict_t &dict, bool isWorld)
{
    // we use -1 to indicate it's not a brush model
    int modelNum = -1;
    if (isWorld) {
        modelNum = 0;
    }

    const dbrush_t *areaportal_brush = nullptr;
    std::optional<qvec3d> brush_offset;

    // Handle func_areaportal; they don't have their own model, the
    // brushes were moved to the world, so we have to "reconstruct"
    // the model. We're also assuming that the areaportal brushes are
    // emitted in the same order as the func_areaportal entities.
    if (dict.find("classname")->second == "func_areaportal") {

        if (dict.has("style")) {
            size_t brush_offset = std::stoull(dict.find("style")->second);

            for (auto &brush : bsp->dbrushes) {
                if (brush.contents & Q2_CONTENTS_AREAPORTAL) {
                    if (brush_offset == 1) {
                        // we'll use this one
                        areaportal_brush = &brush;
                        break;
                    }

                    brush_offset--;
                }
            }
        }
    } else if (dict.find("classname")->second == "func_group") {
        // Some older Q2 maps included func_group in the entity list.
        return;
    }

    // First, print the key/values for this entity
    ewt::print(file, "{{\n");
    for (const auto &keyValue : dict) {
        if (keyValue.first == "model" && !keyValue.second.empty() && keyValue.second[0] == '*') {
            // strip "model" "*NNN" key/values

            std::string modelNumString = keyValue.second;
            modelNumString.erase(0, 1); // erase first character

            modelNum = atoi(modelNumString.c_str());
            continue;
        } else if (areaportal_brush && keyValue.first == "style") {
            continue;
        } else if (modelNum > 0 && keyValue.first == "origin") {
            auto &value = keyValue.second;
            parser_t parser(value, {});
            qvec3d vec;
            parser.parse_token();
            vec[0] = stof(parser.token);
            parser.parse_token();
            vec[1] = stof(parser.token);
            parser.parse_token();
            vec[2] = stof(parser.token);
            if (!qv::emptyExact(vec)) {
                brush_offset = vec;
            }
            continue;
        }

        ewt::print(file, "\"{}\" \"{}\"\n", keyValue.first, keyValue.second);
    }

    std::vector<std::vector<compiled_brush_t>> compiledBrushes;

    // Print brushes if any
    if (modelNum >= 0) {
        const dmodelh2_t *model = &bsp->dmodels[modelNum];

        // If we have brush info, we'll use that directly
        // TODO: support BSPX brushes too
        if (options.hullnum > 0) {
            // recursively visit the clipnodes to gather up a list of clipnode leafs to decompile

            std::vector<decomp_plane_t> stack;
            std::vector<leaf_decompile_task> tasks;
            AddMapBoundsToStack(stack, bsp, aabb3d(qvec3d(model->mins), qvec3d(model->maxs)));

            DecompileClipNode(stack, bsp, &bsp->dclipnodes[model->headnode[options.hullnum]], tasks);

            // decompile the leafs in parallel
            compiledBrushes.resize(tasks.size());
            tbb::parallel_for(static_cast<size_t>(0), tasks.size(), [&](const size_t &i) {
                compiledBrushes[i] = DecompileLeafTaskGeometryOnly(bsp, tasks[i], brush_offset);
            });
        } else if (bsp->loadversion->game->id == GAME_QUAKE_II && !options.ignoreBrushes) {
            std::unordered_map<const dbrush_t *, leaf_decompile_task> brushes;

            auto handle_leaf = [&brushes, bsp, model](const mleaf_t *leaf) {
                for (size_t i = 0; i < leaf->numleafbrushes; i++) {
                    auto brush = &bsp->dbrushes[bsp->dleafbrushes[leaf->firstleafbrush + i]];
                    auto existing = brushes.find(brush);

                    // Don't ever pull out areaportal brushes, since we handle
                    // them in a super special way
                    if (brush->contents & Q2_CONTENTS_AREAPORTAL) {
                        continue;
                    }

                    if (existing == brushes.end()) {
                        auto &task = brushes[brush] = {};
                        task.model = model;
                        task.brush = brush;
                        task.leaf = leaf;
                    }
                }
            };

            std::function<void(const bsp2_dnode_t *)> handle_node = [bsp, &handle_leaf, &handle_node](
                                                                        const bsp2_dnode_t *node) {
                for (auto &c : node->children) {
                    if (c < 0) {
                        handle_leaf(BSP_GetLeafFromNodeNum(bsp, c));
                    } else {
                        handle_node(&bsp->dnodes[c]);
                    }
                }
            };

            if (model->headnode[0] < 0) {
                handle_leaf(BSP_GetLeafFromNodeNum(bsp, model->headnode[0]));
            } else {
                handle_node(BSP_GetNode(bsp, model->headnode[0]));
            }

            std::vector<leaf_decompile_task> brushesVector;
            brushesVector.reserve(brushes.size());
            std::transform(
                brushes.begin(), brushes.end(), std::back_inserter(brushesVector), [](auto &v) { return v.second; });

            compiledBrushes.resize(brushes.size());
            size_t t = brushes.size();

            tbb::parallel_for(static_cast<size_t>(0), brushes.size(), [&](const size_t &i) {
                compiledBrushes[i] = DecompileBrushTask(bsp, options, brushesVector[i], brush_offset);
                t--;
            });
        } else {
            // recursively visit the nodes to gather up a list of leafs to decompile
            auto headnode = BSP_GetNode(bsp, model->headnode[0]);

            std::vector<decomp_plane_t> stack;
            std::vector<leaf_decompile_task> tasks;
            AddMapBoundsToStack(stack, bsp, aabb3d(qvec3d(headnode->mins), qvec3d(headnode->maxs)));

            DecompileNode(stack, bsp, headnode, tasks);

            // decompile the leafs in parallel
            compiledBrushes.resize(tasks.size());
            tbb::parallel_for(static_cast<size_t>(0), tasks.size(), [&](const size_t &i) {
                if (options.geometryOnly) {
                    compiledBrushes[i] = DecompileLeafTaskGeometryOnly(bsp, tasks[i], brush_offset);
                } else {
                    compiledBrushes[i] = DecompileLeafTask(bsp, options, tasks[i], brush_offset);
                }
            });
        }
    } else if (areaportal_brush) {
        leaf_decompile_task task;
        task.brush = areaportal_brush;
        compiledBrushes.push_back(DecompileBrushTask(bsp, options, task, brush_offset));
    }

    // If we run into a trigger brush, replace all of its faces with trigger texture.
    if (modelNum > 0 && dict.find("classname")->second.compare(0, 8, "trigger_") == 0) {
        for (auto &brushlist : compiledBrushes) {
            for (auto &brush : brushlist) {
                for (auto &side : brush.sides) {
                    DefaultTriggerSide(side, bsp);
                }
            }
        }
    }

    // cleanup step: we're left with visible faces having textures, but
    // things that aren't output in BSP faces will use a skip texture.
    // we'll find the best matching texture that we think would work well.
    for (auto &brushlist : compiledBrushes) {
        for (auto &brush : brushlist) {
            for (auto &side : brush.sides) {
                if (side.texture_name != DefaultSkipTexture(bsp)) {
                    continue;
                }

                // check all of the other sides, find the one with the nearest opposite plane
                qvec3d normal_to_check = -side.plane.normal;
                vec_t closest_dot = -DBL_MAX;
                compiled_brush_side_t *closest = nullptr;

                for (auto &side2 : brush.sides) {
                    if (&side2 == &side) {
                        continue;
                    }

                    if (side2.texture_name == DefaultSkipTexture(bsp)) {
                        continue;
                    }

                    vec_t d = qv::dot(normal_to_check, side2.plane.normal);

                    if (!closest || d > closest_dot) {
                        closest_dot = d;
                        closest = &side2;
                    }
                }

                if (closest) {
                    side.texture_name = closest->texture_name;
                } else {
                    side.texture_name = DefaultTextureForContents(bsp, brush.contents);
                }
            }
        }
    }

    // add the origin brush, if we have one
    if (brush_offset.has_value()) {
        std::vector<compiled_brush_t> &brushlist = compiledBrushes.emplace_back();
        compiled_brush_t &brush = brushlist.emplace_back();
        brush.brush_offset = brush_offset;
        brush.contents = {Q2_CONTENTS_ORIGIN};

        constexpr qplane3d planes[] = {
            {{-1, 0, 0}, 8},
            {{0, -1, 0}, 8},
            {{0, 0, -1}, 8},
            {{0, 0, 1}, 8},
            {{0, 1, 0}, 8},
            {{1, 0, 0}, 8},
        };

        for (auto &plane : planes) {
            auto &side = brush.sides.emplace_back();
            side.plane = plane;
            side.texture_name = DefaultOriginTexture(bsp);
            side.valve = plane.normal;
        }
    }

    for (auto &brushlist : compiledBrushes) {
        for (auto &brush : brushlist) {
            brush.write(bsp, file);
        }
    }

    ewt::print(file, "}}\n");
}

void DecompileBSP(const mbsp_t *bsp, const decomp_options &options, std::ofstream &file)
{
    auto entdicts = EntData_Parse(*bsp);

    for (size_t i = 0; i < entdicts.size(); ++i) {
        // entity 0 is implicitly worldspawn (model 0)
        DecompileEntity(bsp, options, file, entdicts[i], i == 0);
    }
}

// MARK: - leaf visualization

static std::vector<leaf_visualization_t> CompiledBrushesToLeafVisualization(std::vector<std::vector<compiled_brush_t>> in)
{
    std::vector<leaf_visualization_t> result;

    for (auto &brush_list : in) {
        for (auto &brush : brush_list) {
            leaf_visualization_t output_leaf;

            // move over windings
            for (auto &in_side : brush.sides) {
                if (in_side.winding) {
                    output_leaf.windings.push_back(std::move(*in_side.winding));
                }
            }
            output_leaf.contents = brush.contents;
            // FIXME: copy over source leafnum

            result.push_back(std::move(output_leaf));
        }
    }

    return result;
}

std::vector<leaf_visualization_t> VisualizeLeafs(const mbsp_t &bsp, int modelnum, int hullnum)
{
    const dmodelh2_t *model = &bsp.dmodels[modelnum];

    std::vector<std::vector<compiled_brush_t>> compiledBrushes;
    std::vector<decomp_plane_t> stack;
    std::vector<leaf_decompile_task> tasks;

    if (hullnum > 0) {
        // recursively visit the clipnodes to gather up a list of clipnode leafs to decompile

        AddMapBoundsToStack(stack, &bsp, aabb3d(qvec3d(model->mins), qvec3d(model->maxs)));

        DecompileClipNode(stack, &bsp, &bsp.dclipnodes[model->headnode[hullnum]], tasks);
    } else {
        // recursively visit the nodes to gather up a list of leafs to decompile
        auto headnode = BSP_GetNode(&bsp, model->headnode[0]);

        AddMapBoundsToStack(stack, &bsp, aabb3d(qvec3d(headnode->mins), qvec3d(headnode->maxs)));

        DecompileNode(stack, &bsp, headnode, tasks);
    }

    // decompile the leafs in parallel
    compiledBrushes.resize(tasks.size());
    tbb::parallel_for(static_cast<size_t>(0), tasks.size(), [&](const size_t &i) {
        compiledBrushes[i] = DecompileLeafTaskLeafVisualization(&bsp,  tasks[i], std::nullopt);
    });

    return CompiledBrushesToLeafVisualization(std::move(compiledBrushes));
}
