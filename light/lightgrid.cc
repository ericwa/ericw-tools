/*  Copyright (C) 1996-1997  Id Software, Inc.

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

#include <cstdint>
#include <iostream>
#include <fstream>
#include <fmt/ostream.h>
#include <fmt/chrono.h>

#include <light/light.hh>
#include <light/phong.hh>
#include <light/bounce.hh>
#include <light/entities.hh>
#include <light/ltface.hh>

#include <common/log.hh>
#include <common/bsputils.hh>
#include <common/fs.hh>
#include <common/prtfile.hh>
#include <common/imglib.hh>
#include <common/parallel.hh>

#include <memory>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <mutex>
#include <string>

#include <common/qvec.hh>

static std::vector<uint8_t> StringToVector(const std::string &str)
{
    std::vector<uint8_t> result(str.begin(), str.end());
    return result;
}

static aabb3f LightGridBounds(const mbsp_t &bsp)
{
    aabb3f result;

    // see if `_lightgrid_hint` entities are in use
    for (auto &entity : GetEntdicts()) {
        if (entity.get_int("_lightgrid_hint")) {
            qvec3d point{};
            entity.get_vector("origin", point);
            result += point;
        }
    }

    if (result.valid()) {
        auto size = result.size();
        if (size[0] > 0 && size[1] > 0 && size[2] > 0) {
            return result;
        }
    }

    result = Model_BoundsOfFaces(bsp, bsp.dmodels[0]);
    return result;
}

void LightGrid(bspdata_t *bspdata)
{
    if (!light_options.lightgrid.value())
        return;

    logging::funcheader();

    auto &bsp = std::get<mbsp_t>(bspdata->bsp);

    const auto grid_bounds = LightGridBounds(bsp);
    const qvec3f grid_maxs = grid_bounds.maxs();
    const qvec3f grid_mins = grid_bounds.mins();
    const qvec3f world_size = grid_maxs - grid_mins;

    // number of grid points on each axis
    const qvec3i grid_size = {
        ceil(world_size[0] / light_options.lightgrid_dist.value()[0]),
        ceil(world_size[1] / light_options.lightgrid_dist.value()[1]),
        ceil(world_size[2] / light_options.lightgrid_dist.value()[2])
    };

    auto grid_index_to_world = [&](const qvec3i &index) -> qvec3f {
        return grid_mins + (index * light_options.lightgrid_dist.value());
    };

    std::vector<lightgrid_samples_t> grid_result;
    grid_result.resize(grid_size[0] * grid_size[1] * grid_size[2]);

    std::vector<uint8_t> occlusion;
    occlusion.resize(grid_size[0] * grid_size[1] * grid_size[2]);

    logging::parallel_for(0, grid_size[0] * grid_size[1] * grid_size[2], [&](int sample_index){
        const int z = (sample_index / (grid_size[0] * grid_size[1]));
        const int y = (sample_index / grid_size[0]) % grid_size[1];
        const int x = sample_index % grid_size[0];

        qvec3d world_point = grid_mins + (qvec3d{x,y,z} * light_options.lightgrid_dist.value());

        bool occluded = Light_PointInWorld(&bsp, world_point);
        if (occluded) {
            // search for a nearby point
            auto [fixed_pos, success] = FixLightOnFace(&bsp, world_point, false, 2.0f);
            if (success) {
                occluded = false;
                world_point = fixed_pos;
            }
        }

        lightgrid_samples_t samples;

        if (!occluded)
            samples = CalcLightgridAtPoint(&bsp, world_point);

        grid_result[sample_index] = samples;
        occlusion[sample_index] = occluded;
    });

    auto get_grid_index = [&](int x, int y, int z) -> int {
        return (grid_size[0] * grid_size[1] * z) + (grid_size[0] * y) + x;
    };

    // the maximum used styles across the map.
    const uint8_t num_styles = [&](){
        int result = 0;
        for (auto &samples : grid_result) {
            result = max(result, samples.used_styles());
        }
        return result;
    }();

    logging::print("     {} lightgrid_dist\n", light_options.lightgrid_dist.value());
    logging::print("     {} grid_size\n", grid_size);
    logging::print("     {} grid_mins\n", grid_mins);
    logging::print("     {} grid_maxs\n", grid_maxs);
    logging::print("     {} num_styles\n", num_styles);

    // non-final, experimental lump
    if (light_options.lightgrid_format.value() == lightgrid_format_t::UNIFORM) {
        std::ostringstream str(std::ios_base::out | std::ios_base::binary);
        str << endianness<std::endian::little>;
        str <= qvec3f{light_options.lightgrid_dist.value()};
        str <= grid_size;
        str <= grid_mins;
        str <= num_styles;

        for (const lightgrid_samples_t &samples : grid_result) {
            str <= static_cast<uint8_t>(samples.used_styles());
            for (int i = 0; i < samples.used_styles(); ++i) {
                str <= static_cast<uint8_t>(samples.samples_by_style[i].style);
                str <= samples.samples_by_style[i].round_to_int();
            }
        }

        // occlusion 3D array
        {
            std::vector<uint8_t> occlusion_bitarray;
            occlusion_bitarray.resize(
                static_cast<size_t>(ceil(grid_size[0] * grid_size[1] * grid_size[2] / 8.0)));

            // transfer bytes to bits
            for (int i = 0; i < occlusion.size(); ++i) {
                if (occlusion[i]) {
                    occlusion_bitarray[i / 8] |= 1 << (i % 8);
                }
            }

            // add to lump
            for (uint8_t byte : occlusion_bitarray) {
                str <= byte;
            }
        }

        auto vec = StringToVector(str.str());
        logging::print( "     {:8} bytes LIGHTGRID\n", vec.size());

        bspdata->bspx.transfer("LIGHTGRID", std::move(vec));
    }

    // other lump
    if (light_options.lightgrid_format.value() == lightgrid_format_t::CLUSTER) {
        const qvec3f grid_dist = qvec3f{light_options.lightgrid_dist.value()};

        std::ostringstream str(std::ios_base::out | std::ios_base::binary);
        str << endianness<std::endian::little>;
        str <= grid_dist;
        str <= grid_size;
        str <= grid_mins;
        str <= num_styles;

        auto cluster_to_leafnums = ClusterToLeafnumsMap(&bsp);
        for (int cluster = 0;; ++cluster) {
            auto it = cluster_to_leafnums.find(cluster);
            if (it == cluster_to_leafnums.end())
                break;

            // compute cluster bounds by adding up leaf bounds
            aabb3f bounds;
            for (int leafnum : it->second) {
                auto &leaf = bsp.dleafs[leafnum];
                bounds += aabb3f{leaf.mins, leaf.maxs};
            }

            qvec3i cluster_min_grid_coord = qv::floor((bounds.mins() - grid_mins) / grid_dist) + qvec3i(-1, -1, -1);
            qvec3i cluster_max_grid_coord = qv::ceil((bounds.maxs() - grid_mins) / grid_dist) + qvec3i(1, 1, 1);

            // clamp to overall size
            cluster_min_grid_coord = qv::max(qvec3i(0, 0, 0), cluster_min_grid_coord);
            cluster_max_grid_coord = qv::min(grid_size, cluster_max_grid_coord);

            qvec3i cluster_grid_size = cluster_max_grid_coord - cluster_min_grid_coord;

            str <= cluster_min_grid_coord;
            str <= cluster_grid_size;

            // logging::print("cluster {} bounds grid mins {} grid size {}\n", cluster, cluster_min_grid_coord, cluster_grid_size);

            auto &cm = cluster_min_grid_coord;
            auto &cs = cluster_grid_size;

            for (int z = cm[2]; z < (cm[2] + cs[2]); ++z) {
                for (int y = cm[1]; y < (cm[1] + cs[1]); ++y) {
                    for (int x = cm[0]; x < (cm[0] + cs[0]); ++x) {
                        int sample_index = get_grid_index(x, y, z);

                        if (occlusion[sample_index]) {
                            str <= static_cast<uint8_t>(0xff);
                            continue;
                        }

                        const lightgrid_samples_t &samples = grid_result[sample_index];
                        str <= static_cast<uint8_t>(samples.used_styles());
                        for (int i = 0; i < samples.used_styles(); ++i) {
                            str <= static_cast<uint8_t>(samples.samples_by_style[i].style);
                            str <= samples.samples_by_style[i].round_to_int();
                        }
                    }
                }
            }
        }

        auto vec = StringToVector(str.str());
        logging::print("     {:8} bytes LIGHTGRID_PERCLUSTER\n", vec.size());

        bspdata->bspx.transfer("LIGHTGRID_PERCLUSTER", std::move(vec));
    }

    // octree lump
    if (light_options.lightgrid_format.value() == lightgrid_format_t::OCTREE)
    {
        /**
         * returns the octant index in [0..7]
         */
        auto child_index = [](qvec3i division_point, qvec3i test_point) -> int {
            int sign[3];
            for (int i = 0; i < 3; ++i)
                sign[i] = (test_point[i] >= division_point[i]);

            return (4 * sign[0]) + (2 * sign[1]) + (sign[2]);
        };

        Q_assert(child_index({1,1,1}, {2,2,2}) == 7);
        Q_assert(child_index({1,1,1}, {1,1,0}) == 6);
        Q_assert(child_index({1,1,1}, {1,0,1}) == 5);
        Q_assert(child_index({1,1,1}, {1,0,0}) == 4);
        Q_assert(child_index({1,1,1}, {0,1,1}) == 3);
        Q_assert(child_index({1,1,1}, {0,1,0}) == 2);
        Q_assert(child_index({1,1,1}, {0,0,1}) == 1);
        Q_assert(child_index({1,1,1}, {0,0,0}) == 0);

        /**
         * returns octant index `i`'s mins and size
         */
        auto get_octant = [](int i, qvec3i mins, qvec3i size, qvec3i division_point) -> std::tuple<qvec3i, qvec3i> {
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
        };

        Q_assert(get_octant(0, {0,0,0}, {2,2,2}, {1,1,1}) == (std::tuple<qvec3i, qvec3i>{{0,0,0}, {1,1,1}}));
        Q_assert(get_octant(7, {0,0,0}, {2,2,2}, {1,1,1}) == (std::tuple<qvec3i, qvec3i>{{1,1,1}, {1,1,1}}));

        /**
         * given a bounding box, selects the division point.
         */
        auto get_division_point = [](qvec3i mins, qvec3i size) -> qvec3i {
            return mins + (size / 2);
        };

        auto is_all_occluded = [&](qvec3i mins, qvec3i size) -> bool {
            for (int z = mins[2]; z < (mins[2] + size[2]); ++z) {
                for (int y = mins[1]; y < (mins[1] + size[1]); ++y) {
                    for (int x = mins[0]; x < (mins[0] + size[0]); ++x) {
                        int sample_index = get_grid_index(x, y, z);
                        if (!occlusion[sample_index]) {
                            return false;
                        }
                    }
                }
            }
            return true;
        };

        constexpr int MAX_DEPTH = 5;
        // if any axis is fewer than this many grid points, don't bother subdividing further, just create a leaf
        constexpr int MIN_NODE_DIMENSION = 4;

        // if set, it's an index in the leafs array
        constexpr uint32_t FLAG_LEAF = 1 << 31;
        constexpr uint32_t FLAG_OCCLUDED = 1 << 30;
        constexpr uint32_t FLAGS = (FLAG_LEAF | FLAG_OCCLUDED);
        // if neither flags are set, it's a node index

        struct octree_node {
            qvec3i division_point;
            std::array<uint32_t, 8> children;
        };

        struct octree_leaf {
            qvec3i mins, size;
        };

        std::vector<octree_node> octree_nodes;
        std::vector<octree_leaf> octree_leafs;

        int occluded_cells = 0;

        /**
         * - inserts either a node or leaf
         * - returns one of:
         *   - FLAG_OCCLUDED if the entire bounds is occluded
         *   - (FLAG_LEAF | leaf_num) for a leaf - a literal chunk of grid samples
         *   - otherwise, it's a node index
         */
        std::function<int(qvec3i, qvec3i, int depth)> build_octree;
        build_octree = [&](qvec3i mins, qvec3i size, int depth) -> uint32_t {
            assert(size[0] > 0);
            assert(size[1] > 0);
            assert(size[2] > 0);

            // special case: fully occluded leaf, just represented as a flag bit
            if (is_all_occluded(mins, size)) {
                occluded_cells += size[0] * size[1] * size[2];
                return FLAG_OCCLUDED;
            }

            // decide whether we are creating a regular leaf or a node?
            bool make_leaf = false;
            if (size[0] < MIN_NODE_DIMENSION || size[1] < MIN_NODE_DIMENSION || size[2] < MIN_NODE_DIMENSION)
                make_leaf = true;
            if (depth == MAX_DEPTH)
                make_leaf = true;

            if (make_leaf) {
                // make a leaf
                const uint32_t leafnum = static_cast<uint32_t>(octree_leafs.size());
                octree_leafs.push_back({
                    .mins = mins, .size = size
                });
                return FLAG_LEAF | leafnum;
            }

            // make a node

            const qvec3i division_point = get_division_point(mins, size);

            // create the 8 child nodes/leafs recursively, store the returned indices
            std::array<uint32_t, 8> children;
            for (int i = 0; i < 8; ++i) {
                // figure out the mins/size of this child
                auto [child_mins, child_size] = get_octant(i, mins, size, division_point);
                children[i] = build_octree(child_mins, child_size, depth + 1);
            }

            // insert the node
            const uint32_t nodenum = static_cast<uint32_t>(octree_nodes.size());
            octree_nodes.push_back({
                .division_point = division_point,
                .children = children
            });
            return nodenum;
        };

        // build the root node
        const uint32_t root_node = build_octree(qvec3i{0,0,0}, grid_size, 0);

        // visualize the leafs
        {
            std::vector<polylib::winding_t> windings;

            for (auto &leaf : octree_leafs) {
                auto leaf_world_mins = grid_index_to_world(leaf.mins);
                auto leaf_world_maxs = grid_index_to_world(leaf.mins + leaf.size - qvec3i(1,1,1));

                aabb3d bounds(leaf_world_mins, leaf_world_maxs);

                auto bounds_windings = polylib::winding_t::aabb_windings(bounds);
                for (auto &w : bounds_windings) {
                    windings.push_back(std::move(w));
                }
            }

            WriteDebugPortals(windings, fs::path(light_options.sourceMap).replace_extension(".octree.prt"));
        }

        // stats
        int stored_cells = 0;
        for (auto &leaf : octree_leafs) {
            stored_cells += leaf.size[0] * leaf.size[1] * leaf.size[2];
        }
        logging::print("octree stored {} grid nodes + {} occluded = {} total, full stored {} (octree is {} percent)\n",
            stored_cells,
            occluded_cells,
            stored_cells + occluded_cells,
            occlusion.size(),
            100.0f * stored_cells / (float) occlusion.size());

        logging::print("octree nodes size: {} bytes ({} * {})\n",
            octree_nodes.size() * sizeof(octree_node),
            octree_nodes.size(),
            sizeof(octree_node));

        logging::print("octree leafs {} overhead {} bytes\n",
            octree_leafs.size(),
            octree_leafs.size() * sizeof(octree_leaf));

        // lookup function
        std::function<std::tuple<lightgrid_samples_t, bool>(uint32_t, qvec3i)> octree_lookup_r;
        octree_lookup_r = [&](uint32_t node_index, qvec3i test_point) -> std::tuple<lightgrid_samples_t, bool> {
            if (node_index & FLAG_OCCLUDED) {
                return {lightgrid_samples_t{}, true};
            }
            if (node_index & FLAG_LEAF) {
                auto &leaf = octree_leafs.at(node_index & (~FLAG_LEAF));
                // in actuality, we'd pull the data from a 3D grid stored in the leaf.
                int i = get_grid_index(test_point[0], test_point[1], test_point[2]);
                return {grid_result[i], occlusion[i]};
            }
            auto &node = octree_nodes[node_index];
            int i = child_index(node.division_point, test_point); // [0..7]
            return octree_lookup_r(node.children[i], test_point);
        };

        // self-check
        for (int z = 0; z < grid_size[2]; ++z) {
            for (int y = 0; y < grid_size[1]; ++y) {
                for (int x = 0; x < grid_size[0]; ++x) {
                    auto [color, occluded] = octree_lookup_r(root_node, {x, y, z});

                    int sample_index = get_grid_index(x, y, z);

                    // compare against original data
                    if (occluded) {
                        Q_assert(occlusion[sample_index]);
                    } else {
                        Q_assert(!occlusion[sample_index]);
                        Q_assert(grid_result[sample_index] == color);
                    }
                }
            }
        }

        // write out the binary data
        const qvec3f grid_dist = qvec3f{light_options.lightgrid_dist.value()};

        std::ostringstream str(std::ios_base::out | std::ios_base::binary);
        str << endianness<std::endian::little>;
        str <= grid_dist;
        str <= grid_size;
        str <= grid_mins;
        str <= num_styles;

        str <= static_cast<uint32_t>(root_node);

        // the nodes (fixed-size)
        str <= static_cast<uint32_t>(octree_nodes.size());
        for (const auto &node : octree_nodes) {
            str <= node.division_point;
            for (const auto child : node.children) {
                str <= child;
            }
        }

        // the leafs (each is variable sized)
        str <= static_cast<uint32_t>(octree_leafs.size());
        for (const auto &leaf : octree_leafs) {
            str <= leaf.mins;
            str <= leaf.size;

            // logging::print("cluster {} bounds grid mins {} grid size {}\n", cluster, cluster_min_grid_coord, cluster_grid_size);

            auto &cm = leaf.mins;
            auto &cs = leaf.size;

            for (int z = cm[2]; z < (cm[2] + cs[2]); ++z) {
                for (int y = cm[1]; y < (cm[1] + cs[1]); ++y) {
                    for (int x = cm[0]; x < (cm[0] + cs[0]); ++x) {
                        int sample_index = get_grid_index(x, y, z);

                        if (occlusion[sample_index]) {
                            str <= static_cast<uint8_t>(0xff);
                            continue;
                        }

                        const lightgrid_samples_t &samples = grid_result[sample_index];
                        str <= static_cast<uint8_t>(samples.used_styles());
                        for (int i = 0; i < samples.used_styles(); ++i) {
                            str <= static_cast<uint8_t>(samples.samples_by_style[i].style);
                            str <= samples.samples_by_style[i].round_to_int();
                        }
                    }
                }
            }
        }

        auto vec = StringToVector(str.str());
        logging::print("     {:8} bytes LIGHTGRID_OCTREE\n", vec.size());

        bspdata->bspx.transfer("LIGHTGRID_OCTREE", std::move(vec));
    }
}
