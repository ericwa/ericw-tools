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
// points, and moves to a dynamic array after that.
template<size_t N>
struct winding_base_t
{
private:
    using array_type = std::array<qvec3d, N>;
    using vector_type = std::vector<qvec3d>;
    using variant_type = std::variant<array_type, vector_type>;
    size_t count = 0;
    bool isVector = false;
    array_type array;
    vector_type vector;

public:
    template<typename array_iterator, typename vector_iterator>
    class iterator_base
    {
        std::variant<array_iterator, vector_iterator> it;

    public:
        using iterator_category = typename vector_iterator::iterator_category;
        using value_type = typename vector_iterator::value_type;
        using difference_type = typename vector_iterator::difference_type;
        using pointer = typename vector_iterator::pointer;
        using reference = typename vector_iterator::reference;

        iterator_base(array_iterator it) : it(it) { }

        iterator_base(vector_iterator it) : it(it) { }

        [[nodiscard]] constexpr reference operator*() const noexcept
        {
            if (std::holds_alternative<array_iterator>(it))
                return *std::get<array_iterator>(it);
            return *std::get<vector_iterator>(it);
        }

        constexpr iterator_base &operator=(const iterator_base &) noexcept = default;

        constexpr iterator_base &operator++() noexcept
        {
            if (std::holds_alternative<array_iterator>(it))
                ++std::get<array_iterator>(it);
            else
                ++std::get<vector_iterator>(it);

            return *this;
        }

        constexpr iterator_base &operator++(int) noexcept
        {
            if (std::holds_alternative<array_iterator>(it))
                std::get<array_iterator>(it)++;
            else
                std::get<vector_iterator>(it)++;

            return *this;
        }

        constexpr iterator_base &operator--() noexcept
        {
            if (std::holds_alternative<array_iterator>(it))
                --std::get<array_iterator>(it);
            else
                --std::get<vector_iterator>(it);

            return *this;
        }

        constexpr iterator_base operator--(int) noexcept
        {
            if (std::holds_alternative<array_iterator>(it))
                std::get<array_iterator>(it)--;
            else
                std::get<vector_iterator>(it)--;

            return *this;
        }

        constexpr iterator_base &operator+=(const difference_type _Off) noexcept
        {
            if (std::holds_alternative<array_iterator>(it))
                std::get<array_iterator>(it) += _Off;
            else
                std::get<vector_iterator>(it) += _Off;

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
            if (std::holds_alternative<array_iterator>(it))
                std::get<array_iterator>(it) -= _Off;
            else
                std::get<vector_iterator>(it) -= _Off;

            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const iterator_base &_Off) const noexcept
        {
            if (std::holds_alternative<array_iterator>(it)) {
                auto sit = std::get<array_iterator>(it);

                Q_assert(std::holds_alternative<array_iterator>(_Off.it));

                return sit == std::get<array_iterator>(_Off.it);
            } else {
                auto sit = std::get<vector_iterator>(it);

                Q_assert(std::holds_alternative<vector_iterator>(_Off.it));

                return sit == std::get<vector_iterator>(_Off.it);
            }
        }

        [[nodiscard]] constexpr bool operator!=(const iterator_base &_Off) const noexcept
        {
            if (std::holds_alternative<array_iterator>(it)) {
                auto sit = std::get<array_iterator>(it);

                Q_assert(std::holds_alternative<array_iterator>(_Off.it));

                return sit != std::get<array_iterator>(_Off.it);
            } else {
                auto sit = std::get<vector_iterator>(it);

                Q_assert(std::holds_alternative<vector_iterator>(_Off.it));

                return sit != std::get<vector_iterator>(_Off.it);
            }
        }

        [[nodiscard]] constexpr difference_type operator-(const iterator_base &_Off) const noexcept
        {
            if (std::holds_alternative<array_iterator>(it)) {
                auto sit = std::get<array_iterator>(it);

                Q_assert(std::holds_alternative<array_iterator>(_Off.it));

                return sit - std::get<array_iterator>(_Off.it);
            } else {
                auto sit = std::get<vector_iterator>(it);

                Q_assert(std::holds_alternative<vector_iterator>(_Off.it));

                return sit - std::get<vector_iterator>(_Off.it);
            }
        }

        [[nodiscard]] constexpr iterator_base operator-(const difference_type _Off) const noexcept
        {
            iterator_base _Tmp = *this;
            _Tmp -= _Off; // TRANSITION, LLVM-49342
            return _Tmp;
        }

        [[nodiscard]] constexpr reference operator[](const difference_type _Off) const noexcept
        {
            return *(*this + _Off);
        }
    };

    // default constructor does nothing
    inline winding_base_t() { }

    // construct winding with initial size; may allocate
    // memory, and sets size, but does not initialize any
    // of them.
    inline winding_base_t(const size_t &initial_size) : count(initial_size), isVector(count > N)
    {
        if (isVector) {
            vector.reserve(count);
        }
    }

    // construct winding from range.
    // iterators must have operator-.
    template<typename Iter, std::enable_if_t<is_iterator_v<Iter>, int> = 0>
    inline winding_base_t(Iter begin, Iter end) : count(end - begin), isVector(count > N)
    {
        if (isVector) {
            vector = std::move(vector_type(begin, end));
        } else {
            std::copy(begin, end, array.begin());
        }
    }

    // copy constructor
    inline winding_base_t(const winding_base_t &copy) : winding_base_t(copy.begin(), copy.end()) { }

    // move constructor
    inline winding_base_t(winding_base_t &&move) noexcept : count(move.count)
    {
        count = move.count;
        isVector = move.isVector;

        if (move.isVector) {
            vector = std::move(move.vector);
            move.isVector = false;
        } else {
            std::copy(move.begin(), move.begin() + move.count, array.begin());
        }
        move.count = 0;
    }

    // assignment copy
    inline winding_base_t &operator=(const winding_base_t &copy)
    {
        count = copy.count;
        isVector = copy.isVector;

        if (isVector) {
            vector = copy.vector;
        } else {
            std::copy(copy.begin(), copy.begin() + copy.count, array.begin());
        }

        return *this;
    }

    // assignment move
    inline winding_base_t &operator=(winding_base_t &&move)
    {
        count = move.count;
        isVector = move.isVector;

        if (move.isVector) {
            vector = std::move(move.vector);
            move.isVector = false;
        } else {
            std::copy(move.begin(), move.begin() + move.count, array.begin());
        }
        move.count = 0;

        return *this;
    }

    inline const size_t &size() const { return count; }

    inline qvec3d &at(const size_t &index)
    {
#ifdef _DEBUG
        if (index >= count)
            throw std::invalid_argument("index");
#endif

        if (isVector)
            return vector[index];
        return array[index];
    }

    inline const qvec3d &at(const size_t &index) const
    {
#ifdef _DEBUG
        if (index >= count)
            throw std::invalid_argument("index");
#endif

        if (isVector)
            return vector[index];
        return array[index];
    }

    inline qvec3d &operator[](const size_t &index) { return at(index); }

    inline const qvec3d &operator[](const size_t &index) const { return at(index); }

    using const_iterator = iterator_base<typename array_type::const_iterator, vector_type::const_iterator>;

    const const_iterator begin() const
    {
        if (isVector)
            return const_iterator(vector.begin());
        return const_iterator(array.begin());
    }

    const const_iterator end() const
    {
        if (isVector)
            return const_iterator(vector.end());
        return const_iterator(array.begin() + count);
    }

    using iterator = iterator_base<typename array_type::iterator, vector_type::iterator>;

    iterator begin()
    {
        if (isVector)
            return iterator(vector.begin());
        return iterator(array.begin());
    }

    iterator end()
    {
        if (isVector)
            return iterator(vector.end());
        return iterator(array.begin() + count);
    }

    template<typename... Args>
    qvec3d &emplace_back(Args &&...vec)
    {
        // move us to dynamic
        if (count == N) {
            vector = std::move(vector_type(begin(), end()));
            isVector = true;
        }

        count++;

        if (isVector) {
            return vector.emplace_back(std::forward<Args>(vec)...);
        }

        return (array[count - 1] = qvec3d(std::forward<Args>(vec)...));
    }

    void push_back(qvec3d &&vec) { emplace_back(std::move(vec)); }

    void push_back(const qvec3d &vec) { emplace_back(vec); }

    void resize(const size_t &new_size)
    {
        // move us to dynamic if we'll expand too big
        if (new_size > N && !isVector) {
            vector.resize(new_size);
            std::copy(begin(), end(), vector.begin());
        } else if (isVector) {
            if (new_size > N) {
                vector.resize(new_size);
                // move us to array if we're shrinking
            } else {
                std::copy_n(vector.begin(), new_size, array.begin());
            }
        }

        count = new_size;
    }

    void clear()
    {
        if (isVector) {
            vector.clear();
            isVector = false;
        }

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

    using save_fn_t = void (*)(winding_base_t &w, void *userinfo);

    void dice(vec_t subdiv, save_fn_t save_fn, void *userinfo)
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
            save_fn(*this, userinfo);
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
                o->dice(subdiv, save_fn, userinfo);
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
};

// the default amount of points to keep on stack
constexpr size_t STACK_POINTS_ON_WINDING = MAX_POINTS_ON_WINDING / 4;

using winding_t = winding_base_t<STACK_POINTS_ON_WINDING>;

}; // namespace polylib
