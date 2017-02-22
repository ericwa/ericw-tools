/*  Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 2017 Eric Wasylishen
 
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
#include <light/light2.hh>
#include <light/entities.hh>
#include <light/trace.hh>
#include <light/ltface2.hh>

#include <common/bsputils.hh>

#include <iostream>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <array>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtx/string_cast.hpp>

using namespace std;
using namespace glm;
using namespace polylib;

glm::vec2 WorldToTexCoord_HighPrecision(const bsp2_t *bsp, const bsp2_dface_t *face, const glm::vec3 &world)
{
    const texinfo_t *tex = Face_Texinfo(bsp, face);
    if (tex == nullptr)
        return glm::vec2(0);
    
    glm::vec2 coord;
    
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
    for (int i = 0; i < 2; i++) {
        coord[i] = (long double)world[0] * tex->vecs[i][0] +
        (long double)world[1] * tex->vecs[i][1] +
        (long double)world[2] * tex->vecs[i][2] +
        tex->vecs[i][3];
    }
    return coord;
}

class faceextents_t {
private:
    glm::ivec2 m_texmins;
    glm::ivec2 m_texsize;
    float m_lightmapscale;
    glm::mat4x4 m_worldToTexCoord;
    glm::mat4x4 m_texCoordToWorld;
    
public:
    faceextents_t() = default;
    
    faceextents_t(const bsp2_dface_t *face, const bsp2_t *bsp, float lmscale)
    : m_lightmapscale(lmscale)
    {
        m_worldToTexCoord = WorldToTexSpace(bsp, face);
        m_texCoordToWorld = TexSpaceToWorld(bsp, face);
        
        glm::vec2 mins(VECT_MAX, VECT_MAX);
        glm::vec2 maxs(-VECT_MAX, -VECT_MAX);
        
        for (int i = 0; i < face->numedges; i++) {
            const glm::vec3 worldpoint = Face_PointAtIndex_E(bsp, face, i);
            const glm::vec2 texcoord = WorldToTexCoord_HighPrecision(bsp, face, worldpoint);
            
            // self test
            auto texcoordRT = this->worldToTexCoord(worldpoint);
            auto worldpointRT = this->texCoordToWorld(texcoord);
            Q_assert(glm::bvec2(true, true) == glm::epsilonEqual(texcoordRT, texcoord, 0.1f));
            Q_assert(glm::bvec3(true, true, true) == glm::epsilonEqual(worldpointRT, worldpoint, 0.1f));
            // end self test
            
            for (int j = 0; j < 2; j++) {
                if (texcoord[j] < mins[j])
                    mins[j] = texcoord[j];
                if (texcoord[j] > maxs[j])
                    maxs[j] = texcoord[j];
            }
        }
        
        for (int i = 0; i < 2; i++) {
            mins[i] = floor(mins[i] / m_lightmapscale);
            maxs[i] = ceil(maxs[i] / m_lightmapscale);
            m_texmins[i] = static_cast<int>(mins[i]);
            m_texsize[i] = static_cast<int>(maxs[i] - mins[i]);
            
            if (m_texsize[i] >= MAXDIMENSION) {
                const plane_t plane = Face_Plane(bsp, face);
                const glm::vec3 point = Face_PointAtIndex_E(bsp, face, 0); // grab first vert
                const char *texname = Face_TextureName(bsp, face);
                
                Error("Bad surface extents:\n"
                      "   surface %d, %s extents = %d, scale = %g\n"
                      "   texture %s at (%s)\n"
                      "   surface normal (%s)\n",
                      Face_GetNum(bsp, face), i ? "t" : "s", m_texsize[i], m_lightmapscale,
                      texname, glm::to_string(point).c_str(),
                      VecStrf(plane.normal));
            }
        }
    }
    
    int width() const { return m_texsize[0] + 1; }
    int height() const { return m_texsize[1] + 1; }
    int numsamples() const { return width() * height(); }
    glm::ivec2 texsize() const { return glm::ivec2(width(), height()); }
    
    int indexOf(const glm::ivec2 &lm) const {
        Q_assert(lm.x >= 0 && lm.x < width());
        Q_assert(lm.y >= 0 && lm.y < height());
        return lm.x + (width() * lm.y);
    }
    
    glm::ivec2 intCoordsFromIndex(int index) const {
        Q_assert(index >= 0);
        Q_assert(index < numsamples());
        
        glm::ivec2 res(index % width(), index / width());
        Q_assert(indexOf(res) == index);
        return res;
    }
    
    glm::vec2 LMCoordToTexCoord(const glm::vec2 &LMCoord) const {
        const glm::vec2 res(m_lightmapscale * (m_texmins[0] + LMCoord.x),
                            m_lightmapscale * (m_texmins[1] + LMCoord.y));
        return res;
    }
    
    glm::vec2 TexCoordToLMCoord(const glm::vec2 &tc) const {
        const glm::vec2 res((tc.x / m_lightmapscale) - m_texmins[0],
                            (tc.y / m_lightmapscale) - m_texmins[1]);
        return res;
    }
    
    glm::vec2 worldToTexCoord(glm::vec3 world) const {
        const glm::vec4 worldPadded(world[0], world[1], world[2], 1.0f);
        const glm::vec4 res = m_worldToTexCoord * worldPadded;
        
        Q_assert(res[3] == 1.0f);
        
        return glm::vec2( res[0], res[1] );
    }
    
    glm::vec3 texCoordToWorld(glm::vec2 tc) const {
        const glm::vec4 tcPadded(tc[0], tc[1], 0.0f, 1.0f);
        const glm::vec4 res = m_texCoordToWorld * tcPadded;
        
        Q_assert(fabs(res[3] - 1.0f) < 0.01f);
        
        return glm::vec3( res[0], res[1], res[2] );
    }
    
    glm::vec2 worldToLMCoord(glm::vec3 world) const {
        return TexCoordToLMCoord(worldToTexCoord(world));
    }
    
    glm::vec3 LMCoordToWorld(glm::vec2 lm) const {
        return texCoordToWorld(LMCoordToTexCoord(lm));
    }
};

class sample_t {
private:
    glm::vec3 numerator;
    float denominator;
    
public:
    sample_t() : numerator(0.0f), denominator(0.0f) {}
    
    void addWeightedValue(float weight, glm::vec3 value) {
        Q_assert(weight >= 0.0f);
        Q_assert(weight <= 1.0f);
        
        numerator += weight * value;
        denominator += weight;
    }
    glm::vec3 value() const {
        return numerator / denominator;
    }
    bool hasValue() const {
        return denominator != 0.0f;
    }
};

static std::vector<glm::vec3> ConstantColor(const faceextents_t &ext, const glm::vec3 &color) {
    std::vector<glm::vec3> res;
    for (int i=0; i<ext.numsamples(); i++) {
        res.push_back(color);
    }
    return res;
}

// holder for a face's lightmap texture
class face_samples_t {
public:
    faceextents_t faceextents;
    vector<sample_t> samples;
    vector<glm::vec3> sampleWorldPos;
    vector<glm::vec2> sampleTexPos;
    
    face_samples_t() {
    }
    
    face_samples_t(const bsp2_t *bsp, const bsp2_dface_t *face) {
        faceextents = {face, bsp, 16};
        
        samples.resize(faceextents.numsamples());
        sampleWorldPos.resize(faceextents.numsamples());
        sampleTexPos.resize(faceextents.numsamples());
    }
    
    vector<glm::vec3> getColors() const {
        vector<glm::vec3> result;
        for (const auto &s : samples) {
            result.push_back(s.value());
        }
        return result;
    }
    
    sample_t &mutableSampleAt(int x, int y) {
        const int i = faceextents.indexOf(glm::ivec2(x, y));
        return samples.at(i);
    }
};

struct face_tris_t {
    vector<float> areas;
    vector<tuple<int,int,int>> tris;
    
    vector<float> normalizedCDF;
    
    glm::vec3 randomPoint(const bsp2_t *bsp) const {
        const float triFloat = Random();
        const int whichTri = SampleCDF(normalizedCDF, triFloat);
        
        const auto tri = tris.at(whichTri);
        tuple<glm::vec3, glm::vec3, glm::vec3> triPts = make_tuple(Vertex_GetPos_E(bsp, get<0>(tri)),
                                                                   Vertex_GetPos_E(bsp, get<1>(tri)),
                                                                   Vertex_GetPos_E(bsp, get<2>(tri)));
        
        const glm::vec3 randomBary = Barycentric_Random(Random(), Random());
        
        const glm::vec3 pt = Barycentric_ToPoint(randomBary, triPts);
        return pt;
    }
};

face_tris_t Face_MakeTris(const bsp2_t *bsp, const bsp2_dface_t *f)
{
    face_tris_t res;
    
    /* now just walk around the surface as a triangle fan */
    int v1, v2, v3;
    v1 = Face_VertexAtIndex(bsp, f, 0);
    v2 = Face_VertexAtIndex(bsp, f, 1);
    for (int j = 2; j < f->numedges; j++)
    {
        v3 = Face_VertexAtIndex(bsp, f, j);
        
        const glm::vec3 p1 = Vertex_GetPos_E(bsp, v1);
        const glm::vec3 p2 = Vertex_GetPos_E(bsp, v2);
        const glm::vec3 p3 = Vertex_GetPos_E(bsp, v3);
        
        const float area = GLM_TriangleArea(p1, p2, p3);
        Q_assert(!isnan(area));
        
        res.areas.push_back(area);
        res.tris.push_back(make_tuple(v1, v2, v3));
        
        v2 = v3;
    }
    
    res.normalizedCDF = MakeCDF(res.areas);
    
    // testing
#if 0
    const auto points = GLM_FacePoints(bsp, f);
    const auto edgeplanes = GLM_MakeInwardFacingEdgePlanes(points);
    
    if (!edgeplanes.empty()) {
        plane_t pl = Face_Plane(bsp, f);
        for (int i=0; i<1024; i++) {
            glm::vec3 pt = res.randomPoint(bsp);
            
            const float planeDist = DotProduct(&pt[0], pl.normal) - pl.dist;
            Q_assert(planeDist < 0.1);
            
            Q_assert(GLM_EdgePlanes_PointInside(edgeplanes, pt));
        }
    }
#endif
    return res;
}

static void
WriteLightmap_Minimal(const bsp2_t *bsp, bsp2_dface_t *face, const faceextents_t &extents,
                      const std::vector<glm::vec3> &samples)
{
    face->styles[0] = 0;
    face->styles[1] = 255;
    face->styles[2] = 255;
    face->styles[3] = 255;
    
    byte *out, *lit, *lux;
    GetFileSpace(&out, &lit, &lux, extents.numsamples());
    face->lightofs = out - filebase;
    
    for (int t = 0; t < extents.height(); t++) {
        for (int s = 0; s < extents.width(); s++) {
            const int sampleindex = extents.indexOf(glm::ivec2(s, t));
            const glm::vec3 &color = samples.at(sampleindex);
            
            *lit++ = color[0];
            *lit++ = color[1];
            *lit++ = color[2];
            
            /* Average the color to get the value to write to the
             .bsp lightmap. this avoids issues with some engines
             that require the lit and internal lightmap to have the same
             intensity. (MarkV, some QW engines)
             */
            vec_t light = LightSample_Brightness(color);
            if (light < 0) light = 0;
            if (light > 255) light = 255;
            *out++ = light;
        }
    }
}

glm::vec4 extendTo4(const glm::vec3 &v) {
    return glm::vec4(v[0], v[1], v[2], 1.0);
}

static glm::vec3 randomColor() {
    return glm::vec3(Random(), Random(), Random()) * 255.0f;
}

struct sample_pos_t {
    const bsp2_dface_t *face;
    glm::vec3 worldpos;
    glm::vec3 color;
};

const bsp2_dface_t *FaceAtPos(const bsp2_t *bsp, const glm::vec3 &pos) {
    float minDist = FLT_MAX;
    const bsp2_dface_t *bestFace = nullptr;
    
    for (int i=0; i<bsp->numfaces; i++) {
        const bsp2_dface_t *f = &bsp->dfaces[i];
        
        glm::vec3 cen = Face_Centroid(bsp, f);
        float len = glm::length(cen - pos);
        if (len < minDist) {
            minDist = len;
            bestFace = f;
        }
    }
    
    const glm::vec3 bcen = Face_Centroid(bsp, bestFace);
    
    std::cout << "FaceAtPos: found face "
    << Face_GetNum(bsp, bestFace)
    << " with centroid "
    << glm::to_string(bcen)
    << " dist "
    << glm::length(bcen - pos)
    << std::endl;
    
    return bestFace;
}

void
LightBatch(bsp2_t *bsp, const batch_t &batch, const all_contrib_faces_t &all_contrib_faces)
{
    // self test for contributing faces
    for (int fnum : batch) {
        const bsp2_dface_t *face = &bsp->dfaces[fnum];
        const plane_t facePlane = Face_Plane(bsp, face);
        const std::vector<contributing_face_t> &contribfaces = all_contrib_faces.at(face);
        
        for (const auto &contribfaceinfo : contribfaces) {
            Q_assert(contribfaceinfo.refFace == face);
            const bsp2_dface_t *contribface = contribfaceinfo.contribFace;
            
            // This transform should represent "unwrapping" the mesh between `face` and `contribface` so that `contribface` lies on the same plane as `face`.
            for (int i=0; i<contribface->numedges; i++) {
                const int v = Face_VertexAtIndex(bsp, contribface, i);
                const glm::vec4 originalPos = extendTo4(Vertex_GetPos_E(bsp, v));
                const glm::vec4 unwrappedPos = contribfaceinfo.contribWorldToRefWorld * glm::vec4(originalPos[0], originalPos[1], originalPos[2], 1.0);
                
                float originalPlaneDist = glm::dot(glm::vec3(facePlane.normal[0],facePlane.normal[1],facePlane.normal[2]), glm::vec3(originalPos)) - facePlane.dist;
                float unwrappedPlaneDist = glm::dot(glm::vec3(facePlane.normal[0],facePlane.normal[1],facePlane.normal[2]), glm::vec3(unwrappedPos)) - facePlane.dist;
                
                if (fabs(originalPlaneDist) > 0.1) {
                    //printf("orig %f %f\n", originalPlaneDist, unwrappedPlaneDist);
                    Q_assert(fabs(unwrappedPlaneDist) < 0.1);
                }
            }
        }
    }
    
    // good test case: give every face a random color
    // => there should be smooth seams (without aliasing) between faces on the
    // same plane
    
    map<const bsp2_dface_t *, faceextents_t> extentsForFace;
    for (int fnum : batch) {
        const bsp2_dface_t *face = &bsp->dfaces[fnum];
        extentsForFace[face] = faceextents_t {face, bsp, 16};
    }
    
    map<const bsp2_dface_t *, glm::vec3> colorForFace;
    for (int fnum : batch) {
        const bsp2_dface_t *face = &bsp->dfaces[fnum];
        const glm::vec3 color = randomColor();
        
        colorForFace[face] = color;
    }
    
    // build trisForFace
    map<const bsp2_dface_t *, face_tris_t> trisForFace;
    for (int fnum : batch) {
        const bsp2_dface_t *face = &bsp->dfaces[fnum];
        const face_tris_t tris = Face_MakeTris(bsp, face);
        
        // check area
        winding_t *w = WindingFromFace(bsp, face);
        float wa = WindingArea(w);
        
        float sum = 0;
        for (auto ta : tris.areas) {
            sum += ta;
        }
        if(fabs(wa - sum) > 0.01) {
            printf("areas mismatch %g %g\n", wa, sum);
        }
        free(w);
        
        trisForFace[face] = tris;
    }
    
    // generate 64 samples per face
    map<const bsp2_dface_t *, vector<sample_pos_t>> samplePossForFace;
    for (int fnum : batch) {
        const bsp2_dface_t *face = &bsp->dfaces[fnum];
        const face_tris_t &tris = trisForFace.at(face);
        const glm::vec3 color = colorForFace.at(face);
        
        const auto points = GLM_FacePoints(bsp, face);
        const auto edgeplanes = GLM_MakeInwardFacingEdgePlanes(points);
        
        if (edgeplanes.empty())
            continue;
        
        auto &vec = samplePossForFace[face];
        for (int i=0; i<32; i++) {
            sample_pos_t s;
            s.face = face;
            s.worldpos = tris.randomPoint(bsp);
            s.color = color;
            
            Q_assert(GLM_EdgePlanes_PointInside(edgeplanes, s.worldpos));
            vec.push_back(s);
        }
    }
    
    // build storage for final lightmaps
    map<const bsp2_dface_t *, face_samples_t> faceSamples;
    for (int fnum : batch) {
        const bsp2_dface_t *face = &bsp->dfaces[fnum];
        
        face_samples_t fs(bsp, face);
        faceSamples[face] = fs;
    }
    
    // Now fill in the lightmaps...
    for (int fnum : batch) {
        const bsp2_dface_t *face = &bsp->dfaces[fnum];
        face_samples_t &fs = faceSamples.at(face);
        const face_tris_t &tris = trisForFace.at(face);
        const faceextents_t &extents = extentsForFace.at(face);
        const vector<sample_pos_t> &samples = samplePossForFace.at(face);
        
        // affecing faces
        const vector<contributing_face_t> &affectingFaces = all_contrib_faces.at(face);
        
        // Find contributing samples, map them into our own texture space
        vector<pair<glm::vec2, sample_pos_t>> contributingSamplesInTexSpace;
        for (const contributing_face_t &contributingFace : affectingFaces) {
            if (contributingFace.contribFace == face)
                continue; // FIXME: make hard error
            
            
            const auto face_points = GLM_FacePoints(bsp, face);
            const auto face_edgeplanes = GLM_MakeInwardFacingEdgePlanes(face_points);
            
            const auto contribface_points = GLM_FacePoints(bsp, contributingFace.contribFace);
            const auto contribface_edgeplanes = GLM_MakeInwardFacingEdgePlanes(contribface_points);
            
            const auto &samplePoss = samplePossForFace.at(contributingFace.contribFace);
            
            for (const sample_pos_t &samplePos : samplePoss) {
                Q_assert(face == contributingFace.refFace);
                Q_assert(samplePos.face == contributingFace.contribFace);
                
                const glm::vec4 faceTC = contributingFace.contribWorldToRefTex * extendTo4(samplePos.worldpos);
                
                Q_assert(fabs(faceTC.z - 0.0) < 0.01);
                Q_assert(fabs(faceTC.w - 1.0) < 0.01);
                
                contributingSamplesInTexSpace.push_back(make_pair(glm::vec2(faceTC.x, faceTC.y), samplePos));
            }
        }
        
        // Add our own samples
        const auto &samplePoss = samplePossForFace.at(face);
        for (const sample_pos_t &samplePos : samplePoss) {
            const auto tc = extents.worldToTexCoord(samplePos.worldpos);
            contributingSamplesInTexSpace.push_back(make_pair(tc, samplePos));
        }
        
        // color in all luxels, by applying a gaussian kernel to a single light sample in the centroid of the face
        
        for (int y=0; y<extents.height(); y++) {
            for (int x=0; x<extents.width(); x++) {
                sample_t &mutableSample = fs.mutableSampleAt(x, y);
                const glm::vec2 texCoord = extents.LMCoordToTexCoord(glm::vec2(x, y));
                
                // loop thru samples
                for (const auto &pr : contributingSamplesInTexSpace) {
                    const glm::vec2 dist = pr.first - texCoord;
                    
                    float weight = glm::max(0.0f, 16.0f - glm::length(dist)) / 16.0f;
                    
                    //if (glm::length(dist) < 16) {
                    mutableSample.addWeightedValue(weight, pr.second.color);
                    //}
                }
            }
        }
    }
    
    // save out..
    for (const auto &pr : faceSamples) {
        bsp2_dface_t *face = const_cast<bsp2_dface_t *>(pr.first);
        const auto &extents = extentsForFace.at(face);
        
        WriteLightmap_Minimal(bsp, face, extents, pr.second.getColors());
    }
}



#if 0
// begin test
const texinfo_t *tex = &bsp->texinfo[face->texinfo];
float uv[2];
vec3_t wp;
Face_PointAtIndex(bsp, face, 0, wp);
WorldToTexCoord(wp, tex, uv);

vec3_t wp_roundtrip;
TexCoordToWorld(uv[0], uv[1], &texorg, wp_roundtrip);

//                printf("wp: %g %g %g\n tc: %g %g,\nrt: %g %g %g\n",
//                       wp[0], wp[1], wp[2],
//                       uv[0], uv[1],
//                       wp_roundtrip[0],
//                       wp_roundtrip[1],
//                       wp_roundtrip[2]
//                       );
//
// new method:

glm::mat4x4 WT = WorldToTexSpace(bsp, face);
glm::mat4x4 TW = TexSpaceToWorld(bsp, face);

glm::vec4 uv2 = WT * glm::vec4(wp[0], wp[1], wp[2], 1.0);
glm::vec4 wp2 = TW * glm::vec4(uv2[0], uv2[1], uv2[2], uv2[3]);

//                printf("uv2: %g %g %g %g\n wp2: %g %g %g %g\n",
//                       uv2[0], uv2[1], uv2[2], uv2[3],
//                       wp2[0], wp2[1], wp2[2], wp2[3]
//                       );

Q_assert(fabs(uv2[2] - 0.0) < 0.01);

Q_assert(fabs(uv2[0] - uv[0]) < 0.01);
Q_assert(fabs(uv2[1] - uv[1]) < 0.01);

Q_assert(fabs(wp2[0] - wp[0]) < 0.01);
Q_assert(fabs(wp2[1] - wp[1]) < 0.01);
Q_assert(fabs(wp2[2] - wp[2]) < 0.01);

// end test
#endif
