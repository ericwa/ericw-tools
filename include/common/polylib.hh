/* common/polylib.h */

#pragma once

#include <common/mathlib.hh>

#include <common/log.hh>
#include <common/bspfile.hh>
#include <common/cmdlib.hh>
#include <common/aabb.hh>
#include <variant>
#include <array>
#include <vector>

#include <type_traits>
#include <stdexcept>
#include <optional>

#include <fmt/ostream.h>

#include <tbb/scalable_allocator.h>

namespace polylib
{

constexpr size_t MAX_POINTS_ON_WINDING = 96;

constexpr double DEFAULT_BOGUS_RANGE = 65536.0;

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

// Stack storage; uses stack allocation. Throws if it can't insert
// a new member.
template<class T, size_t N>
struct winding_storage_stack_t
{
public:
    using float_type = T;
    using vec3_type = qvec<T, 3>;

protected:
    using array_type = std::array<vec3_type, N>;
    array_type array;

public:
    size_t count = 0;

    // default constructor does nothing
    inline winding_storage_stack_t() { }

    // construct winding with initial size; may allocate
    // memory, and sets size, but does not initialize any
    // of them.
    inline winding_storage_stack_t(size_t initial_size)
        : count(initial_size)
    {
        if (initial_size > N) {
            throw std::bad_alloc();
        }
    }

    // construct winding from range.
    // iterators must have operator+ and operator-.
    template<typename Iter, std::enable_if_t<is_iterator_v<Iter>, int> = 0>
    inline winding_storage_stack_t(Iter begin, Iter end)
        : winding_storage_stack_t(end - begin)
    {
        std::copy_n(begin, count, array.begin());
    }

    // copy constructor; uses optimized method of copying
    // data over.
    inline winding_storage_stack_t(const winding_storage_stack_t &copy)
        : winding_storage_stack_t(copy.size())
    {
        std::copy_n(copy.array.begin(), copy.count, array.begin());
    }

    // move constructor
    inline winding_storage_stack_t(winding_storage_stack_t &&move) noexcept
        : count(move.count)
    {
        count = move.count;

        std::copy_n(move.array.begin(), move.count, array.begin());

        move.count = 0;
    }

    // assignment copy
    inline winding_storage_stack_t &operator=(const winding_storage_stack_t &copy)
    {
        count = copy.count;

        // copy array range
        std::copy_n(copy.array.begin(), copy.count, array.begin());

        return *this;
    }

    // assignment move
    inline winding_storage_stack_t &operator=(winding_storage_stack_t &&move) noexcept
    {
        count = move.count;

        // blit over array data
        std::copy_n(move.array.begin(), move.count, array.begin());

        move.count = 0;

        return *this;
    }

    inline size_t size() const { return count; }

    inline vec3_type &at(size_t index)
    {
#ifdef _DEBUG
        if (index >= count)
            throw std::invalid_argument("index");
#endif

        return array[index];
    }

    inline const vec3_type &at(size_t index) const
    {
#ifdef _DEBUG
        if (index >= count)
            throw std::invalid_argument("index");
#endif

        return array[index];
    }

    // un-bounds-checked
    inline vec3_type &operator[](size_t index) { return array[index]; }

    // un-bounds-checked
    inline const vec3_type &operator[](size_t index) const { return array[index]; }

    using const_iterator = typename array_type::const_iterator;

    inline const const_iterator begin() const { return array.begin(); }

    inline const const_iterator end() const { return array.begin() + count; }

    using iterator = typename array_type::iterator;

    inline iterator begin() { return array.begin(); }

    inline iterator end() { return array.begin() + count; }

    inline vec3_type &emplace_back(const vec3_type &vec)
    {
        count++;

        if (count > N) {
            throw std::bad_alloc();
        }

        return (array[count - 1] = vec);
    }

    inline void resize(size_t new_size)
    {
        if (new_size > N) {
            throw std::bad_alloc();
        }

        count = new_size;
    }

    inline void reserve(size_t size) { }

    inline void clear() { count = 0; }
};

// Heap storage; uses a vector.
template<class T>
struct winding_storage_heap_t
{
public:
    using float_type = T;
    using vec3_type = qvec<T, 3>;

protected:
    std::vector<vec3_type, tbb::scalable_allocator<vec3_type>> values{};

public:
    // default constructor does nothing
    inline winding_storage_heap_t() { }

    // construct winding with initial size; may allocate
    // memory, and sets size, but does not initialize any
    // of them.
    inline winding_storage_heap_t(size_t initial_size)
        : values(initial_size)
    {
    }

    // construct winding from range.
    // iterators must have operator+ and operator-.
    template<typename Iter, std::enable_if_t<is_iterator_v<Iter>, int> = 0>
    inline winding_storage_heap_t(Iter begin, Iter end)
        : values(begin, end)
    {
    }

    // copy constructor; uses optimized method of copying
    // data over.
    inline winding_storage_heap_t(const winding_storage_heap_t &copy)
        : values(copy.values)
    {
    }

    // move constructor
    inline winding_storage_heap_t(winding_storage_heap_t &&move) noexcept
        : values(std::move(move.values))
    {
    }

    // assignment copy
    inline winding_storage_heap_t &operator=(const winding_storage_heap_t &copy)
    {
        resize(copy.size());

        // copy array range
        std::copy_n(copy.values.data(), copy.size(), values.data());

        return *this;
    }

    // assignment move
    inline winding_storage_heap_t &operator=(winding_storage_heap_t &&move) noexcept
    {
        // take ownership of heap pointer
        values = std::move(move.values);

        return *this;
    }

    inline size_t size() const { return values.size(); }

    inline vec3_type &at(size_t index) { return values[index]; }

    inline const vec3_type &at(size_t index) const { return values[index]; }

    // un-bounds-checked
    inline vec3_type &operator[](size_t index) { return values[index]; }

    // un-bounds-checked
    inline const vec3_type &operator[](size_t index) const { return values[index]; }

    inline const auto begin() const { return values.begin(); }

    inline const auto end() const { return values.end(); }

    inline auto begin() { return values.begin(); }

    inline auto end() { return values.end(); }

    inline vec3_type &emplace_back(const vec3_type &vec) { return values.emplace_back(vec); }

    inline void resize(size_t new_size) { values.resize(new_size); }

    inline void reserve(size_t size) { values.reserve(size); }

    inline void clear() { values.clear(); }
};

// Hybrid storage; uses stack allocation for the first N
// points, and uses a dynamic vector for storage after that.
template<class T, size_t N>
struct winding_storage_hybrid_t
{
public:
    using float_type = T;
    using vec3_type = qvec<T, 3>;

protected:
    using array_type = std::array<vec3_type, N>;
    using vector_type = std::vector<vec3_type>;
    using variant_type = std::variant<array_type, vector_type>;
    array_type array;
    vector_type vector;

public:
    size_t count = 0;

    template<bool is_const>
    class iterator_base
    {
        friend struct winding_storage_hybrid_t;

        using container_type =
            typename std::conditional_t<is_const, const winding_storage_hybrid_t *, winding_storage_hybrid_t *>;
        size_t index;
        container_type w;

        iterator_base(size_t index_in, container_type w_in)
            : index(index_in),
              w(w_in)
        {
        }

    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = typename std::conditional_t<is_const, const vec3_type, vec3_type>;
        using difference_type = ptrdiff_t;
        using pointer = value_type *;
        using reference = value_type &;

        iterator_base(const iterator_base &) = default;
        iterator_base &operator=(const iterator_base &) noexcept = default;

        [[nodiscard]] inline reference operator*() const noexcept { return (*w)[index]; }

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

        [[nodiscard]] constexpr bool operator!=(const iterator_base &_Off) const noexcept { return !(*this == _Off); }

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

        [[nodiscard]] inline reference operator[](const difference_type _Off) const noexcept { return *(*this + _Off); }
    };

    // default constructor does nothing
    inline winding_storage_hybrid_t() { }

    // construct winding with initial size; may allocate
    // memory, and sets size, but does not initialize any
    // of them.
    inline winding_storage_hybrid_t(size_t initial_size)
        : count(initial_size)
    {
        if (count > N) {
            vector.reserve(count);
            vector.resize(count - N);
        }
    }

    // construct winding from range.
    // iterators must have operator+ and operator-.
    template<typename Iter, std::enable_if_t<is_iterator_v<Iter>, int> = 0>
    inline winding_storage_hybrid_t(Iter begin, Iter end)
        : winding_storage_hybrid_t(end - begin)
    {
        // copy the array range
        std::copy_n(begin, std::min(count, N), array.begin());

        // copy the vector range, if required
        if (count > N) {
            std::copy(begin + N, end, vector.begin());
        }
    }

    // copy constructor; uses optimized method of copying
    // data over.
    inline winding_storage_hybrid_t(const winding_storage_hybrid_t &copy)
        : winding_storage_hybrid_t(copy.size())
    {
        // copy array range
        memcpy(&array.front(), &copy.array.front(), std::min(count, N) * sizeof(vec3_type));

        // copy vector range, if required
        if (count > N) {
            memcpy(&vector.front(), &copy.vector.front(), (count - N) * sizeof(vec3_type));
        }
    }

    // move constructor
    inline winding_storage_hybrid_t(winding_storage_hybrid_t &&move) noexcept
        : count(move.count)
    {
        count = move.count;

        // blit over array data
        memcpy(&array.front(), &move.array.front(), std::min(count, N) * sizeof(vec3_type));

        // move vector data, if available
        if (count > N) {
            vector = std::move(move.vector);
        }

        move.count = 0;
    }

    // assignment copy
    inline winding_storage_hybrid_t &operator=(const winding_storage_hybrid_t &copy)
    {
        count = copy.count;

        // copy array range
        memcpy(&array.front(), &copy.array.front(), std::min(count, N) * sizeof(vec3_type));

        // copy vector range, if required
        if (count > N) {
            vector.reserve(count);
            vector.resize(count - N);
            memcpy(&vector.front(), &copy.vector.front(), (count - N) * sizeof(vec3_type));
        }

        return *this;
    }

    // assignment move
    inline winding_storage_hybrid_t &operator=(winding_storage_hybrid_t &&move) noexcept
    {
        count = move.count;

        // blit over array data
        memcpy(&array.front(), &move.array.front(), std::min(count, N) * sizeof(vec3_type));

        // move vector data, if available
        if (count > N) {
            vector = std::move(move.vector);
        }

        move.count = 0;

        return *this;
    }

    inline size_t size() const { return count; }

    inline size_t vector_size() const { return vector.size(); }

    inline vec3_type &at(size_t index)
    {
#ifdef _DEBUG
        if (index >= count)
            throw std::invalid_argument("index");
#endif

        if (index >= N) {
            return vector[index - N];
        }

        return array[index];
    }

    inline const vec3_type &at(size_t index) const
    {
#ifdef _DEBUG
        if (index >= count)
            throw std::invalid_argument("index");
#endif

        if (index >= N) {
            return vector[index - N];
        }

        return array[index];
    }

    // un-bounds-checked
    inline vec3_type &operator[](size_t index)
    {
        if (index >= N) {
            return vector[index - N];
        }

        return array[index];
    }

    // un-bounds-checked
    inline const vec3_type &operator[](size_t index) const
    {
        if (index >= N) {
            return vector[index - N];
        }

        return array[index];
    }

    using const_iterator = iterator_base<true>;

    inline const const_iterator begin() const { return const_iterator(0, this); }

    inline const const_iterator end() const { return const_iterator(count, this); }

    using iterator = iterator_base<false>;

    inline iterator begin() { return iterator(0, this); }

    inline iterator end() { return iterator(count, this); }

    inline vec3_type &emplace_back(const vec3_type &vec)
    {
        count++;

        if (count > N) {
            return vector.emplace_back(vec);
        }

        return (array[count - 1] = vec);
    }

    inline void resize(size_t new_size)
    {
        // resize vector if necessary
        if (new_size > N) {
            vector.resize(new_size - N);
        }

        count = new_size;
    }

    inline void reserve(size_t size) { }

    inline void clear() { count = 0; }
};

// Winding type, with storage template. Doesn't inherit the storage,
// since that might slow things down with virtual destructor.
template<typename TStorage>
struct winding_base_t
{
public:
    using float_type = typename TStorage::float_type;
    using vec3_type = typename TStorage::vec3_type;

protected:
    TStorage storage;

public:
    // default constructor does nothing
    inline winding_base_t() { }

    // construct winding with initial size; may allocate
    // memory, and sets size, but does not initialize any
    // of them.
    inline winding_base_t(size_t initial_size)
        : storage(initial_size)
    {
    }

    // construct winding from range.
    // iterators must have operator+ and operator-.
    template<typename Iter, std::enable_if_t<is_iterator_v<Iter>, int> = 0>
    inline winding_base_t(Iter begin, Iter end)
        : storage(begin, end)
    {
    }

    // initializer list constructor
    inline winding_base_t(std::initializer_list<vec3_type> l)
        : storage(l.begin(), l.end())
    {
    }

    // copy constructor; we require copying to be done with clone() to avoid performance bugs
    inline winding_base_t(const winding_base_t &copy) = delete;

    // move constructor
    inline winding_base_t(winding_base_t &&move) noexcept
        : storage(std::move(move.storage))
    {
    }

    // assignment copy
    inline winding_base_t &operator=(const winding_base_t &copy) = delete;

    // assignment move
    inline winding_base_t &operator=(winding_base_t &&move) noexcept
    {
        storage = std::move(move.storage);
        return *this;
    }

    inline bool empty() const { return size() == 0; }

    inline explicit operator bool() const { return !empty(); }

    inline size_t size() const { return storage.size(); }

    inline vec3_type &at(size_t index) { return storage.at(index); }

    inline const vec3_type &at(size_t index) const { return storage.at(index); }

    // un-bounds-checked
    inline vec3_type &operator[](size_t index) { return storage[index]; }

    // un-bounds-checked
    inline const vec3_type &operator[](size_t index) const { return storage[index]; }

    inline const auto begin() const { return storage.begin(); }

    inline const auto end() const { return storage.end(); }

    inline auto begin() { return storage.begin(); }

    inline auto end() { return storage.end(); }

    template<typename... Args>
    inline vec3_type &emplace_back(Args &&...vec)
    {
        return storage.emplace_back(vec3_type(std::forward<Args>(vec)...));
    }

    inline void push_back(const vec3_type &vec) { storage.emplace_back(vec); }

    inline void resize(size_t new_size) { storage.resize(new_size); }

    inline void reserve(size_t size) { storage.reserve(size); }

    inline void clear() { storage.clear(); }

    template<typename TStor>
    friend struct winding_base_t;

    // explicit copying function
    template<typename TStor = TStorage>
    winding_base_t<TStor> clone() const
    {
        winding_base_t<TStor> result;
        if constexpr (std::is_same_v<TStor, TStorage>) {
            result.storage = storage;
        } else {
            result.storage = TStor{begin(), end()};
        }
        return result;
    }

    // non-storage functions

    float_type area() const
    {
        float_type total = 0;

        for (size_t i = 2; i < size(); i++) {
            vec3_type d1 = at(i - 1) - at(0);
            vec3_type d2 = at(i) - at(0);
            total += 0.5 * qv::length(qv::cross(d1, d2));
        }

        return total;
    }

    vec3_type center() const
    {
        vec3_type center{};

        for (auto &point : *this)
            center += point;

        return center * (1.0 / size());
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
        for (size_t i = 0; i < size(); i++) {
            size_t j = (i + 1) % size();
            size_t k = (i + size() - 1) % size();
            vec3_type v1 = qv::normalize(at(j) - at(i));
            vec3_type v2 = qv::normalize(at(i) - at(k));

            if (qv::dot(v1, v2) < 1.0 - DIST_EPSILON)
                temp.push_back(at(i));
        }

        if (size() != temp.size()) {
            *this = std::move(temp);
        }
    }

    qplane3d plane() const
    {
        vec3_type v1 = at(0) - at(1);
        vec3_type v2 = at(2) - at(1);
        vec3_type normal = qv::normalize(qv::cross(v1, v2));

        return {normal, qv::dot(at(0), normal)};
    }

    template<typename TPlane>
    static winding_base_t from_plane(const qplane3<TPlane> &plane, float_type worldextent)
    {
        /* find the major axis */
        float_type max = -std::numeric_limits<float_type>::max();
        int32_t x = -1;
        for (size_t i = 0; i < 3; i++) {
            float_type v = std::abs(plane.normal[i]);

            if (v > max) {
                x = i;
                max = v;
            }
        }

        if (x == -1 || max == -VECT_MAX) {
            FError("no axis found");
        }

        vec3_type vup{};

        switch (x) {
            case 0:
            case 1: vup[2] = 1; break;
            case 2: vup[0] = 1; break;
        }

        float_type v = qv::dot(vup, plane.normal);
        vup += plane.normal * -v;
        vup = qv::normalize(vup);

        vec3_type org = plane.normal * plane.dist;
        vec3_type vright = qv::cross(vup, plane.normal);

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

    void check(float_type bogus_range = DEFAULT_BOGUS_RANGE, float_type on_epsilon = DEFAULT_ON_EPSILON) const
    {
        if (size() < 3)
            FError("{} points", size());

        float_type a = area();
        if (a < 1)
            FError("{} area", a);

        qplane3d face = plane();

        for (size_t i = 0; i < size(); i++) {
            const vec3_type &p1 = at(i);
            size_t j = 0;

            for (; j < 3; j++)
                if (p1[j] > bogus_range || p1[j] < -bogus_range)
                    FError("BOGUS_RANGE: {}", p1[j]);

            /* check the point is on the face plane */
            float_type d = face.distance_to(p1);
            if (d < -on_epsilon || d > on_epsilon)
                FError("point off plane");

            /* check the edge isn't degenerate */
            const vec3_type &p2 = at((i + 1) % size());
            vec3_type dir = p2 - p1;

            if (qv::length(dir) < on_epsilon)
                FError("degenerate edge");

            vec3_type edgenormal = qv::normalize(qv::cross(face.normal, dir));
            float_type edgedist = qv::dot(p1, edgenormal) + on_epsilon;

            /* all other points must be on front side */
            for (size_t j = 0; j < size(); j++) {
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
        result.reserve(size());

        for (size_t i = 0; i < size(); i++) {
            const vec3_type &v0 = at(i);
            const vec3_type &v1 = at((i + 1) % size());

            vec3_type edgevec = qv::normalize(v1 - v0);
            vec3_type normal = qv::cross(edgevec, p.normal);

            result.emplace_back(normal, qv::dot(normal, v0));
        }

        return result;
    }

    // dists/sides can be null, or must have (size() + 1) reserved
    template<typename TPlane>
    inline std::array<size_t, SIDE_TOTAL> calc_sides(const qplane3<TPlane> &plane, float_type *dists,
        planeside_t *sides, float_type on_epsilon = DEFAULT_ON_EPSILON) const
    {
        std::array<size_t, SIDE_TOTAL> counts{};

        /* determine sides for each point */
        size_t i;

        for (i = 0; i < size(); i++) {
            float_type dot = plane.distance_to(at(i));

            if (dists) {
                dists[i] = dot;
            }

            planeside_t side;

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

    float_type max_dist_off_plane(const qplane3d &plane)
    {
        float_type max_dist = 0.0;
        for (size_t i = 0; i < size(); i++) {
            float_type dist = std::abs(plane.distance_to(at(i)));
            if (dist > max_dist) {
                max_dist = dist;
            }
        }
        return max_dist;
    }

    /*
    ==================
    ClipWinding

    Clips the winding to the plane, returning the new windings.
    If keepon is true, an exactly on-plane winding will be saved, otherwise
    it will be clipped away.
    ==================
    */
    template<typename TStor = TStorage>
    twosided<std::optional<winding_base_t<TStor>>> clip(
        const qplane3d &plane, float_type on_epsilon = DEFAULT_ON_EPSILON, bool keepon = false) const
    {
        float_type *dists = (float_type *)alloca(sizeof(float_type) * (size() + 1));
        planeside_t *sides = (planeside_t *)alloca(sizeof(planeside_t) * (size() + 1));

        std::array<size_t, SIDE_TOTAL> counts = calc_sides(plane, dists, sides, on_epsilon);

        if (keepon && !counts[SIDE_FRONT] && !counts[SIDE_BACK])
            return {this->clone<TStor>(), std::nullopt};

        if (!counts[SIDE_FRONT])
            return {std::nullopt, this->clone<TStor>()};
        else if (!counts[SIDE_BACK])
            return {this->clone<TStor>(), std::nullopt};

        twosided<winding_base_t<TStor>> results{};

        for (auto &w : results) {
            w.reserve(size() + 4);
        }

        for (size_t i = 0; i < size(); i++) {
            const vec3_type &p1 = at(i);

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
            const vec3_type &p2 = at((i + 1) % size());

            float_type dot = dists[i] / (dists[i] - dists[i + 1]);
            vec3_type mid;

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

        return {std::move(results[SIDE_FRONT]), std::move(results[SIDE_BACK])};
    }

    /*
    ==================
    clip_front

    The same as the above, except it will avoid allocating the front
    if it doesn't need to be modified; destroys *this if it does end up
    allocating a new winding.

    Cheaper than clip(...)[SIDE_FRONT]
    ==================
    */
    template<typename TPlane>
    std::optional<winding_base_t> clip_front(
        const qplane3<TPlane> &plane, float_type on_epsilon = DEFAULT_ON_EPSILON, bool keepon = false)
    {
        float_type *dists = (float_type *)alloca(sizeof(float_type) * (size() + 1));
        planeside_t *sides = (planeside_t *)alloca(sizeof(planeside_t) * (size() + 1));

        std::array<size_t, SIDE_TOTAL> counts = calc_sides(plane, dists, sides, on_epsilon);

        if (keepon && !counts[SIDE_FRONT] && !counts[SIDE_BACK])
            return std::move(*this);

        if (!counts[SIDE_FRONT])
            return std::nullopt;
        else if (!counts[SIDE_BACK])
            return std::move(*this);

        winding_base_t result;
        result.reserve(size() + 4);

        for (size_t i = 0; i < size(); i++) {
            const vec3_type &p1 = at(i);

            if (sides[i] == SIDE_ON) {
                result.push_back(p1);
                continue;
            } else if (sides[i] == SIDE_FRONT) {
                result.push_back(p1);
            }

            if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
                continue;

            /* generate a split point */
            const vec3_type &p2 = at((i + 1) % size());

            float_type dot = dists[i] / (dists[i] - dists[i + 1]);
            vec3_type mid;

            for (size_t j = 0; j < 3; j++) { /* avoid round off error when possible */
                if (plane.normal[j] == 1)
                    mid[j] = plane.dist;
                else if (plane.normal[j] == -1)
                    mid[j] = -plane.dist;
                else
                    mid[j] = p1[j] + dot * (p2[j] - p1[j]);
            }

            result.push_back(mid);
        }

        return result;
    }

    /*
    ==================
    clip_back

    The same as the above, except it will avoid allocating the front
    if it doesn't need to be modified; destroys *this if it does end up
    allocating a new winding.

    Cheaper than clip(...)[SIDE_BACK]
    ==================
    */
    std::optional<winding_base_t> clip_back(
        const qplane3d &plane, float_type on_epsilon = DEFAULT_ON_EPSILON, bool keepon = false)
    {
        float_type *dists = (float_type *)alloca(sizeof(float_type) * (size() + 1));
        planeside_t *sides = (planeside_t *)alloca(sizeof(planeside_t) * (size() + 1));

        std::array<size_t, SIDE_TOTAL> counts = calc_sides(plane, dists, sides, on_epsilon);

        if (keepon && !counts[SIDE_FRONT] && !counts[SIDE_BACK])
            return std::nullopt;

        if (!counts[SIDE_FRONT])
            return std::move(*this);
        else if (!counts[SIDE_BACK])
            return std::nullopt;

        winding_base_t result;
        result.reserve(size() + 4);

        for (size_t i = 0; i < size(); i++) {
            const vec3_type &p1 = at(i);

            if (sides[i] == SIDE_ON) {
                result.push_back(p1);
                continue;
            } else if (sides[i] == SIDE_BACK) {
                result.push_back(p1);
            }

            if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
                continue;

            /* generate a split point */
            const vec3_type &p2 = at((i + 1) % size());

            float_type dot = dists[i] / (dists[i] - dists[i + 1]);
            vec3_type mid;

            for (size_t j = 0; j < 3; j++) { /* avoid round off error when possible */
                if (plane.normal[j] == 1)
                    mid[j] = plane.dist;
                else if (plane.normal[j] == -1)
                    mid[j] = -plane.dist;
                else
                    mid[j] = p1[j] + dot * (p2[j] - p1[j]);
            }

            result.push_back(mid);
        }

        return std::move(result);
    }

    // SaveFn is a callable of type `winding_base_t & -> void`
    template<typename SaveFn>
    void dice(float_type subdiv, SaveFn &&save_fn)
    {
        if (!size())
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
        winding_base_t result(size());

        std::reverse_copy(begin(), end(), result.begin());

        return result;
    }

    winding_base_t translate(const vec3_type &offset) const
    {
        winding_base_t result = this->clone();

        for (vec3_type &p : result) {
            p += offset;
        }

        return result;
    }

    bool directional_equal(const winding_base_t &w, float_type equal_epsilon = POINT_EQUAL_EPSILON) const
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
                const vec3_type &our_point = (*this)[(i + j) % this_size];
                const vec3_type &their_point = w[j];

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

    bool undirectional_equal(const winding_base_t &w, float_type equal_epsilon = POINT_EQUAL_EPSILON) const
    {
        return directional_equal(w, equal_epsilon) || directional_equal(w.flip(), equal_epsilon);
    }

    static std::array<winding_base_t, 6> aabb_windings(const aabb3d &bbox)
    {
        double worldextent = 0;
        for (int i = 0; i < 3; ++i) {
            worldextent = std::max(worldextent, std::abs(bbox.maxs()[i]));
            worldextent = std::max(worldextent, std::abs(bbox.mins()[i]));
        }
        worldextent += 1;

        std::array<winding_base_t, 6> result;
        auto planes = aabb_planes(bbox);

        for (int i = 0; i < 6; ++i) {
            result[i] = winding_base_t::from_plane(planes[i], worldextent);
        }

        for (int i = 0; i < 6; ++i) {
            for (int j = 0; j < 6; ++j) {
                if (i == j)
                    continue;

                result[i] = *result[i].clip_back(planes[j]);
            }
        }

        return result;
    }

    // gtest support
    // also, makes printable via fmt since we include fmt/ostream.h
    friend std::ostream &operator<<(std::ostream &os, const winding_base_t &winding)
    {
        os << "{";
        for (size_t i = 0; i < winding.size(); ++i) {
            os << "(" << winding[i] << ")";
            if ((i + 1) < winding.size())
                os << ", ";
        }
        os << "}";
        return os;
    }
};

// the default amount of points to keep on stack
constexpr size_t STACK_POINTS_ON_WINDING = MAX_POINTS_ON_WINDING / 4;

using winding_t = winding_base_t<winding_storage_heap_t<double>>;
using winding3f_t = winding_base_t<winding_storage_heap_t<float>>;
}; // namespace polylib

// fmt support
template<class T>
struct fmt::formatter<polylib::winding_base_t<T>> : fmt::ostream_formatter
{
};
