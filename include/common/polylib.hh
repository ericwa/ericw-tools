/* common/polylib.h */

#pragma once

#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/cmdlib.hh>
#include <common/aabb.hh>
#include <variant>
#include <array>
#include <vector>

#include <type_traits>
#include <stdexcept>
#include <optional>

namespace polylib
{

constexpr size_t MAX_POINTS_ON_WINDING = 96;

constexpr vec_t DEFAULT_BOGUS_RANGE = 65536.0;

using winding_edges_t = std::vector<qplane3d>;

inline bool PointInWindingEdges(const winding_edges_t &wi, const qvec3d &point)
{
    /* edgeplane faces toward the center of the face */
    for (auto &edgeplane : wi) {
        if (edgeplane.distance_to(point) < 0) {
            return false;
        }
    }

    return true;
}

// Polygonal winding; uses stack allocation for the first N
// points, and uses a dynamic vector for storage after that.
template<size_t N>
struct winding_base_t
{
private:
    using array_type = std::array<qvec3d, N>;
    using vector_type = std::vector<qvec3d>;
    using variant_type = std::variant<array_type, vector_type>;

public:
    size_t count = 0;
    array_type array;
    vector_type vector;
    template<bool is_const>
    class iterator_base
    {
        friend struct winding_base_t;

        using container_type = typename std::conditional_t<is_const, const winding_base_t *, winding_base_t *>;
        size_t index;
        container_type w;

        iterator_base(size_t index_in, container_type w_in) : index(index_in), w(w_in) { }

    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = typename std::conditional_t<is_const, const qvec3d, qvec3d>;
        using difference_type = ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

        iterator_base(const iterator_base &) = default;
        iterator_base &operator=(const iterator_base &) noexcept = default;

        [[nodiscard]] inline reference operator*() const noexcept
        {
            return (*w)[index];
        }

        constexpr iterator_base &operator++() noexcept
        {
            index++;
            return *this;
        }

        constexpr iterator_base &operator++(int) noexcept
        {
            index++;
            return *this;
        }

        constexpr iterator_base &operator--() noexcept
        {
            index--;
            return *this;
        }

        constexpr iterator_base operator--(int) noexcept
        {
            index--;
            return *this;
        }

        constexpr iterator_base &operator+=(const difference_type _Off) noexcept
        {
            index += _Off;
            return *this;
        }

        [[nodiscard]] constexpr iterator_base operator+(const difference_type _Off) const noexcept
        {
            iterator_base _Tmp = *this;
            _Tmp += _Off; // TRANSITION, LLVM-49342
            return _Tmp;
        }

        constexpr iterator_base &operator-=(const difference_type _Off) noexcept
        {
            index -= _Off;
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const iterator_base &_Off) const noexcept
        {
            return w == _Off.w && index == _Off.index;
        }

        [[nodiscard]] constexpr bool operator!=(const iterator_base &_Off) const noexcept
        {
            return !(*this == _Off);
        }

        [[nodiscard]] constexpr difference_type operator-(const iterator_base &_Off) const noexcept
        {
            return index - _Off.index;
        }

        [[nodiscard]] constexpr iterator_base operator-(const difference_type _Off) const noexcept
        {
            iterator_base _Tmp = *this;
            _Tmp -= _Off; // TRANSITION, LLVM-49342
            return _Tmp;
        }

        [[nodiscard]] inline reference operator[](const difference_type _Off) const noexcept
        {
            return *(*this + _Off);
        }
    };

    // default constructor does nothing
    inline winding_base_t() { }

    // construct winding with initial size; may allocate
    // memory, and sets size, but does not initialize any
    // of them.
    inline winding_base_t(const size_t &initial_size) : count(initial_size)
    {
        if (count > N) {
            vector.reserve(count);
            vector.resize(count - N);
        }
    }

    // construct winding from range.
    // iterators must have operator+ and operator-.
    template<typename Iter, std::enable_if_t<is_iterator_v<Iter>, int> = 0>
    inline winding_base_t(Iter begin, Iter end) : winding_base_t(end - begin)
    {
        // copy the array range
        std::copy_n(begin, min(count, N), array.begin());

        // copy the vector range, if required
        if (count > N) {
            std::copy(begin + N, end, vector.begin());
        }
    }

    // initializer list constructor
    inline winding_base_t(std::initializer_list<qvec3d> l) : winding_base_t(l.begin(), l.end()) {}

    // copy constructor; uses optimized method of copying
    // data over.
    inline winding_base_t(const winding_base_t &copy) : winding_base_t(copy.size())
    {
        // copy array range
        memcpy(&array.front(), &copy.array.front(), min(count, N) * sizeof(qvec3d));

        // copy vector range, if required
        if (count > N) {
            memcpy(&vector.front(), &copy.vector.front(), (count - N) * sizeof(qvec3d));
        }
    }

    // move constructor
    inline winding_base_t(winding_base_t &&move) noexcept : count(move.count)
    {
        count = move.count;

        // blit over array data
        memcpy(&array.front(), &move.array.front(), min(count, N) * sizeof(qvec3d));

        // move vector data, if available
        if (count > N) {
            vector = std::move(move.vector);
        }

        move.count = 0;
    }

    // assignment copy
    inline winding_base_t &operator=(const winding_base_t &copy)
    {
        count = copy.count;

        // copy array range
        memcpy(&array.front(), &copy.array.front(), min(count, N) * sizeof(qvec3d));

        // copy vector range, if required
        if (count > N) {
            vector.reserve(count);
            vector.resize(count - N);
            memcpy(&vector.front(), &copy.vector.front(), (count - N) * sizeof(qvec3d));
        }

        return *this;
    }

    // assignment move
    inline winding_base_t &operator=(winding_base_t &&move)
    {
        count = move.count;

        // blit over array data
        memcpy(&array.front(), &move.array.front(), min(count, N) * sizeof(qvec3d));

        // move vector data, if available
        if (count > N) {
            vector = std::move(move.vector);
        }

        move.count = 0;

        return *this;
    }

    inline bool empty() const { return count == 0; }

    inline explicit operator bool() const { return count != 0; }

    inline const size_t &size() const { return count; }

    inline qvec3d &at(const size_t &index)
    {
#ifdef _DEBUG
        if (index >= count)
            throw std::invalid_argument("index");
#endif

        if (index >= N) {
            return vector[index];
        }

        return array[index];
    }

    inline const qvec3d &at(const size_t &index) const
    {
#ifdef _DEBUG
        if (index >= count)
            throw std::invalid_argument("index");
#endif

        if (index >= N) {
            return vector[index];
        }

        return array[index];
    }

    // un-bounds-checked
    inline qvec3d &operator[](const size_t &index) { 
        if (index >= N) {
            return vector[index - N];
        }

        return array[index];
    }

    // un-bounds-checked
    inline const qvec3d &operator[](const size_t &index) const {
        if (index >= N) {
            return vector[index - N];
        }

        return array[index];
    }

    using const_iterator = iterator_base<true>;

    const const_iterator begin() const
    {
        return const_iterator(0, this);
    }

    const const_iterator end() const
    {
        return const_iterator(count, this);
    }

    using iterator = iterator_base<false>;

    iterator begin()
    {
        return iterator(0, this);
    }

    iterator end()
    {
        return iterator(count, this);
    }

    template<typename... Args>
    qvec3d &emplace_back(Args &&...vec)
    {
        count++;

        if (count > N) {
            return vector.emplace_back(std::forward<Args>(vec)...);
        }

        return (array[count - 1] = qvec3d(std::forward<Args>(vec)...));
    }

    void push_back(qvec3d &&vec) { emplace_back(std::move(vec)); }

    void push_back(const qvec3d &vec) { emplace_back(vec); }

    void resize(const size_t &new_size)
    {
        // resize vector if necessary
        if (new_size > N) {
            vector.resize(new_size - N);
        }

        count = new_size;
    }

    void clear()
    {
        count = 0;
    }

    vec_t area() const
    {
        // if (count < 3)
        //    throw std::domain_error("count");

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
        qvec3d center{};

        for (auto &point : *this)
            center += point;

        return center * (1.0 / count);
    }

    aabb3d bounds() const
    {
        aabb3d b;

        for (auto &point : *this)
            b += point;

        return b;
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

            if (qv::dot(v1, v2) < 1.0 - DIST_EPSILON)
                temp.push_back(at(i));
        }

        if (count != temp.count)
            *this = std::move(temp);
    }

    qplane3d plane() const
    {
        qvec3d v1 = at(0) - at(1);
        qvec3d v2 = at(2) - at(1);
        qvec3d normal = qv::normalize(qv::cross(v1, v2));

        return {normal, qv::dot(at(0), normal)};
    }

    static winding_base_t from_plane(const qplane3d &plane, const vec_t &worldextent)
    {
        /* find the major axis */
        vec_t max = -VECT_MAX;
        int32_t x = -1;
        for (size_t i = 0; i < 3; i++) {
            vec_t v = fabs(plane.normal[i]);

            if (v > max) {
                x = i;
                max = v;
            }
        }

        if (x == -1)
            FError("no axis found");

        qvec3d vup{};

        switch (x) {
            case 0:
            case 1: vup[2] = 1; break;
            case 2: vup[0] = 1; break;
        }

        vec_t v = qv::dot(vup, plane.normal);
        vup += plane.normal * -v;
        vup = qv::normalize(vup);

        qvec3d org = plane.normal * plane.dist;
        qvec3d vright = qv::cross(vup, plane.normal);

        vup *= worldextent;
        vright *= worldextent;

        /* project a really big axis aligned box onto the plane */
        winding_base_t w(4);

        w[0] = org - vright + vup;
        w[1] = org + vright + vup;
        w[2] = org + vright - vup;
        w[3] = org - vright - vup;

        return w;
    }

    void check(const vec_t &bogus_range = DEFAULT_BOGUS_RANGE, const vec_t &on_epsilon = DEFAULT_ON_EPSILON) const
    {
        if (count < 3)
            FError("{} points", count);

        vec_t a = area();
        if (a < 1)
            FError("{} area", a);

        qplane3d face = plane();

        for (size_t i = 0; i < count; i++) {
            const qvec3d &p1 = at(i);
            size_t j = 0;

            for (; j < 3; j++)
                if (p1[j] > bogus_range || p1[j] < -bogus_range)
                    FError("BOGUS_RANGE: {}", p1[j]);

            /* check the point is on the face plane */
            vec_t d = face.distance_to(p1);
            if (d < -on_epsilon || d > on_epsilon)
                FError("point off plane");

            /* check the edge isn't degenerate */
            const qvec3d &p2 = at((i + 1) % count);
            qvec3d dir = p2 - p1;

            if (qv::length(dir) < on_epsilon)
                FError("degenerate edge");

            qvec3d edgenormal = qv::normalize(qv::cross(face.normal, dir));
            vec_t edgedist = qv::dot(p1, edgenormal) + on_epsilon;

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

    std::vector<qvec3f> glm_winding_points() const { return {begin(), end()}; }

    static inline winding_base_t from_winding_points(const std::vector<qvec3f> &points)
    {
        return {points.begin(), points.end()};
    }

    winding_edges_t winding_edges() const
    {
        qplane3d p = plane();

        winding_edges_t result;
        result.reserve(count);

        for (size_t i = 0; i < count; i++) {
            const qvec3d &v0 = at(i);
            const qvec3d &v1 = at((i + 1) % count);

            qvec3d edgevec = qv::normalize(v1 - v0);
            qvec3d normal = qv::cross(edgevec, p.normal);

            result.emplace_back(normal, qv::dot(normal, v0));
        }

        return result;
    }

    // dists/sides can be null, or must have (size() + 1) reserved
    inline std::array<size_t, SIDE_TOTAL> calc_sides(
        const qplane3d &plane, vec_t *dists, side_t *sides, const vec_t &on_epsilon = DEFAULT_ON_EPSILON) const
    {
        std::array<size_t, SIDE_TOTAL> counts{};

        /* determine sides for each point */
        size_t i;

        for (i = 0; i < count; i++) {
            vec_t dot = plane.distance_to(at(i));

            if (dists) {
                dists[i] = dot;
            }

            side_t side;

            if (dot > on_epsilon)
                side = SIDE_FRONT;
            else if (dot < -on_epsilon)
                side = SIDE_BACK;
            else
                side = SIDE_ON;

            counts[side]++;

            if (sides) {
                sides[i] = side;
            }
        }

        if (sides) {
            sides[i] = sides[SIDE_FRONT];
        }
        if (dists) {
            dists[i] = dists[SIDE_FRONT];
        }

        return counts;
    }

    /*
    ==================
    ClipWinding

    Clips the winding to the plane, returning the new windings.
    If keepon is true, an exactly on-plane winding will be saved, otherwise
    it will be clipped away.
    ==================
    */
    std::array<std::optional<winding_base_t>, 2> clip(
        const qplane3d &plane, const vec_t &on_epsilon = DEFAULT_ON_EPSILON, const bool &keepon = false) const
    {
        vec_t *dists = (vec_t *)alloca(sizeof(vec_t) * (count + 1));
        side_t *sides = (side_t *)alloca(sizeof(side_t) * (count + 1));

        std::array<size_t, SIDE_TOTAL> counts = calc_sides(plane, dists, sides, on_epsilon);

        if (keepon && !counts[SIDE_FRONT] && !counts[SIDE_BACK])
            return {*this, std::nullopt};

        if (!counts[SIDE_FRONT])
            return {std::nullopt, *this};
        else if (!counts[SIDE_BACK])
            return {*this, std::nullopt};

        std::array<winding_base_t, 2> results{};

        for (size_t i = 0; i < count; i++) {
            const qvec3d &p1 = at(i);

            if (sides[i] == SIDE_ON) {
                results[SIDE_FRONT].push_back(p1);
                results[SIDE_BACK].push_back(p1);
                continue;
            } else if (sides[i] == SIDE_FRONT) {
                results[SIDE_FRONT].push_back(p1);
            } else if (sides[i] == SIDE_BACK) {
                results[SIDE_BACK].push_back(p1);
            }

            if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
                continue;

            /* generate a split point */
            const qvec3d &p2 = at((i + 1) % count);

            vec_t dot = dists[i] / (dists[i] - dists[i + 1]);
            qvec3d mid;

            for (size_t j = 0; j < 3; j++) { /* avoid round off error when possible */
                if (plane.normal[j] == 1)
                    mid[j] = plane.dist;
                else if (plane.normal[j] == -1)
                    mid[j] = -plane.dist;
                else
                    mid[j] = p1[j] + dot * (p2[j] - p1[j]);
            }

            results[SIDE_FRONT].push_back(mid);
            results[SIDE_BACK].push_back(mid);
        }

        if (results[SIDE_FRONT].count > MAX_POINTS_ON_WINDING || results[SIDE_BACK].count > MAX_POINTS_ON_WINDING)
            FError("MAX_POINTS_ON_WINDING");

        return {std::move(results[SIDE_FRONT]), std::move(results[SIDE_BACK])};
    }

    void dice(vec_t subdiv, std::function<void(winding_base_t &)> save_fn)
    {
        if (!count)
            return;

        aabb3d b = bounds();

        size_t i;

        for (i = 0; i < 3; i++)
            if (floor((b.mins()[i] + 1) / subdiv) < floor((b.maxs()[i] - 1) / subdiv))
                break;

        if (i == 3) {
            // no splitting needed
            save_fn(*this);
            return;
        }

        //
        // split the winding
        //
        qplane3d split{};
        split.normal[i] = 1;
        split.dist = subdiv * (1 + floor((b.mins()[i] + 1) / subdiv));
        auto clipped = clip(split);
        clear();

        //
        // create a new patch
        //
        for (auto &o : clipped)
            if (o.has_value())
                o->dice(subdiv, save_fn);
    }

    /**
     * Returns a winding for the face. Colinear vertices are filtered out.
     * Might return a degenerate polygon.
     */
    static winding_base_t from_face(const mbsp_t *bsp, const mface_t *f)
    {
        winding_base_t w(f->numedges);

        for (size_t i = 0; i < f->numedges; i++) {
            int32_t se = bsp->dsurfedges[f->firstedge + i];
            uint32_t v;

            if (se < 0)
                v = bsp->dedges[-se][1];
            else
                v = bsp->dedges[se][0];

            w[i] = bsp->dvertexes[v];
        }

        // CHECK: can we do the above + colinear checks
        // in a single pass?
        w.remove_colinear();

        return w;
    }

    winding_base_t flip() const
    {
        winding_base_t result(count);

        std::reverse_copy(begin(), end(), result.begin());

        return result;
    }

    winding_base_t translate(const qvec3d& offset) const
    {
        winding_base_t result(*this);

        for (qvec3d& p : result) {
            p += offset;
        }

        return result;
    }

    bool directional_equal(const winding_base_t& w,  const vec_t &equal_epsilon = POINT_EQUAL_EPSILON) const
    {
        if (this->size() != w.size()) {
            return false;
        }

        const auto this_size = size();

        // try different start offsets in `this`
        for (int i = 0; i < this_size; ++i) {
            bool all_equal = true;

            // index in `w` to compare
            for (int j = 0; j < this_size; ++j) {
                const qvec3d &our_point = (*this)[(i + j) % this_size];
                const qvec3d &their_point = w[j];

                if (!qv::epsilonEqual(our_point, their_point, equal_epsilon)) {
                    all_equal = false;
                    break;
                }
            }

            if (all_equal) {
                return true;
            }
        }

        return false;
    }

    bool undirectional_equal(const winding_base_t& w,  const vec_t &equal_epsilon = POINT_EQUAL_EPSILON) const
    {
        return directional_equal(w, equal_epsilon)
            || directional_equal(w.flip(), equal_epsilon);
    }
};

// the default amount of points to keep on stack
constexpr size_t STACK_POINTS_ON_WINDING = MAX_POINTS_ON_WINDING / 4;

using winding_t = winding_base_t<STACK_POINTS_ON_WINDING>;

}; // namespace polylib
