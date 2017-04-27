/*  Copyright (C) 2017 Eric Wasylishen

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

#include <common/bsputils.hh>
#include <common/cmdlib.hh>
#include <common/mesh.hh>
#include <common/octree.hh>

#include <common/mathlib.hh>

#include <iterator>
#include <set>
#include <map>

using namespace std;

// FIXME: Remove
std::vector<qvec3f> qvecsToGlm(std::vector<qvec3f> qvecs) {
    std::vector<qvec3f> res;
    for (const auto &qvec : qvecs) {
        res.push_back(qvec3f(qvec[0], qvec[1], qvec[2]));
    }
    return res;
}

mesh_t buildMesh(const vector<vector<qvec3f>> &faces)
{
    // FIXME: this is ugly
    using pos_t = tuple<float, float, float>;
    
    int nextVert = 0;
    map<pos_t, int> posToVertIndex;
    
    vector<qplane3f> faceplanes;
    vector<vector<int>> facesWithIndices;
    for (const auto &face : faces) {
        vector<int> vertIndices;
        
        // compute face plane
        const auto glmvecs = qvecsToGlm(face);
        qvec4f gp = GLM_PolyPlane(glmvecs);
        qplane3f qp(qvec3f(gp[0], gp[1], gp[2]), gp[3]);
        faceplanes.push_back(qp);
        
        for (const auto &vert : face) {
            float distOff = qp.distAbove(vert);
            Q_assert(fabs(distOff) < 0.001);
            
            const pos_t pos = make_tuple(vert[0], vert[1], vert[2]);
            const auto it = posToVertIndex.find(pos);
            
            if (it == posToVertIndex.end()) {
                posToVertIndex[pos] = nextVert;
                vertIndices.push_back(nextVert);
                nextVert++;
            } else {
                int vertIndex = it->second;
                vertIndices.push_back(vertIndex);
            }
        }
        
        facesWithIndices.push_back(vertIndices);
    }
    
    // convert posToVertIndex to a vector
    vector<qvec3f> vertsVec;
    vertsVec.resize(posToVertIndex.size());
    for (const auto &posIndex : posToVertIndex) {
        const pos_t &pos = posIndex.first;
        vertsVec.at(posIndex.second) = qvec3f(std::get<0>(pos), std::get<1>(pos), std::get<2>(pos));
    }
    
    mesh_t res;
    res.verts = vertsVec;
    res.faces = facesWithIndices;
    res.faceplanes = faceplanes;
    return res;
}

mesh_t buildMeshFromBSP(const bsp2_t *bsp)
{
    mesh_t res;
    for (int i=0; i<bsp->numvertexes; i++) {
        const dvertex_t *vert = &bsp->dvertexes[i];
        res.verts.push_back(qvec3f(vert->point[0],
                                   vert->point[1],
                                   vert->point[2]));
    }
    
    for (int i=0; i<bsp->numfaces; i++) {
        const bsp2_dface_t *f = &bsp->dfaces[i];
        
        // grab face verts
        std::vector<vertnum_t> face;
        for (int j=0; j<f->numedges; j++){
            int vnum = Face_VertexAtIndex(bsp, f, j);
            face.push_back(vnum);
        }
        res.faces.push_back(face);
        
        // grab exact plane
        const qplane3f plane = Face_Plane_E(bsp, f);
        res.faceplanes.push_back(plane);
    }
    
    return res;
}

std::vector<std::vector<qvec3f>> meshToFaces(const mesh_t &mesh)
{
    std::vector<std::vector<qvec3f>> res;
    for (const auto &meshFace : mesh.faces) {
        std::vector<qvec3f> points;
        for (int vertIndex : meshFace) {
            const qvec3f point = mesh.verts.at(vertIndex);
            points.push_back(point);
        }
        res.push_back(points);
    }
    Q_assert(res.size() == mesh.faces.size());
    return res;
}

aabb3f mesh_face_bbox(const mesh_t &mesh, facenum_t facenum)
{
    const std::vector<int> &face = mesh.faces.at(facenum);
    
    const qvec3f vert0 = mesh.verts.at(face.at(0));
    aabb3f bbox(vert0, vert0);
    
    for (int vert_i : face) {
        const qvec3f vert = mesh.verts.at(vert_i);
        bbox = bbox.expand(vert);
    }
    return bbox;
}

static octree_t<vertnum_t> build_vert_octree(const mesh_t &mesh)
{
    std::vector<std::pair<aabb3f, vertnum_t>> vertBboxNumPairs;

    for (int i=0; i<mesh.verts.size(); i++) {
        const qvec3f vert = mesh.verts[i];
        const aabb3f bbox(vert, vert);
        
        vertBboxNumPairs.push_back(make_pair(bbox, i));
    }

    return makeOctree(vertBboxNumPairs);
}

qvec3f qToG(qvec3f in) {
    return qvec3f(in[0], in[1], in[2]);
}

qvec3f gToQ(qvec3f in) {
    return qvec3f(in[0], in[1], in[2]);
}

/**
 * Possibly insert vert `vnum` on one of the edges of face `fnum`, if it happens
 * to lie on one of the edges.
 */
void face_InsertVertIfNeeded(mesh_t &mesh, facenum_t fnum, vertnum_t vnum)
{
    meshface_t &face = mesh.faces.at(fnum);
    const qplane3f &faceplane = mesh.faceplanes.at(fnum);
    const qvec3f potentialVertPos = mesh.verts.at(vnum);
    
    const float distOff = faceplane.distAbove(potentialVertPos);
    if (fabs(distOff) > TJUNC_DIST_EPSILON)
        return; // not on the face plane
    
    // N.B. we will modify the `face` std::vector within this loop
    for (int i=0; i<face.size(); i++) {
        const qvec3f v0 = mesh.verts.at(i);
        const qvec3f v1 = mesh.verts.at((i+1)%face.size());
        
        // does `potentialVertPos` lie on the line between `v0` and `v1`?
        float distToLine = DistToLine(qToG(v0), qToG(v1), qToG(potentialVertPos));
        if (distToLine > TJUNC_DIST_EPSILON)
            continue;
        
        // N.B.: not a distance
        float fracOfLine = FractionOfLine(qToG(v0), qToG(v1), qToG(potentialVertPos));
        if (fracOfLine < 0 || fracOfLine > 1)
            continue;
        
        // do it
        auto it = face.begin();
        std::advance(it, i + 1);
        face.insert(it, vnum);
        Q_assert(face.at(i + 1) == vnum);
        return;
    }
    
    // didn't do it
    return;
}

template<class T>
static set<T> vecToSet(const vector<T> &vec) {
    set<T> res;
    for (const auto &item : vec) {
        res.insert(item);
    }
    return res;
}

void cleanupFace(mesh_t &mesh,
                 facenum_t i,
                 const octree_t<vertnum_t> &vertoctree) {

    aabb3f facebbox = mesh_face_bbox(mesh, i);
    facebbox = facebbox.grow(qvec3f(1,1,1));
    
    const set<vertnum_t> face_vert_set = vecToSet(mesh.faces.at(i));
    const vector<vertnum_t> nearbyverts = vertoctree.queryTouchingBBox(facebbox);
    
    for (vertnum_t vnum : nearbyverts) {
        // skip verts that are already on the face
        if (face_vert_set.find(vnum) != face_vert_set.end()) {
            continue;
        }
        
        // possibly add this vert
        face_InsertVertIfNeeded(mesh, i, vnum);
    }
}

void cleanupMesh(mesh_t &mesh)
{
    const octree_t<vertnum_t> vertoctree = build_vert_octree(mesh);
    
    for (int i=0; i<mesh.faces.size(); i++) {
        cleanupFace(mesh, i, vertoctree);
    }
}

// sample point positioning

class position_t {
public:
    bool m_unoccluded;
    const bsp2_dface_t *m_actualFace;
    qvec3f m_position;
    qvec3f m_interpolatedNormal;
    
    position_t(qvec3f position)
    : m_unoccluded(false),
    m_actualFace(nullptr),
    m_position(position),
    m_interpolatedNormal(qvec3f(0,0,0)) {}
    
    position_t(const bsp2_dface_t *actualFace,
               qvec3f position,
               qvec3f interpolatedNormal)
    : m_unoccluded(true),
    m_actualFace(actualFace),
    m_position(position),
    m_interpolatedNormal(interpolatedNormal) {};
};

static const float sampleOffPlaneDist = 1.0f;

// precondition: `point` is on the same plane as `face` and within the bounds.
static position_t
PositionSamplePointOnFace(const bsp2_t *bsp,
                          const bsp2_dface_t *face,
                          const bool phongShaded,
                          const qvec3f &point)
{
    const auto &facecache = FaceCacheForFNum(Face_GetNum(bsp, face));
    const auto &points = facecache.points();
    const auto &normals = facecache.normals();
    const auto &edgeplanes = facecache.edgePlanes();
    const auto &plane = facecache.plane();
    
    if (edgeplanes.empty()) {
        // degenerate polygon
        return position_t(point);
    }
    
    const float planedist = GLM_DistAbovePlane(plane, point);
    Q_assert(fabs(planedist - sampleOffPlaneDist) <= POINT_EQUAL_EPSILON);
    
    const float insideDist = GLM_EdgePlanes_PointInsideDist(edgeplanes, point);
    if (insideDist < -POINT_EQUAL_EPSILON) {
        // Non-convex polygon
        return position_t(point);
    }
    
    const modelinfo_t *mi = ModelInfoForFace(bsp, Face_GetNum(bsp, face));
    
    // Get the point normal
    qvec3f pointNormal;
    if (phongShaded) {
        const auto interpNormal = GLM_InterpolateNormal(points, normals, point);
        // We already know the point is in the face, so this should always succeed
        if(!interpNormal.first)
            return position_t(point);
        pointNormal = interpNormal.second;
    } else {
        pointNormal = qvec3f(plane);
    }
    
    const bool inSolid = Light_PointInAnySolid(bsp, mi->model, point);
    if (inSolid) {
        // Check distance to border
        const float distanceInside = GLM_EdgePlanes_PointInsideDist(edgeplanes, point);
        if (distanceInside < 1.0f) {
            // Point is too close to the border. Try nudging it inside.
            const auto &shrunk = facecache.pointsShrunkBy1Unit();
            if (!shrunk.empty()) {
                const pair<int, qvec3f> closest = GLM_ClosestPointOnPolyBoundary(shrunk, point);
                const qvec3f newPoint = closest.second + (qvec3f(plane) * sampleOffPlaneDist);
                if (!Light_PointInAnySolid(bsp, mi->model, newPoint))
                    return position_t(face, newPoint, pointNormal);
            }
        }
        
        return position_t(point);
    }
    
    return position_t(face, point, pointNormal);
}

static position_t positionSample_r(const mesh_t &mesh, facenum_t startingFace, const qvec3f &startingPos, int recursionDepth)
{
    const auto &facecache = FaceCacheForFNum(Face_GetNum(bsp, face));
    const qvec4f &surfplane = facecache.plane();
    const auto &points = facecache.points();
    const auto &edgeplanes = facecache.edgePlanes();
    
    // project `point` onto the surface plane, then lift it off again
    const qvec3f point = GLM_ProjectPointOntoPlane(surfplane, origPoint) + (qvec3f(surfplane) * sampleOffPlaneDist);
    
    // check if in face..
    if (GLM_EdgePlanes_PointInside(edgeplanes, point)) {
        return PositionSamplePointOnFace(bsp, face, phongShaded, point);
    }
    
    // not in any triangle. among the edges this point is _behind_,
    // search for the one that the point is least past the endpoints of the edge
    {
        int bestplane = -1;
        float bestdist = FLT_MAX;
        
        for (int i=0; i<face->numedges; i++) {
            const qvec3f v0 = points.at(i);
            const qvec3f v1 = points.at((i+1) % points.size());
            
            const auto edgeplane = GLM_MakeInwardFacingEdgePlane(v0, v1, qvec3f(surfplane));
            if (!edgeplane.first)
                continue; // degenerate edge
            
            float planedist = GLM_DistAbovePlane(edgeplane.second, point);
            if (planedist < POINT_EQUAL_EPSILON) {
                // behind this plane. check whether we're between the endpoints.
                
                const qvec3f v0v1 = v1 - v0;
                const float v0v1dist = qv::length(v0v1);
                
                const float t = FractionOfLine(v0, v1, point); // t=0 for point=v0, t=1 for point=v1
                
                float edgedist;
                if (t < 0) edgedist = fabs(t) * v0v1dist;
                else if (t > 1) edgedist = t * v0v1dist;
                else edgedist = 0;
                
                if (edgedist < bestdist) {
                    bestplane = i;
                    bestdist = edgedist;
                }
            }
        }
        
        if (bestplane != -1) {
            // FIXME: Also need to handle non-smoothed but same plane
            const bsp2_dface_t *smoothed = Face_EdgeIndexSmoothed(bsp, face, bestplane);
            if (smoothed) {
                // try recursive search
                if (recursiondepth < 3) {
                    // call recursively to look up normal in the adjacent face
                    return CalcPointNormal(bsp, smoothed, point, phongShaded, face_lmscale, recursiondepth + 1);
                }
            }
        }
    }
    
    // 2. Try snapping to poly
    
    const pair<int, qvec3f> closest = GLM_ClosestPointOnPolyBoundary(points, point);
    const float texSpaceDist = TexSpaceDist(bsp, face, closest.second, point);
    
    if (texSpaceDist <= face_lmscale) {
        // Snap it to the face edge. Add the 1 unit off plane.
        const qvec3f snapped = closest.second + (qvec3f(surfplane) * sampleOffPlaneDist);
        return PositionSamplePointOnFace(bsp, face, phongShaded, snapped);
    }
    
    // This point is too far from the polygon to be visible in game, so don't bother calculating lighting for it.
    // Dont contribute to interpolating.
    // We could safely colour it in pink for debugging.
    return position_t(point);
}

sample_position_t positionSample(const mesh_t &mesh, facenum_t startingFace, const qvec3f &startingPos)
{
    // call the recursive version
    return positionSample_r(mesh, startingFace, startingPos, 0);
}
