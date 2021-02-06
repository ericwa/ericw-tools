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

#include "decompile.h"

#include <common/entdata.h>
#include <common/cmdlib.hh>
#include <common/bspfile.hh>
#include <common/bsputils.hh>
#include <common/mathlib.hh>
#include <common/polylib.hh>

#include <vector>
#include <cstdio>
#include <string>
#include <memory>
#include <utility>
#include <tuple>

// texturing

class texdef_valve_t {
public:
    vec3_t axis[2];
    vec_t scale[2];
    vec_t shift[2];

    texdef_valve_t() {
        for (int i=0;i<2;i++)
            for (int j=0;j<3;j++)
                axis[i][j] = 0;

        for (int i=0;i<2;i++)
            scale[i] = 0;

        for (int i=0;i<2;i++)
            shift[i] = 0;
    }
};

// FIXME: merge with map.cc copy
static texdef_valve_t
TexDef_BSPToValve(const float in_vecs[2][4])
{
    texdef_valve_t res;

// From the valve -> bsp code,
//
//    for (i = 0; i < 3; i++) {
//        out->vecs[0][i] = axis[0][i] / scale[0];
//        out->vecs[1][i] = axis[1][i] / scale[1];
//    }
//
// We'll generate axis vectors of length 1 and pick the necessary scale

    for (int i=0; i<2; i++) {
        vec3_t axis;
        for (int j=0; j<3; j++) {
            axis[j] = in_vecs[i][j];
        }
        const vec_t length = VectorNormalize(axis);
        // avoid division by 0
        if (length != 0.0) {
            res.scale[i] = 1.0f / length;
        } else {
            res.scale[i] = 0.0;
        }
        res.shift[i] = in_vecs[i][3];
        VectorCopy(axis, res.axis[i]);
    }

    return res;
}

static void
WriteFaceTexdef(const mbsp_t *bsp, const bsp2_dface_t *face, FILE* file)
{
    const gtexinfo_t *texinfo = Face_Texinfo(bsp, face);
    const auto valve = TexDef_BSPToValve(texinfo->vecs);

    fprintf(file, "[ %g %g %g %g ] [ %g %g %g %g ] %g %g %g",
            valve.axis[0][0], valve.axis[0][1], valve.axis[0][2], valve.shift[0],
            valve.axis[1][0], valve.axis[1][1], valve.axis[1][2], valve.shift[1],
            0.0, valve.scale[0], valve.scale[1]);
}

static void
WriteNullTexdef(const mbsp_t *bsp, FILE* file)
{
    // FIXME: need to pick based on plane normal
    fprintf(file, "[ %g %g %g %g ] [ %g %g %g %g ] %g %g %g",
            1, 0, 0, 0,
            0, 1, 0, 0,
            0.0, 1, 1);
}


//

struct decomp_plane_t {
    const bsp2_dnode_t* node; // can be nullptr
    bool nodefront; // only set if node is non-null. true = we are visiting the front side of the plane

    // this should be an outward-facing plane
    qvec3d normal;
    double distance;
};

struct planepoints {
    qvec3d point0;
    qvec3d point1;
    qvec3d point2;
};

// brush creation

using namespace polylib;

std::vector<decomp_plane_t>
RemoveRedundantPlanes(const mbsp_t *bsp, std::vector<decomp_plane_t> planes)
{
    std::vector<decomp_plane_t> result;

    for (const decomp_plane_t &plane : planes) {
        // outward-facing plane
        vec3_t normal;
        glm_to_vec3_t(plane.normal, normal);
        winding_t *winding = BaseWindingForPlane(normal, plane.distance);

        // clip `winding` by all of the other planes, flipped
        for (const decomp_plane_t &plane2 : planes) {
            if (&plane2 == &plane)
                continue;

            // get flipped plane
            vec3_t plane2normal;
            glm_to_vec3_t(plane2.normal * -1.0, plane2normal);
            float plane2dist = -plane2.distance;

            // frees winding.
            winding_t *front = nullptr;
            winding_t *back = nullptr;
            ClipWinding(winding, plane2normal, plane2dist, &front, &back);

            // discard the back, continue clipping the front part
            free(back);
            winding = front;

            // check if everything was clipped away
            if (winding == nullptr)
                break;
        }

        if (winding != nullptr) {
            // this plane is not redundant
            result.push_back(plane);
        }

        free(winding);
    }

    return result;
}


std::tuple<qvec3d, qvec3d> MakeTangentAndBitangentUnnormalized(const qvec3d& normal)
{
    // 0, 1, or 2
    const int axis = qv::indexOfLargestMagnitudeComponent(normal);
    const int otherAxisA = (axis + 1) % 3;
    const int otherAxisB = (axis + 2) % 3;

    // setup two other vectors that are perpendicular to each other
    qvec3d otherVecA;
    otherVecA[otherAxisA] = 1.0;

    qvec3d otherVecB;
    otherVecB[otherAxisB] = 1.0;

    qvec3d tangent = qv::cross(normal, otherVecA);
    qvec3d bitangent = qv::cross(normal, otherVecB);

    // We want `test` to point in the same direction as normal.
    // Swap the tangent bitangent if we got the direction wrong.
    qvec3d test = qv::cross(tangent, bitangent);

    if (qv::dot(test, normal) < 0) {
        std::swap(tangent, bitangent);
    }

    // debug test
    if (1) {
        auto n = qv::normalize(qv::cross(tangent, bitangent));
        double d = qv::distance(n, normal);

        assert(d < 0.0001);
    }

    return { tangent, bitangent };
}

static planepoints NormalDistanceToThreePoints(const qvec3d& normal, const double dist) {
    std::tuple<qvec3d, qvec3d> tanBitan = MakeTangentAndBitangentUnnormalized(normal);

    planepoints result;

    result.point0 = normal * dist;
    result.point1 = result.point0 + std::get<1>(tanBitan);
    result.point2 = result.point0 + std::get<0>(tanBitan);

    return result;
}

void PrintPoint(const qvec3d& v, FILE* file) {
    fprintf(file, "( %0.17g %0.17g %0.17g )", v[0], v[1], v[2]);
}

static void
PrintPlanePoints(const mbsp_t *bsp, const decomp_plane_t& decompplane, FILE* file)
{
    // we have a plane in (normal, distance) form;
    const planepoints p = NormalDistanceToThreePoints(decompplane.normal, decompplane.distance);

    PrintPoint(p.point0, file);
    fprintf(file, " ");
    PrintPoint(p.point1, file);
    fprintf(file, " ");
    PrintPoint(p.point2, file);
}

static std::vector<const bsp2_dface_t *>
FindFacesOnNode(const bsp2_dnode_t* node, const mbsp_t *bsp)
{
    std::vector<const bsp2_dface_t *> result;

    if (node) {
        for (int i=0; i<node->numfaces; i++) {
            const bsp2_dface_t *face = BSP_GetFace(bsp, node->firstface + i);

            result.push_back(face);
        }
    }

    return result;
}

static std::string DefaultTextureForContents(int contents)
{
    switch (contents) {
        case CONTENTS_WATER:
            return "*waterskip";
        case CONTENTS_SLIME:
            return "*slimeskip";
        case CONTENTS_LAVA:
            return "*lavaskip";
        case CONTENTS_SKY:
            return "skyskip";
        default:
            return "skip";
    }
}

// structures representing a brush

struct decomp_brush_face_t {
    /**
     * The currently clipped section of the face.
     * May be null to indicate it was clipped away.
     */
    winding_t *winding;
    /**
     * The face we were originally derived from
     */
    const bsp2_dface_t *original_face;

public: // rule of three
    ~decomp_brush_face_t() {
        free(winding);
    }

    decomp_brush_face_t(const decomp_brush_face_t& other) : // copy constructor
    winding(CopyWinding(other.winding)),
    original_face(other.original_face) {}

    decomp_brush_face_t& operator=(const decomp_brush_face_t& other) { // copy assignment
        winding = CopyWinding(other.winding);
        original_face = other.original_face;
        return *this;
    }
public:
    decomp_brush_face_t() :
    winding(nullptr),
    original_face(nullptr) {}

    decomp_brush_face_t(const mbsp_t *bsp, const bsp2_dface_t *face) :
    winding(WindingFromFace(bsp, face)),
    original_face(face) {}

    decomp_brush_face_t(winding_t* windingToTakeOwnership, const bsp2_dface_t *face) :
    winding(windingToTakeOwnership),
    original_face(face) {}

    /**
     * Returns the { front, back } after the clip.
     */
    std::pair<decomp_brush_face_t, decomp_brush_face_t> clipToPlane(const qvec3d& normal, double distance) const {
        vec3_t pnormal;
        glm_to_vec3_t(normal, pnormal);

        winding_t *temp = CopyWinding(winding);
        winding_t *front = nullptr;
        winding_t *back = nullptr;
        ClipWinding(temp, pnormal, distance, &front, &back); // frees temp

        // front or back may be null (if fully clipped).
        // these constructors take ownership of the winding.
        return std::make_pair(decomp_brush_face_t(front, original_face),
                              decomp_brush_face_t(back, original_face));
    }
};

/**
 * Builds the initial list of faces on the node
 */
static std::vector<decomp_brush_face_t>
BuildDecompFacesOnPlane(const mbsp_t *bsp, const decomp_plane_t& plane)
{
    if (plane.node == nullptr) {
        return {};
    }

    const bsp2_dnode_t* node = plane.node;

    std::vector<decomp_brush_face_t> result;
    result.reserve(static_cast<size_t>(node->numfaces));

    for (int i=0; i<node->numfaces; i++) {
        const bsp2_dface_t *face = BSP_GetFace(bsp, node->firstface + i);

        const bool faceOnBack = face->side;
        if (faceOnBack == plane.nodefront) {
            continue; // mismatch
        }

        result.emplace_back(bsp, face);
    }

    return result;
}

struct decomp_brush_side_t {
    /**
     * During decompilation, we can have multiple faces on a single plane of the brush.
     * All vertices of these should lie on the plane.
     */
    std::vector<decomp_brush_face_t> faces;

    decomp_plane_t plane;

    decomp_brush_side_t(const mbsp_t *bsp, const decomp_plane_t& planeIn) :
    faces(BuildDecompFacesOnPlane(bsp, planeIn)),
    plane(planeIn) {}

    decomp_brush_side_t(std::vector<decomp_brush_face_t> facesIn, const decomp_plane_t& planeIn) :
    faces(std::move(facesIn)),
    plane(planeIn) {}

    /**
     * Returns the { front, back } after the clip.
     */
    std::tuple<decomp_brush_side_t, decomp_brush_side_t> clipToPlane(const qvec3d& normal, double distance) const {
        // FIXME: assert normal/distance are not our plane

        std::vector<decomp_brush_face_t> frontfaces, backfaces;

        for (auto& face : faces) {
            auto [faceFront, faceBack] = face.clipToPlane(normal, distance);
            if (faceFront.winding) {
                frontfaces.push_back(std::move(faceFront));
            }
            if (faceBack.winding) {
                backfaces.push_back(std::move(faceBack));
            }
        }

        return {decomp_brush_side_t(std::move(frontfaces), plane),
                decomp_brush_side_t(std::move(backfaces), plane)};
    }
};

struct decomp_brush_t {
    std::vector<decomp_brush_side_t> sides;

    decomp_brush_t(std::vector<decomp_brush_side_t> sidesIn) :
    sides(std::move(sidesIn)) {}

    std::unique_ptr<decomp_brush_t> clone() const {
        return std::unique_ptr<decomp_brush_t>(new decomp_brush_t(*this));
    }

    /**
     * Returns the front and back side after clipping to the given plane.
     */
    std::tuple<std::unique_ptr<decomp_brush_t>,
               std::unique_ptr<decomp_brush_t>> clipToPlane(const qvec3d& normal, double distance) const {
        // handle exact cases
        for (auto& side : sides) {
            if (side.plane.normal == normal && side.plane.distance == distance) {
                // the whole brush is on/behind the plane
                return {nullptr, clone()};
            } else if (side.plane.normal == -normal && side.plane.distance == -distance) {
                // the whole brush is on/in front of the plane
                return {clone(), nullptr};
            }
        }

        // general case.

        return {nullptr, nullptr};
    }
};

/***
 * Preconditions: planes are exactly the planes that define the brush
 *
 * @returns a brush object which has the faces from the .bsp clipped to
 * the parts that lie on the brush.
 */
static decomp_brush_t
BuildInitialBrush(const mbsp_t *bsp, const std::vector<decomp_plane_t>& planes)
{
    std::vector<decomp_brush_side_t> sides;

    for (const decomp_plane_t &plane : planes) {
        auto side = decomp_brush_side_t(bsp, plane);

        // clip `side` by all of the other planes, and keep the back portion
        for (const decomp_plane_t &plane2 : planes) {
            if (&plane2 == &plane)
                continue;

            auto [front, back] = side.clipToPlane(plane2.normal, plane2.distance);

            side = back;
        }

        // NOTE: side may have had all of its faces clipped away, but we still need to keep it
        // as it's one of the final boundaries of the brush

        sides.push_back(std::move(side));
    }

    return decomp_brush_t(sides);
}

std::vector<decomp_brush_t>
SplitDifferentTexturedPartsOfBrush(const mbsp_t *bsp, decomp_brush_t brush)
{
    // FIXME: implement
    return { brush };
}

/**
 * Preconditions:
 *  - The existing path of plane side choices have been pushed onto `planestack`
 *  - We've arrived at a leaf
 */
static void
DecompileLeaf(const std::vector<decomp_plane_t>* planestack, const mbsp_t *bsp, const mleaf_t *leaf, FILE* file)
{
    if (leaf->contents == CONTENTS_EMPTY) {
        return;
    }

    auto reducedPlanes = RemoveRedundantPlanes(bsp, *planestack);
    if (reducedPlanes.empty()) {
        printf("warning, skipping empty brush\n");
        return;
    }

    printf("before: %d after %d\n", (int)planestack->size(), (int)reducedPlanes.size());

    // At this point, we should gather all of the faces on `reducedPlanes` and clip away the
    // parts that are outside of our brush. (keeping track of which of the nodes they belonged to)
    // It's possible that the faces are half-overlapping the leaf, so we may have to cut the
    // faces in half.
    auto initialBrush = BuildInitialBrush(bsp, reducedPlanes);

    // Next, for each plane in reducedPlanes, if there are 2+ faces on the plane with non-equal
    // texinfo, we need to clip the brush perpendicular to the face until there are no longer
    // 2+ faces on a plane with non-equal texinfo.
    auto finalBrushes = SplitDifferentTexturedPartsOfBrush(bsp, initialBrush);

    for (const decomp_brush_t& brush : finalBrushes) {
        fprintf(file, "{\n");
        for (const auto& side : brush.sides) {
            PrintPlanePoints(bsp, side.plane, file);

            // see if we have a face
            auto faces = side.faces;// FindFacesOnNode(side.plane.node, bsp);
            if (!faces.empty()) {
                const bsp2_dface_t *face = faces.at(0).original_face;
                const char *name = Face_TextureName(bsp, face);
                if (0 == strlen(name)) {
                    fprintf(file, " %s ", DefaultTextureForContents(leaf->contents).c_str());
                    WriteNullTexdef(bsp, file);
                } else {
                    fprintf(file, " %s ", name);
                    WriteFaceTexdef(bsp, face, file);
                }
            } else {
                // print a default face
                fprintf(file, " %s ", DefaultTextureForContents(leaf->contents).c_str());
                WriteNullTexdef(bsp, file);
            }
            fprintf(file, "\n");
        }
        fprintf(file, "}\n");
    }
}

/**
 * @param front whether we are visiting the front side of the node plane
 */
decomp_plane_t MakeDecompPlane(const mbsp_t *bsp, const bsp2_dnode_t *node, const bool front) {
    decomp_plane_t result;

    result.node = node;
    result.nodefront = front;

    const dplane_t *dplane = BSP_GetPlane(bsp, node->planenum);

    result.normal = qvec3d(dplane->normal[0],
                         dplane->normal[1],
                         dplane->normal[2]);
    result.distance = static_cast<double>(dplane->dist);

    // flip the plane if we went down the front side, since we want the outward-facing plane
    if (front) {
        result.normal = result.normal * -1.0;
        result.distance = result.distance * -1.0;
    }

    return result;
}

/**
 * Preconditions:
 *  - The existing path of plane side choices have been pushed onto `planestack` (but not `node`)
 *  - We're presented with a new plane, `node`
 */
static void
DecompileNode(std::vector<decomp_plane_t>* planestack, const mbsp_t *bsp, const bsp2_dnode_t *node, FILE* file)
{
    auto handleSide = [&](const bool front) {
        planestack->push_back(MakeDecompPlane(bsp, node, front));

        const int32_t child = node->children[front ? 0 : 1];

        if (child < 0) {
            // it's a leaf on this side
            DecompileLeaf(planestack, bsp, BSP_GetLeafFromNodeNum(bsp, child), file);
        } else {
            // it's another node - process it recursively
            DecompileNode(planestack, bsp, BSP_GetNode(bsp, child), file);
        }

        planestack->pop_back();
    };

    // handle the front and back
    handleSide(true);
    handleSide(false);
}

void
AddMapBoundsToStack(std::vector<decomp_plane_t>* planestack, const mbsp_t *bsp, const bsp2_dnode_t* headnode)
{
    for (int i=0; i<3; ++i) {
        for (int sign=0; sign<2; ++sign) {

            qvec3d normal;
            normal[i] = (sign == 0) ? 1 : -1;

            double dist;
            if (sign == 0) {
                // positive
                dist = headnode->maxs[i];
            } else {
                dist = -headnode->mins[i];
            }

            // we want outward-facing planes
            planestack->push_back({ nullptr, false, normal, dist });
        }
    }
}

static void
DecompileEntity(const mbsp_t *bsp, FILE* file, entdict_t dict, bool isWorld)
{
    // we use -1 to indicate it's not a brush model
    int modelNum = -1;
    if (isWorld) {
        modelNum = 0;
    }

    // First, print the key/values for this entity
    fprintf(file, "{\n");
    for (const auto& keyValue : dict) {
        if (keyValue.first == "model"
            && keyValue.second.size() > 0
            && keyValue.second[0] == '*')
        {
            // strip "model" "*NNN" key/values

            std::string modelNumString = keyValue.second;
            modelNumString.erase(0, 1); // erase first character

            modelNum = atoi(modelNumString.c_str());
            continue;
        }

        fprintf(file, "\"%s\" \"%s\"\n", keyValue.first.c_str(), keyValue.second.c_str());
    }

    // Print brushes if any
    if (modelNum >= 0) {
        const dmodelh2_t* model = &bsp->dmodels[modelNum];

        // start with hull0 of the model
        const bsp2_dnode_t* headnode = BSP_GetNode(bsp, model->headnode[0]);


        std::vector<decomp_plane_t> stack;
        AddMapBoundsToStack(&stack, bsp, headnode);
        DecompileNode(&stack, bsp, headnode, file);
    }

    fprintf(file, "}\n");
}

void
DecompileBSP(const mbsp_t *bsp, FILE* file)
{
    auto entdicts = EntData_Parse(bsp->dentdata);

    for (size_t i = 0; i < entdicts.size(); ++i) {
        // entity 0 is implicitly worldspawn (model 0)
        DecompileEntity(bsp, file, entdicts[i], i == 0);
    }
}
