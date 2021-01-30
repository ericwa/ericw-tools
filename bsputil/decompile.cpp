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

#include <common/cmdlib.hh>
#include <common/bspfile.hh>
#include <common/bsputils.hh>
#include <common/mathlib.hh>

#include <vector>
#include <cstdio>
#include <utility>
#include <tuple>

struct decomp_plane_t {
//    const bsp2_dnode_t* node;
//    bool front;

    qvec3d normal;
    double distance;
};

struct planepoints {
    qvec3d point0;
    qvec3d point1;
    qvec3d point2;
};

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

    // We want test to point in the same direction as normal.
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

/**
 * Preconditions:
 *  - The existing path of plane side choices have been pushed onto `planestack`
 *  - We've arrived at a
 */
static void
DecompileLeaf(const std::vector<decomp_plane_t>* planestack, const mbsp_t *bsp, const mleaf_t *leaf, FILE* file)
{
    //printf("got leaf %d\n", leaf->contents);

    if (leaf->contents == CONTENTS_SOLID) {
        fprintf(file, "{\n");
        for (const auto& decompplane : *planestack) {
            PrintPlanePoints(bsp, decompplane, file);

            fprintf(file, " __TB_empty 0 0 0 1 1\n");
        }
        fprintf(file, "}\n");
    }
}

decomp_plane_t MakeDecompPlane(const mbsp_t *bsp, const bsp2_dnode_t *node, const bool front) {
    decomp_plane_t result;

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
            planestack->push_back({ normal, dist });
        }
    }
}

void
DecompileBSP(const mbsp_t *bsp, FILE* file)
{
    const dmodelh2_t* model = &bsp->dmodels[0];

    // start with hull0 of the world

    const bsp2_dnode_t* headnode = BSP_GetNode(bsp, model->headnode[0]);

    fprintf(file, "{\n");
    fprintf(file, "\"classname\" \"worldspawn\"\n");

    std::vector<decomp_plane_t> stack;
    AddMapBoundsToStack(&stack, bsp, headnode);
    DecompileNode(&stack, bsp, headnode, file);

    fprintf(file, "}\n");
}
