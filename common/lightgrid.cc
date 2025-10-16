#include <common/lightgrid.hh>

std::optional<bspx_lightgrid_samples_t> Lightgrid_SampleAtPoint(
    const lightgrid_octree_t &lightgrid, const qvec3f &world_point)
{
    return {};
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
} // namespace lightgrid
