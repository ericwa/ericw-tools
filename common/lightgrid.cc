#include <common/lightgrid.hh>

std::optional<bspx_lightgrid_samples_t> Lightgrid_SampleAtPoint(
    const lightgrid_octree_t &lightgrid, const qvec3f &world_point)
{
    // convert world_point to grid space

    qvec3f local_point_f = (world_point - lightgrid.header.grid_mins) / lightgrid.header.grid_dist;

    qvec3i local_point_i = {Q_rint(local_point_f[0]), Q_rint(local_point_f[1]), Q_rint(local_point_f[2])};

    // check if in bounds
    for (int axis = 0; axis < 3; ++axis) {
        if (local_point_i[axis] < 0 || local_point_i[axis] >= lightgrid.header.grid_size[axis]) {
            return {};
        }
    }

    return lightgrid::octree_lookup_r(lightgrid, lightgrid.header.root_node, local_point_i);
}

std::optional<lightgrids_sampleset_t> Lightgrids_SampleAtPoint(const lightgrids_t &lightgrid, const qvec3f &world_point)
{
    // check if we're inside any subgirds, in order
    for (const auto &subgrid : lightgrid.subgrids) {
        // convert world_point to grid space

        qvec3f local_point_f = (world_point - subgrid.header.grid_mins) / subgrid.header.grid_dist;

        qvec3i local_point_i = {Q_rint(local_point_f[0]), Q_rint(local_point_f[1]), Q_rint(local_point_f[2])};

        // check if in bounds
        bool out_of_bounds = false;
        for (int axis = 0; axis < 3; ++axis) {
            if (local_point_i[axis] < 0 || local_point_i[axis] >= subgrid.header.grid_size[axis]) {
                out_of_bounds = true;
            }
        }

        if (out_of_bounds)
            continue; // try next subgrid

        // we're in bounds, use this subgrid
        return lightgrid::octree_lookup_r(subgrid, subgrid.header.root_node, local_point_i);
    }

    return std::nullopt;
}

namespace lightgrid
{
int child_index(qvec3i division_point, qvec3i test_point)
{
    int sign[3];
    for (int i = 0; i < 3; ++i)
        sign[i] = (test_point[i] >= division_point[i]);

    return (4 * sign[0]) + (2 * sign[1]) + (sign[2]);
}

std::tuple<qvec3i, qvec3i> get_octant(int i, qvec3i mins, qvec3i size, qvec3i division_point)
{
    qvec3i child_mins;
    qvec3i child_size;
    for (int axis = 0; axis < 3; ++axis) {
        int bit;
        if (axis == 0) {
            bit = 4;
        } else if (axis == 1) {
            bit = 2;
        } else {
            bit = 1;
        }

        if (i & bit) {
            child_mins[axis] = division_point[axis];
            child_size[axis] = mins[axis] + size[axis] - division_point[axis];
        } else {
            child_mins[axis] = mins[axis];
            child_size[axis] = division_point[axis] - mins[axis];
        }
    }
    return {child_mins, child_size};
}

int get_grid_index(const qvec3i &grid_size, int x, int y, int z)
{
    return (grid_size[0] * grid_size[1] * z) + (grid_size[0] * y) + x;
}

bspx_lightgrid_samples_t octree_lookup_r(const lightgrid_octree_t &octree, uint32_t node_index, qvec3i test_point)
{
    if (node_index & lightgrid::FLAG_OCCLUDED) {
        bspx_lightgrid_samples_t result;
        result.occluded = true;
        return result;
    }
    if (node_index & lightgrid::FLAG_LEAF) {
        uint32_t leaf_index = node_index & ~lightgrid::FLAG_LEAF;
        const lightgrid_leaf_t &leaf = octree.leafs[leaf_index];

        qvec3i pos_local = test_point - leaf.mins;

        return leaf.at(pos_local[0], pos_local[1], pos_local[2]);
    }
    auto &node = octree.nodes[node_index];
    int i = child_index(node.division_point, test_point); // [0..7]
    return octree_lookup_r(octree, node.children[i], test_point);
}

lightgrids_sampleset_t octree_lookup_r(const subgrid_t &octree, uint32_t node_index, qvec3i test_point)
{
    if (node_index & lightgrid::FLAG_OCCLUDED) {
        lightgrids_sampleset_t result;
        result.occluded = true;
        return result;
    }
    if (node_index & lightgrid::FLAG_LEAF) {
        uint32_t leaf_index = node_index & ~lightgrid::FLAG_LEAF;
        const auto &leaf = octree.leafs[leaf_index];

        qvec3i pos_local = test_point - leaf.mins;

        return leaf.at(pos_local[0], pos_local[1], pos_local[2]);
    }
    auto &node = octree.nodes[node_index];
    int i = child_index(node.division_point, test_point); // [0..7]
    return octree_lookup_r(octree, node.children[i], test_point);
}

} // namespace lightgrid
