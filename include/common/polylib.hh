/* common/polylib.h */

#pragma once

#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/cmdlib.hh>
#include <variant>
#include <array>
#include <vector>

#include <type_traits>
#include <stdexcept>
#include <optional>

template<class T, class = void>
struct is_iterator : std::false_type
{
};

template<class T>
struct is_iterator<T,
    std::void_t<typename std::iterator_traits<T>::difference_type,
                typename std::iterator_traits<T>::pointer,
                typename std::iterator_traits<T>::reference,
                typename std::iterator_traits<T>::value_type,
                typename std::iterator_traits<T>::iterator_category>> : std::true_type
{
};

template<class T>
constexpr bool is_iterator_v = is_iterator<T>::value;

namespace polylib
{

constexpr size_t MAX_POINTS_ON_WINDING = 64;

constexpr vec_t ON_EPSILON = 0.1;
constexpr vec_t DEFAULT_BOGUS_RANGE = 65536.0;

using winding_edges_t = std::vector<plane_t>;

inline bool PointInWindingEdges(const winding_edges_t &wi, const vec3_t &point)
{
    /* edgeplane faces toward the center of the face */
    for (auto &edgeplane : wi) {
        if (DotProduct(point, edgeplane.normal) - edgeplane.dist < 0)
            return false;
    }

    return true;
}

// Polygonal winding; uses stack allocation for the first N
// points, and moves to a dynamic array after that.
template<size_t N>
struct winding_base_t
{
private:
    using array_type = std::array<qvec3d, N>;
    using vector_type = std::vector<qvec3d>;
    using variant_type = std::variant<array_type, vector_type>;
    size_t count = 0;
    variant_type data;

public:
    // default constructor
    winding_base_t() { }

    // construct winding with initial size; may allocate
    // memory, and sets size, but does not initialize any
    // of them.
    winding_base_t(const size_t &initial_size) :
        count(initial_size),
        data(count > N ? variant_type(vector_type(initial_size)) : variant_type(array_type()))
    {
    }

    // construct winding from range.
    // iterators must have operator-.
    template<typename Iter, std::enable_if_t<is_iterator_v<Iter>, int> = 0>
    winding_base_t(Iter begin, Iter end) :
        count(end - begin),
        data(count > N ? variant_type(vector_type(begin, end)) : variant_type(array_type()))
    {
        if (!is_dynamic())
            std::copy(begin, end, std::get<array_type>(data).begin());
    }

    // copy constructor
    winding_base_t(const winding_base_t &copy) :
        count(copy.count),
        data(copy.data)
    {
    }

    // move constructor
    winding_base_t(winding_base_t &&move) :
        count(move.count),
        data(std::move(move.data))
    {
        move.count = 0;
    }

    // assignment copy
    inline winding_base_t &operator=(const winding_base_t &copy)
    {
        count = copy.count;
        data = copy.data;

        return *this;
    }

    // assignment move
    inline winding_base_t &operator=(winding_base_t &&move)
    {
        count = move.count;
        data = std::move(move.data);

        move.count = 0;

        return *this;
    }

    inline bool is_dynamic() const
    {
        return std::holds_alternative<vector_type>(data);
    }

    inline const size_t &size() const
    {
        return count;
    }

    inline qvec3d &at(const size_t &index)
    {
#ifdef _DEBUG
        if (index >= count)
            throw std::invalid_argument("index");
#endif

        if (is_dynamic())
            return std::get<vector_type>(data)[index];
        return std::get<array_type>(data)[index];
    }

    inline const qvec3d &at(const size_t &index) const
    {
#ifdef _DEBUG
        if (index >= count)
            throw std::invalid_argument("index");
#endif

        if (is_dynamic())
            return std::get<vector_type>(data)[index];
        return std::get<array_type>(data)[index];
    }

    inline qvec3d &operator[](const size_t &index)
    {
        return at(index);
    }

    inline const qvec3d &operator[](const size_t &index) const
    {
        return at(index);
    }

    const qvec3d *begin() const
    {
        if (is_dynamic())
            return &std::get<vector_type>(data)[0];
        return &std::get<array_type>(data)[0];
    }

    const qvec3d *end() const
    {
        if (is_dynamic())
            return &std::get<vector_type>(data)[count];
        return &std::get<array_type>(data)[count];
    }

    void push_back(const qvec3d &vec)
    {
        // move us to dynamic
        if (count == N && !is_dynamic())
            data = vector_type(begin(), end());

        if (is_dynamic())
            std::get<vector_type>(data).push_back(vec);
        else
            std::get<array_type>(data)[count] = vec;

        count++;
    }

    void clear()
    {
        if (is_dynamic())
            std::get<vector_type>(data).clear();

        count = 0;
        data = array_type();
    }

    vec_t area() const
    {
        if (count < 3)
            throw std::domain_error("count");

        vec_t total = 0;

        for (size_t i = 2; i < count; i++) {
            qvec3d d1 = at(i - 1) - at(0);
            qvec3d d2 = at(i) - at(0);
            total += 0.5 * qv::length(qv::cross(d1, d2));
        }

        return total;
    }

    qvec3d center() const
    {
        qvec3d center { };

        for (auto &point : *this)
            center += point;

        return center * (1.0 / count);
    }

    // TODO: qboundf/qboundd type
    void bounds(vec3_t out_mins, vec3_t out_maxs) const
    {
        ClearBounds(out_mins, out_maxs);

        for (auto &point : *this)
            AddPointToBounds((const vec_t *) &point[0], out_mins, out_maxs);
    }

    /*
     * ============
     * RemoveColinearPoints
     * ============
     */
    void remove_colinear()
    {
        winding_base_t temp;

        // CHECK: would this be more efficient to instead store a running
        // list of indices that *are* collinear, so we can return sooner
        // before copying points over?
        for (size_t i = 0; i < count; i++) {
            size_t j = (i + 1) % count;
            size_t k = (i + count - 1) % count;
            qvec3d v1 = qv::normalize(at(j) - at(i));
            qvec3d v2 = qv::normalize(at(i) - at(k));

            if (qv::dot(v1, v2) < 0.999)
                temp.push_back(at(i));
        }

        if (count != temp.count)
            *this = std::move(temp);
    }

    plane_t plane() const
    {
        plane_t p;

        qvec3d v1 = at(0) - at(1);
        qvec3d v2 = at(2) - at(1);
        qvec3d normal = qv::normalize(qv::cross(v1, v2));

        for (size_t i = 0; i < 3; i++)
            p.normal[i] = normal[i];

        p.dist = qv::dot(at(0), normal);

        return p;
    }

    static winding_base_t from_plane(const vec3_t &normal, const vec_t &dist)
    {
        /* find the major axis */
        vec_t max = -VECT_MAX;
        int32_t x = -1;
        for (size_t i = 0; i < 3; i++) {
            vec_t v = fabs(normal[i]);

            if (v > max) {
                x = i;
                max = v;
            }
        }

        if (x == -1)
            FError("no axis found");

        qvec3d vup { };

        switch (x) {
            case 0:
            case 1: vup[2] = 1; break;
            case 2: vup[0] = 1; break;
        }

        vec_t v = DotProduct(&vup[0], normal);
        vup += qvec3d { normal } * -v;
        vup = qv::normalize(vup);

        vec3_t org;
        VectorScale(normal, dist, org);

        qvec3d vright = qv::cross(vup, qvec3d(normal));

        vup *= 10e6;
        vright *= 10e6;

        /* project a really big axis aligned box onto the plane */
        winding_base_t w(4);
        qvec3d vorg{org};

        w[0] = vorg - vright + vup;
        w[1] = vorg + vright + vup;
        w[2] = vorg + vright - vup;
        w[3] = vorg - vright - vup;

        return w;
    }

    void check(const vec_t &bogus_range = DEFAULT_BOGUS_RANGE) const
    {
        vec3_t dir, edgenormal;

        if (count < 3)
            FError("{} points", count);

        vec_t a = area();
        if (a < 1)
            FError("{} area", a);

        plane_t face = plane();

        for (size_t i = 0; i < count; i++) {
            const qvec3d &p1 = at(i);
            size_t j = 0;

            for (; j < 3; j++)
                if (p1[j] > bogus_range || p1[j] < -bogus_range)
                    FError("BOGUS_RANGE: {}", p1[j]);

            /* check the point is on the face plane */
            vec_t d = DotProduct(&p1[0], face.normal) - face.dist;
            if (d < -ON_EPSILON || d > ON_EPSILON)
                FError("point off plane");

            /* check the edge isn't degenerate */
            const qvec3d &p2 = get[(i + 1) % count];
            qvec3d dir = p2 - p1;

            if (qv::length(dir) < ON_EPSILON)
                FError("degenerate edge");

            qvec3d edgenormal = qv::normalize(qv::cross(face.normal, dir));
            vec_t edgedist = qv::dot(p1, edgenormal) + ON_EPSILON;

            /* all other points must be on front side */
            for (size_t j = 0; j < count; j++) {
                if (j == i)
                    continue;
                d = qv::dot(at(j), edgenormal);
                if (d > edgedist)
                    FError("non-convex");
            }
        }
    }

    std::vector<qvec3f> glm_winding_points() const
    {
        std::vector<qvec3f> points;
        points.resize(count);
        std::copy(begin(), end(), points.begin());
        return points;
    }

    static inline winding_base_t from_winding_points(const std::vector<qvec3f> &points)
    {
        return { points.begin(), points.end() };
    }

    winding_edges_t winding_edges() const
    {
        plane_t p = plane();

        winding_edges_t result(count);

        for (size_t i = 0; i < count; i++) {
            plane_t &dest = result[i];

            const qvec3d &v0 = at(i);
            const qvec3d &v1 = get((i + 1) % count);

            qvec3d edgevec = qv::normalize(v1 - v0);
            qvec3d normal = qv::cross(edgevec, p.normal);
            for (size_t i = 0; i < 3; i++)
                dest.normal[i] = normal[i];
            dest.dist = qv::dot(normal, v0);
        }

        return result;
    }

    std::array<std::optional<winding_base_t>, 2> clip(const vec3_t &normal, const vec_t &dist) const
    {
        vec_t *dists = (vec_t *) alloca(sizeof(vec_t) * (count + 1));
        side_t *sides = (side_t *) alloca(sizeof(side_t) * (count + 1));
        int counts[3] { };

        /* determine sides for each point */
        size_t i;

        for (i = 0; i < count; i++) {
            vec_t dot = DotProduct(&at(i)[0], normal);
            dot -= dist;

            dists[i] = dot;

            if (dot > ON_EPSILON)
                sides[i] = SIDE_FRONT;
            else if (dot < -ON_EPSILON)
                sides[i] = SIDE_BACK;
            else
                sides[i] = SIDE_ON;

            counts[sides[i]]++;
        }

        sides[i] = sides[0];
        dists[i] = dists[0];

        if (!counts[0])
            return { std::nullopt, *this };
        else if (!counts[1])
            return { *this, std::nullopt };

        /* can't use counts[0]+2 because of fp grouping errors */
        std::array<winding_base_t, 2> results { };

        for (i = 0; i < count; i++) {
            const qvec3d &p1 = at(i);

            if (sides[i] == SIDE_ON) {
                results[0].push_back(p1);
                results[1].push_back(p1);
                continue;
            } else if (sides[i] == SIDE_FRONT) {
                results[0].push_back(p1);
            } else if (sides[i] == SIDE_BACK) {
                results[1].push_back(p1);
            }

            if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
                continue;

            /* generate a split point */
            const qvec3d &p2 = at((i + 1) % count);

            vec_t dot = dists[i] / (dists[i] - dists[i + 1]);
            qvec3d mid { };

            for (size_t j = 0; j < 3; j++) { /* avoid round off error when possible */
                if (normal[j] == 1)
                    mid[j] = dist;
                else if (normal[j] == -1)
                    mid[j] = -dist;
                else
                    mid[j] = p1[j] + dot * (p2[j] - p1[j]);
            }
        
            results[0].push_back(mid);
            results[1].push_back(mid);
        }

        if (results[0].count > MAX_POINTS_ON_WINDING || results[1].count > MAX_POINTS_ON_WINDING)
            FError("MAX_POINTS_ON_WINDING");

        return { std::move(results[0]), std::move(results[1]) };
    }

    std::optional<winding_base_t> chop(const vec3_t &normal, const vec_t &dist)
    {
        auto clipped = clip(normal, dist);

        clear();

        return clipped[0];
    }

    using save_fn_t = void (*)(winding_base_t &w, void *userinfo);

    void dice(vec_t subdiv, save_fn_t save_fn, void *userinfo)
    {
        vec3_t mins, maxs;

        bounds(mins, maxs);

        size_t i;

        for (i = 0; i < 3; i++)
            if (floor((mins[i] + 1) / subdiv) < floor((maxs[i] - 1) / subdiv))
                break;

        if (i == 3) {
            // no splitting needed
            save_fn(*this, userinfo);
            return;
        }

        //
        // split the winding
        //
        vec3_t split { };
        split[i] = 1;
        vec_t dist = subdiv * (1 + floor((mins[i] + 1) / subdiv));
        auto clipped = clip(split, dist);
        clear();

        //
        // create a new patch
        //
        for (auto &o : clipped)
            if (o.has_value())
                o->dice(subdiv, save_fn, userinfo);
    }

    static winding_base_t from_face(const mbsp_t *bsp, const bsp2_dface_t *f)
    {
        winding_base_t w(f->numedges);

        for (size_t i = 0; i < f->numedges; i++) {
            int32_t se = bsp->dsurfedges[f->firstedge + i];
            uint32_t v;

            if (se < 0)
                v = bsp->dedges[-se].v[1];
            else
                v = bsp->dedges[se].v[0];

            const dvertex_t *dv = &bsp->dvertexes[v];

            for (size_t j = 0; j < 3; j++) {
                w[i][j] = dv->point[j];
            }
        }

        // CHECK: can we do the above + colinear checks
        // in a single pass?
        w.remove_colinear();

        return w;
    }
};

// the default amount of points to keep on stack
constexpr size_t STACK_POINTS_ON_WINDING = MAX_POINTS_ON_WINDING / 4;

using winding_t = winding_base_t<STACK_POINTS_ON_WINDING>;

}; // namespace polylib
