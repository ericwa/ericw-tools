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

#ifndef __LIGHT_LIGHT2_H__
#define __LIGHT_LIGHT2_H__

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/log.hh>
#include <common/threads.hh>
#include <common/polylib.hh>

#include <light/litfile.hh>
#include <light/trace.hh>
#include <light/settings.hh>

#include <vector>
#include <map>
#include <set>
#include <string>
#include <cassert>
#include <limits>
#include <sstream>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>


using batch_t = std::vector<int>;
using batches_t = std::vector<batch_t>;
/// a directed edge can be used by more than one face, e.g. two cube touching just along an edge
using edgeToFaceMap_t = std::map<std::pair<int,int>, std::vector<const bsp2_dface_t *>>;

class contributing_face_t {
public:
    const bsp2_dface_t *contribFace;
    const bsp2_dface_t *refFace;
    
    std::vector<glm::vec4> contribFaceEdgePlanes;
};

using all_contrib_faces_t = std::map<const bsp2_dface_t *, std::vector<contributing_face_t>>;
using blocking_plane_t = glm::vec4;

struct lightbatchthread_info_t {
    batches_t all_batches;
    all_contrib_faces_t all_contribFaces;
    bsp2_t *bsp;
};

void *LightBatchThread(void *arg);

std::vector<blocking_plane_t> BlockingPlanes(const bsp2_t *bsp, const bsp2_dface_t *f, const edgeToFaceMap_t &edgeToFaceMap);
edgeToFaceMap_t MakeEdgeToFaceMap(const bsp2_t *bsp);
batches_t MakeLightingBatches(const bsp2_t *bsp);
all_contrib_faces_t MakeContributingFaces(const bsp2_t *bsp);
std::vector<contributing_face_t> SetupContributingFaces(const bsp2_t *bsp, const bsp2_dface_t *face, const edgeToFaceMap_t &edgeToFaceMap);

std::pair<bool, glm::mat4x4> RotationAboutLineSegment(glm::vec3 p0, glm::vec3 p1,
                                                      glm::vec3 face0Norm, glm::vec3 face1Norm);

glm::mat4x4 WorldToTexSpace(const bsp2_t *bsp, const bsp2_dface_t *f);
glm::mat4x4 TexSpaceToWorld(const bsp2_t *bsp, const bsp2_dface_t *f);

#endif /* __LIGHT_LIGHT2_H__ */
