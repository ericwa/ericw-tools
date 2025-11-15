#pragma once

#include <common/qvec.hh>
#include <common/bspxfile.hh>

#include <optional>

std::optional<bspx_lightgrid_samples_t> Lightgrid_SampleAtPoint(
    const lightgrid_octree_t &lightgrid, const qvec3f &world_point);

std::optional<lightgrids_sampleset_t> Lightgrids_SampleAtPoint(
    const lightgrids_t &lightgrid, const qvec3f &world_point);

namespace lightgrid
{
// if set, it's an index in the leafs array
constexpr uint32_t FLAG_LEAF = 1 << 31;
constexpr uint32_t FLAG_OCCLUDED = 1 << 30;
constexpr uint32_t FLAGS = (FLAG_LEAF | FLAG_OCCLUDED);
// if neither flags are set, it's a node index

/**
 * returns the octant index in [0..7]
 */
int child_index(qvec3i division_point, qvec3i test_point);

/**
 * returns octant index `i`'s mins and size
 */
std::tuple<qvec3i, qvec3i> get_octant(int i, qvec3i mins, qvec3i size, qvec3i division_point);

int get_grid_index(const qvec3i &grid_size, int x, int y, int z);

bspx_lightgrid_samples_t octree_lookup_r(const lightgrid_octree_t &octree, uint32_t node_index, qvec3i test_point);
lightgrids_sampleset_t octree_lookup_r(const subgrid_t &octree, uint32_t node_index, qvec3i test_point);
} // namespace lightgrid
