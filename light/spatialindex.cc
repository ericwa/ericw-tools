/*  Copyright (C) 2025 Eric Wasylishen

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

#include "light/spatialindex.hh"

spatialindex_t::~spatialindex_t()
{
    clear();
}

spatialindex_t::spatialindex_t() { }

static void FilterFunc(const RTCFilterFunctionNArguments *args)
{
    constexpr int VALID = -1;
    constexpr int REJECT = 0;

    for (unsigned int i = 0; i < args->N; ++i) {
        if (args->valid[i] != VALID) {
            continue;
        }

        // check geometry normal (unnormalized) and ray normal (unnormalized)
        qvec3f geom_normal = qvec3f(RTCHitN_Ng_x(args->hit, args->N, i), RTCHitN_Ng_y(args->hit, args->N, i),
            RTCHitN_Ng_z(args->hit, args->N, i));

        qvec3f ray_normal = qvec3f(RTCRayN_dir_x(args->ray, args->N, i), RTCRayN_dir_y(args->ray, args->N, i),
            RTCRayN_dir_z(args->ray, args->N, i));

        if (qv::dot(geom_normal, ray_normal) > 0) {
            // backface cull
            args->valid[i] = REJECT;
        }
    }
}

void spatialindex_t::commit()
{
    assert(state == state_t::filling_geom);
    assert(scene == nullptr);
    assert(device == nullptr);

    device = rtcNewDevice(nullptr);
    scene = rtcNewScene(device);

    // create + populate geometry
    RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);
    rtcSetSharedGeometryBuffer(
        geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, vertices.data(), 0, 4 * sizeof(float), vertices.size());
    rtcSetSharedGeometryBuffer(
        geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, indices.data(), 0, sizeof(tri_t), indices.size());
    rtcSetGeometryIntersectFilterFunction(geom, FilterFunc);
    rtcCommitGeometry(geom);

    rtcAttachGeometry(scene, geom);
    rtcReleaseGeometry(geom);

    rtcCommitScene(scene);

    state = state_t::tracing;
}

void spatialindex_t::add_poly(const polylib::winding_t &winding, std::any payload)
{
    assert(state == state_t::filling_geom);

    if (winding.size() < 3)
        return;

    uint32_t start_vertex = vertices.size();

    // push winding verts in CCW order
    for (int i = 0; i < winding.size(); ++i) {
        vertices.push_back(qvec4f(qvec3f(winding[winding.size() - 1 - i]), 0.0f));
    }

    // push the  CCW triangles
    for (int i = 2; i < winding.size(); ++i) {
        indices.push_back({start_vertex, start_vertex + i - 1, start_vertex + i});
        payloads_per_tri.push_back(payload);
    }
}

void spatialindex_t::clear()
{
    // geometry is owned by scene, no need to release

    state = state_t::filling_geom;

    if (scene) {
        rtcReleaseScene(scene);
        scene = nullptr;
    }
    if (device) {
        rtcReleaseDevice(device);
        device = nullptr;
    }
    vertices.clear();
    indices.clear();
    payloads_per_tri.clear();
}

hitresult_t spatialindex_t::trace_ray(const qvec3f &origin, const qvec3f &direction)
{
    assert(state == state_t::tracing);

    RTCRayHit rayhit;
    rayhit.ray.org_x = origin[0];
    rayhit.ray.org_y = origin[1];
    rayhit.ray.org_z = origin[2];
    rayhit.ray.dir_x = direction[0];
    rayhit.ray.dir_y = direction[1];
    rayhit.ray.dir_z = direction[2];
    rayhit.ray.tnear = 0;
    rayhit.ray.tfar = std::numeric_limits<float>::infinity();
    rayhit.ray.mask = 0xff'ff'ff'ff;
    rayhit.ray.flags = 0;
    rayhit.ray.time = 0;
    rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rayhit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;

    rtcIntersect1(scene, &rayhit);

    hitresult_t result;
    if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
        result.hit = true;
        result.hitpos = origin + (direction * rayhit.ray.tfar);

        assert(rayhit.hit.primID < payloads_per_tri.size());
        result.hitpayload = &payloads_per_tri[rayhit.hit.primID];
    } else {
        result.hit = false;
        result.hitpayload = nullptr;
    }
    return result;
}
