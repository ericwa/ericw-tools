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

#pragma once

#include <common/polylib.hh>
#include <embree4/rtcore.h>

#include <any>

struct hitresult_t
{
    bool hit;
    qvec3f hitpos;
    // points to data owned by spatialindex_t, or nullptr if we didn't hit anything
    const std::any *hitpayload;
};

enum class state_t
{
    filling_geom,
    tracing
};

class spatialindex_t
{
private:
    state_t state = state_t::filling_geom;

    RTCDevice device = nullptr;
    RTCScene scene = nullptr;

    struct tri_t
    {
        uint32_t v0, v1, v2;
    };
    std::vector<qvec4f> vertices;
    std::vector<tri_t> indices;
    std::vector<std::any> payloads_per_tri;

public:
    ~spatialindex_t();
    spatialindex_t();
    spatialindex_t(const spatialindex_t &other) = delete;
    spatialindex_t &operator=(const spatialindex_t &other) = delete;

    void clear();
    void add_poly(const polylib::winding_t &winding, std::any payload);
    void commit();

    state_t get_state() const { return state; }

public:
    hitresult_t trace_ray(const qvec3f &origin, const qvec3f &direction);
};
