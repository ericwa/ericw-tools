/*  Copyright (C) 2017 Eric Wasylishen

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

#include <initializer_list>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <algorithm>
#include <array>
#include <ostream>
#include <fmt/core.h>
#include <tuple>
#include "common/mathlib.hh"

template<class T, size_t N>
class qvec
{
protected:
    std::array<T, N> v;

    template<class T2, size_t N2>
    friend class qvec;

public:
    using value_type = T;

    inline qvec() = default;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
#endif
    template<typename... Args,
        typename = std::enable_if_t<sizeof...(Args) && std::is_convertible_v<std::common_type_t<Args...>, T>>>
    constexpr qvec(Args... a)
        : v({})
    {
        constexpr size_t count = sizeof...(Args);

        // special case for single argument
        if constexpr (count == 1) {
            for (auto &e : v)
                ((e = a, true) || ...);
        }
        // multiple arguments; copy up to min(N, `count`),
        // leave `count` -> N as zeroes
        else {
            constexpr size_t copy_size = std::min(N, count);
            size_t i = 0;
            ((i++ < copy_size ? (v[i - 1] = a, true) : false), ...);
        }
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

private:
    template<typename FT, std::size_t... pack>
    constexpr void copy_impl(const FT &from, std::index_sequence<pack...> packed)
    {
        ((v[pack] = from[pack]), ...);
    }

public:
    // copy from C-style array, exact lengths only
    template<typename T2>
    constexpr qvec(const T2 (&array)[N])
    {
        copy_impl(array, std::make_index_sequence<N>());
    }

    // copy from std::array, exact lengths only
    template<typename T2>
    constexpr qvec(const std::array<T2, N> &array)
    {
        copy_impl(array, std::make_index_sequence<N>());
    }

    /**
     * Casting from another vector type of the same length
     */
    template<class T2>
    constexpr qvec(const qvec<T2, N> &other)
    {
        copy_impl(other, std::make_index_sequence<N>());
    }

private:
    template<typename FT, std::size_t... pack>
    constexpr void copy_trunc_impl(const FT &from, std::index_sequence<pack...> packed)
    {
        ((
            (pack < N) ?
                (v[pack] = ((pack < from.size() ? (from[pack]) : 0)))
            :
                (false)
         ), ...);
    }

public:
    /**
     * Casting from another vector type of the same type but
     * different length
     */
    template<size_t N2>
    constexpr qvec(const qvec<T, N2> &other)
    {
        // truncates if `other` is longer than `this`
        // zero-fill if `other` is smaller than `this`
        copy_trunc_impl(other, std::make_index_sequence<std::max(N, N2)>());
    }

    /**
     * Extending a vector
     */
    constexpr qvec(const qvec<T, N - 1> &other, T value)
    {
        std::copy(other.begin(), other.end(), v.begin());
        v[N - 1] = value;
    }

    [[nodiscard]] constexpr size_t size() const { return N; }

    // Sort support
    [[nodiscard]] constexpr bool operator<(const qvec &other) const { return v < other.v; }
    [[nodiscard]] constexpr bool operator<=(const qvec &other) const { return v <= other.v; }
    [[nodiscard]] constexpr bool operator>(const qvec &other) const { return v > other.v; }
    [[nodiscard]] constexpr bool operator>=(const qvec &other) const { return v >= other.v; }
    [[nodiscard]] constexpr bool operator==(const qvec &other) const { return v == other.v; }
    [[nodiscard]] constexpr bool operator!=(const qvec &other) const { return v != other.v; }

    [[nodiscard]] constexpr const T &at(const size_t idx) const
    {
        assert(idx >= 0 && idx < N);
        return v[idx];
    }

    [[nodiscard]] constexpr T &at(const size_t idx)
    {
        assert(idx >= 0 && idx < N);
        return v[idx];
    }

    [[nodiscard]] constexpr const T &operator[](const size_t idx) const { return at(idx); }
    [[nodiscard]] constexpr T &operator[](const size_t idx) { return at(idx); }
    
private:
    template<typename O, typename FT, typename F, std::size_t... pack>
    static constexpr void add_impl(O &out, const FT &a, const F &b, std::index_sequence<pack...> packed)
    {
        ((out[pack] = a[pack] + b[pack]), ...);
    }

public:
    template<typename F>
    [[nodiscard]] constexpr inline auto operator+(const qvec<F, N> &other) const
    {
        qvec<decltype(T() + F()), N> v;

        add_impl(v, *this, other, std::make_index_sequence<N>());

        return v;
    }
    
private:
    template<typename O, typename FT, typename F, std::size_t... pack>
    static constexpr void sub_impl(O &out, const FT &a, const F &b, std::index_sequence<pack...> packed)
    {
        ((out[pack] = a[pack] - b[pack]), ...);
    }

public:
    template<typename F>
    [[nodiscard]] constexpr inline auto operator-(const qvec<F, N> &other) const
    {
        qvec<decltype(T() - F()), N> v;

        sub_impl(v, *this, other, std::make_index_sequence<N>());

        return v;
    }
    
private:
    template<typename O, typename FT, typename F, std::size_t... pack>
    static constexpr void scale_v_impl(O &out, const FT &a, const F &b, std::index_sequence<pack...> packed)
    {
        ((out[pack] = a[pack] * b), ...);
    }

public:
    template<typename S>
    [[nodiscard]] constexpr inline auto operator*(const S &scale) const
    {
        qvec<decltype(T() * S()), N> v;

        scale_v_impl(v, *this, scale, std::make_index_sequence<N>());

        return v;
    }
    
private:
    template<typename O, typename FT, typename F, std::size_t... pack>
    static constexpr void scale_vf_impl(O &out, const FT &a, const F &b, std::index_sequence<pack...> packed)
    {
        ((out[pack] = a[pack] * b[pack]), ...);
    }

public:
    template<typename F>
    [[nodiscard]] constexpr inline auto operator*(const qvec<F, N> &scale) const
    {
        qvec<decltype(T() * F()), N> v;

        scale_vf_impl(v, *this, scale, std::make_index_sequence<N>());

        return v;
    }
    
private:
    template<typename O, typename FT, typename F, std::size_t... pack>
    static constexpr void div_v_impl(O &out, const FT &a, const F &b, std::index_sequence<pack...> packed)
    {
        ((out[pack] = a[pack] / b), ...);
    }

public:
    template<typename S>
    [[nodiscard]] constexpr inline auto operator/(const S &scale) const
    {
        qvec<decltype(T() / S()), N> v;

        div_v_impl(v, *this, scale, std::make_index_sequence<N>());

        return v;
    }
    
private:
    template<typename O, typename FT, typename F, std::size_t... pack>
    static constexpr void div_vf_impl(O &out, const FT &a, const F &b, std::index_sequence<pack...> packed)
    {
        ((out[pack] = a[pack] / b[pack]), ...);
    }

public:
    template<typename F>
    [[nodiscard]] constexpr inline auto operator/(const qvec<F, N> &scale) const
    {
        qvec<decltype(T() / F()), N> v;
        
        div_vf_impl(v, *this, scale, std::make_index_sequence<N>());

        return v;
    }
    
private:
    template<typename O, typename FT, std::size_t... pack>
    static constexpr void inv_v_impl(O &out, const FT &a, std::index_sequence<pack...> packed)
    {
        ((out[pack] = -a[pack]), ...);
    }

public:
    [[nodiscard]] constexpr inline auto operator-() const
    {
        qvec<decltype(-T()), N> v;
        
        inv_v_impl(v, *this, std::make_index_sequence<N>());

        return v;
    }

    template<typename F>
    inline qvec operator+=(const qvec<F, N> &other)
    {
        return *this = *this + other;
    }
    template<typename F>
    inline qvec operator-=(const qvec<F, N> &other)
    {
        return *this = *this - other;
    }
    template<typename S>
    inline qvec operator*=(const S &scale)
    {
        return *this = *this * scale;
    }
    template<typename F>
    inline qvec &operator*=(const qvec<F, N> &other)
    {
        return *this = *this * other;
    }
    template<typename S>
    inline qvec operator/=(const S &scale)
    {
        return *this = *this / scale;
    }
    template<typename F>
    inline qvec operator/=(const qvec<F, N> &other)
    {
        return *this = *this * other;
    }

    [[nodiscard]] constexpr qvec<T, 3> xyz() const
    {
        static_assert(N >= 3);
        return qvec<T, 3>(*this);
    }

    // stream support
    auto stream_data() { return std::tie(v); }

    // iterator support
    constexpr auto begin() { return v.begin(); }
    constexpr auto end() { return v.end(); }
    constexpr auto begin() const { return v.begin(); }
    constexpr auto end() const { return v.end(); }
    constexpr auto cbegin() const { return v.cbegin(); }
    constexpr auto cend() const { return v.cend(); }
};

// Fmt support
template<class T, size_t N>
struct fmt::formatter<qvec<T, N>>
{
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(const qvec<T, N> &p, FormatContext &ctx) -> decltype(ctx.out())
    {
        for (size_t i = 0; i < N - 1; i++) {
            fmt::format_to(ctx.out(), "{}", p[i]);
            fmt::format_to(ctx.out(), " ");
        }

        return fmt::format_to(ctx.out(), "{}", p[N - 1]);
    }
};

namespace qv
{
template<class T, class T2>
[[nodiscard]] constexpr auto cross(const qvec<T, 3> &v1, const qvec<T2, 3> &v2)
{
    return qvec<decltype(T() * T2() - T() * T2()), 3>{
        v1[1] * v2[2] - v1[2] * v2[1], v1[2] * v2[0] - v1[0] * v2[2], v1[0] * v2[1] - v1[1] * v2[0]};
}

template<typename T, typename U, std::size_t... pack>
constexpr auto dot_impl(const T &v1, const U &v2, std::index_sequence<pack...> packed)
{
    return ((v1[pack] * v2[pack]) + ...);
}

template<typename T, typename U, typename L = std::common_type_t<typename T::value_type, typename U::value_type>>
[[nodiscard]] constexpr L dot(const T &v1, const U &v2)
{
    static_assert(std::size(T()) == std::size(U()), "Can't dot() with two differently-sized vectors");

    constexpr size_t N = std::size(T());

    return dot_impl(v1, v2, std::make_index_sequence<N>());
}

template<size_t N, class T>
[[nodiscard]] inline qvec<T, N> floor(const qvec<T, N> &v1)
{
    qvec<T, N> res;
    for (size_t i = 0; i < N; i++) {
        res[i] = std::floor(v1[i]);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] inline qvec<T, N> ceil(const qvec<T, N> &v1)
{
    qvec<T, N> res;
    for (size_t i = 0; i < N; i++) {
        res[i] = std::ceil(v1[i]);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] inline qvec<T, N> pow(const qvec<T, N> &v1, const qvec<T, N> &v2)
{
    qvec<T, N> res;
    for (size_t i = 0; i < N; i++) {
        res[i] = std::pow(v1[i], v2[i]);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] constexpr T min(const qvec<T, N> &v)
{
    T res = std::numeric_limits<T>::largest();
    for (auto &c : v) {
        res = std::min(c, res);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] constexpr T max(const qvec<T, N> &v)
{
    T res = std::numeric_limits<T>::lowest();
    for (auto &c : v) {
        res = std::max(c, res);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] inline qvec<T, N> abs(const qvec<T, N> &v)
{
    qvec<T, N> res;
    for (size_t i = 0; i < N; i++) {
        res[i] = std::abs(v[i]);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] inline qvec<T, N> min(const qvec<T, N> &v1, const qvec<T, N> &v2)
{
    qvec<T, N> res;
    for (size_t i = 0; i < N; i++) {
        res[i] = std::min(v1[i], v2[i]);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] inline qvec<T, N> max(const qvec<T, N> &v1, const qvec<T, N> &v2)
{
    qvec<T, N> res;
    for (size_t i = 0; i < N; i++) {
        res[i] = std::max(v1[i], v2[i]);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] constexpr T length2(const qvec<T, N> &v1)
{
    return qv::dot(v1, v1);
}

template<size_t N, class T>
[[nodiscard]] inline T length(const qvec<T, N> &v1)
{
    return std::sqrt(length2(v1));
}

template<size_t N, class T, class T2>
[[nodiscard]] inline T distance(const qvec<T, N> &v1, const qvec<T2, N> &v2)
{
    return length(v2 - v1);
}

template<size_t N, class T>
[[nodiscard]] constexpr T distance2(const qvec<T, N> &v1, const qvec<T, N> &v2)
{
    return length2(v2 - v1);
}

template<size_t N, class T>
[[nodiscard]] inline qvec<T, N> normalize(const qvec<T, N> &v1, T &len)
{
    len = length(v1);
    return len ? (v1 / len) : v1;
}

template<size_t N, class T>
[[nodiscard]] inline qvec<T, N> normalize(const qvec<T, N> &v1)
{
    T len = length(v1);
    return len ? (v1 / len) : v1;
}

template<size_t N, class T>
inline T normalizeInPlace(qvec<T, N> &v1)
{
    T len = length(v1);
    if (len) {
        v1 /= len;
    }
    return len;
}

template<typename T>
[[nodiscard]] inline std::string to_string(const qvec<T, 3> &v1)
{
    return fmt::format("{}", v1);
}

// explicit specialization, for reducing compile times
template<>
[[nodiscard]] std::string to_string(const qvec<double, 3> &v1);

// explicit specialization, for reducing compile times
template<>
[[nodiscard]] std::string to_string(const qvec<int, 3> &v1);

template<typename T>
[[nodiscard]] inline bool epsilonEqual(const T &v1, const T &v2, T epsilon)
{
    return fabs(v1 - v2) <= epsilon;
}

template<size_t N, class T>
[[nodiscard]] inline bool epsilonEqual(const qvec<T, N> &v1, const qvec<T, N> &v2, T epsilon)
{
    for (size_t i = 0; i < N; i++) {
        if (!epsilonEqual(v1[i], v2[i], epsilon))
            return false;
    }
    return true;
}

template<size_t N, class T>
[[nodiscard]] inline bool epsilonEmpty(const qvec<T, N> &v1, T epsilon)
{
    return epsilonEqual({}, v1, epsilon);
}

template<typename T>
[[nodiscard]] inline bool gate(const T &v1, T epsilon)
{
    return v1 <= epsilon;
}

template<size_t N, class T>
[[nodiscard]] inline bool gate(const qvec<T, N> &v, T epsilon)
{
    for (size_t i = 0; i < N; i++) {
        if (!gate(v[i], epsilon)) {
            return false;
        }
    }
    return true;
}

template<size_t N, class T>
[[nodiscard]] constexpr bool equalExact(const qvec<T, N> &v1, const qvec<T, N> &v2)
{
    return v1 == v2;
}

template<size_t N, class T>
[[nodiscard]] constexpr bool emptyExact(const qvec<T, N> &v1)
{
    return equalExact({}, v1);
}

template<size_t N, class T>
[[nodiscard]] inline size_t indexOfLargestMagnitudeComponent(const qvec<T, N> &v)
{
    size_t largestIndex = 0;
    T largestMag = 0;

    for (size_t i = 0; i < N; ++i) {
        const T currentMag = std::fabs(v[i]);

        if (currentMag > largestMag) {
            largestMag = currentMag;
            largestIndex = i;
        }
    }

    return largestIndex;
}

template<typename T>
std::tuple<qvec<T, 3>, qvec<T, 3>> MakeTangentAndBitangentUnnormalized(const qvec<T, 3> &normal)
{
    // 0, 1, or 2
    const int axis = qv::indexOfLargestMagnitudeComponent(normal);
    const int otherAxisA = (axis + 1) % 3;
    const int otherAxisB = (axis + 2) % 3;

    // setup two other vectors that are perpendicular to each other
    qvec<T, 3> otherVecA{};
    otherVecA[otherAxisA] = 1.0;

    qvec<T, 3> otherVecB{};
    otherVecB[otherAxisB] = 1.0;

    auto tangent = qv::cross(normal, otherVecA);
    auto bitangent = qv::cross(normal, otherVecB);

    // We want `test` to point in the same direction as normal.
    // Swap the tangent bitangent if we got the direction wrong.
    qvec<T, 3> test = qv::cross(tangent, bitangent);

    if (qv::dot(test, normal) < 0) {
        std::swap(tangent, bitangent);
    }

    // debug test
#ifdef PARANOID
    if (0) {
        auto n = qv::normalize(qv::cross(tangent, bitangent));
        double d = qv::distance(n, normal);

        assert(d < 0.0001);
    }
#endif

    return {tangent, bitangent};
}

template<size_t N, typename T>
[[nodiscard]] inline T TriangleArea(const qvec<T, N> &v0, const qvec<T, N> &v1, const qvec<T, N> &v2)
{
    return static_cast<T>(0.5) * qv::length(qv::cross(v2 - v0, v1 - v0));
}

template<typename Iter, typename T = typename std::iterator_traits<Iter>::value_type>
[[nodiscard]] inline T PolyCentroid(Iter begin, Iter end)
{
    using value_type = typename T::value_type;
    size_t num_points = end - begin;

    if (!num_points)
        return qvec<value_type, 3>(std::numeric_limits<value_type>::quiet_NaN());
    else if (num_points == 1)
        return *begin;
    else if (num_points == 2)
        return avg(*begin, *(begin + 1));

    T poly_centroid{};
    value_type poly_area = 0;

    const T &v0 = *begin;
    for (auto it = begin + 2; it != end; ++it) {
        const T &v1 = *(it - 1);
        const T &v2 = *it;

        const value_type triarea = TriangleArea(v0, v1, v2);
        const T tricentroid = avg(v0, v1, v2);

        poly_area += triarea;
        poly_centroid = poly_centroid + (tricentroid * triarea);
    }

    poly_centroid /= poly_area;

    return poly_centroid;
}

template<typename Iter, typename T = typename std::iterator_traits<Iter>::value_type,
    typename F = typename T::value_type>
[[nodiscard]] inline F PolyArea(Iter begin, Iter end)
{
    if ((end - begin) < 3)
        return static_cast<F>(0);

    float poly_area = 0;

    const T &v0 = *begin;
    for (auto it = begin + 2; it != end; ++it) {
        const T &v1 = *(it - 1);
        const T &v2 = *it;

        poly_area += TriangleArea(v0, v1, v2);
    }

    return poly_area;
}

template<typename T>
[[nodiscard]] inline qvec<T, 3> Barycentric_FromPoint(
    const qvec<T, 3> &p, const qvec<T, 3> &t0, const qvec<T, 3> &t1, const qvec<T, 3> &t2)
{
    const auto v0 = t1 - t0;
    const auto v1 = t2 - t0;
    const auto v2 = p - t0;
    T d00 = qv::dot(v0, v0);
    T d01 = qv::dot(v0, v1);
    T d11 = qv::dot(v1, v1);
    T d20 = qv::dot(v2, v0);
    T d21 = qv::dot(v2, v1);
    T invDenom = (d00 * d11 - d01 * d01);
    invDenom = 1.0 / invDenom;

    qvec<T, 3> res;
    res[1] = (d11 * d20 - d01 * d21) * invDenom;
    res[2] = (d00 * d21 - d01 * d20) * invDenom;
    res[0] = 1.0 - res[1] - res[2];
    return res;
}

// from global illumination total compendium p. 12
template<typename T>
[[nodiscard]] inline qvec<T, 3> Barycentric_Random(T r1, T r2)
{
    qvec<T, 3> res;
    res[0] = 1.0 - sqrt(r1);
    res[1] = r2 * sqrt(r1);
    res[2] = 1.0 - res[0] - res[1];
    return res;
}

/// Evaluates the given barycentric coord for the given triangle
template<typename T>
[[nodiscard]] constexpr qvec<T, 3> Barycentric_ToPoint(
    const qvec<T, 3> &bary, const qvec<T, 3> &t0, const qvec<T, 3> &t1, const qvec<T, 3> &t2)
{
    return (t0 * bary[0]) + (t1 * bary[1]) + (t2 * bary[2]);
}

// Snap vector to nearest axial component
template<typename T>
[[nodiscard]] inline qvec<T, 3> Snap(qvec<T, 3> normal, const T &epsilon = NORMAL_EPSILON)
{
    for (auto &v : normal) {
        if (fabs(v - 1) < epsilon) {
            normal = {};
            v = 1;
            break;
        }
        if (fabs(v - -1) < epsilon) {
            normal = {};
            v = -1;
            break;
        }
    }

    return normal;
}

template<typename T>
inline qvec<T, 3> mangle_from_vec(const qvec<T, 3> &v)
{
    static constexpr qvec<T, 3> up(0, 0, 1);
    static constexpr qvec<T, 3> east(1, 0, 0);
    static constexpr qvec<T, 3> north(0, 1, 0);

    // get rotation about Z axis
    T x = qv::dot(east, v);
    T y = qv::dot(north, v);
    T theta = atan2(y, x);

    // get angle away from Z axis
    T cosangleFromUp = qv::dot(up, v);
    cosangleFromUp = std::min(std::max(static_cast<T>(-1.0), cosangleFromUp), static_cast<T>(1.0));
    T radiansFromUp = acosf(cosangleFromUp);

    return qvec<T, 3>{theta, -(radiansFromUp - Q_PI / 2.0), 0} * static_cast<T>(180.0 / Q_PI);
}

/* detect colors with components in 0-1 and scale them to 0-255 */
template<typename T>
constexpr qvec<T, 3> normalize_color_format(const qvec<T, 3> &color)
{
    if (color[0] >= 0 && color[0] <= 1 && color[1] >= 0 && color[1] <= 1 && color[2] >= 0 && color[2] <= 1) {
        return color * 255;
    }

    return color;
}

}; // namespace qv

using qvec2f = qvec<float, 2>;
using qvec3f = qvec<float, 3>;
using qvec4f = qvec<float, 4>;

using qvec2d = qvec<double, 2>;
using qvec3d = qvec<double, 3>;
using qvec4d = qvec<double, 4>;

using qvec2i = qvec<int32_t, 2>;
using qvec3i = qvec<int32_t, 3>;

using qvec3s = qvec<int16_t, 3>;
using qvec3b = qvec<uint8_t, 3>;

using qvec4b = qvec<uint8_t, 4>;

template<class T>
class qplane3
{
public:
    qvec<T, 3> normal;
    T dist;

    constexpr qplane3() = default;
    constexpr qplane3(const qvec<T, 3> &normal, const T &dist)
        : normal(normal),
          dist(dist)
    {
    }

    // convert from plane of a different type
    template<typename T2>
    constexpr qplane3(const qplane3<T2> &plane)
        : qplane3(plane.normal, static_cast<T2>(plane.dist))
    {
    }

private:
    auto as_tuple() const { return std::tie(normal, dist); }

public:
    // Sort support
    [[nodiscard]] constexpr bool operator<(const qplane3 &other) const { return as_tuple() < other.as_tuple(); }
    [[nodiscard]] constexpr bool operator<=(const qplane3 &other) const { return as_tuple() <= other.as_tuple(); }
    [[nodiscard]] constexpr bool operator>(const qplane3 &other) const { return as_tuple() > other.as_tuple(); }
    [[nodiscard]] constexpr bool operator>=(const qplane3 &other) const { return as_tuple() >= other.as_tuple(); }
    [[nodiscard]] constexpr bool operator==(const qplane3 &other) const { return as_tuple() == other.as_tuple(); }
    [[nodiscard]] constexpr bool operator!=(const qplane3 &other) const { return as_tuple() != other.as_tuple(); }

    [[nodiscard]] constexpr const qvec<T, 4> vec4() const { return qvec<T, 4>(normal[0], normal[1], normal[2], dist); }

    [[nodiscard]] constexpr qplane3 operator-() const { return {-normal, -dist}; }

    // generic case
    template<typename F>
    [[nodiscard]] inline F distance_to(const qvec<F, 3> &point) const
    {
        return qv::dot(point, normal) - dist;
    }

    // stream support
    void stream_write(std::ostream &s) const { s <= std::tie(normal, dist); }
    void stream_read(std::istream &s) { s >= std::tie(normal, dist); }
};

// Fmt support
template<class T>
struct fmt::formatter<qplane3<T>> : formatter<qvec<T, 3>>
{
    template<typename FormatContext>
    auto format(const qplane3<T> &p, FormatContext &ctx) -> decltype(ctx.out())
    {
        fmt::format_to(ctx.out(), "{{normal: ");
        fmt::formatter<qvec<T, 3>>::format(p.normal, ctx);
        fmt::format_to(ctx.out(), ", dist: {}}}", p.dist);
        return ctx.out();
    }
};

using qplane3f = qplane3<float>;
using qplane3d = qplane3<double>;

namespace qv
{
template<typename T>
[[nodiscard]] bool epsilonEqual(
    const qplane3<T> &p1, const qplane3<T> &p2, T normalEpsilon = NORMAL_EPSILON, T distEpsilon = DIST_EPSILON)
{
    return epsilonEqual(p1.normal, p2.normal, normalEpsilon) && epsilonEqual(p1.dist, p2.dist, distEpsilon);
}
} // namespace qv

/**
 * Row x Col matrix of T.
 */
template<class T, size_t NRow, size_t NCol>
class qmat
{
public:
    /**
     * Column-major order. [ (row0,col0), (row1,col0), .. ]
     */
    std::array<T, NRow * NCol> m_values;

public:
    /**
     * Identity matrix if square, otherwise fill with 0
     */
    constexpr qmat()
        : m_values({})
    {
        if constexpr (NRow == NCol) {
            // identity matrix
            for (size_t i = 0; i < NCol; i++) {
                this->at(i, i) = 1;
            }
        }
    }

    /**
     * Fill with a value
     */
    inline qmat(const T &val) { m_values.fill(val); }

    // copy constructor
    constexpr qmat(const qmat &other)
        : m_values(other.m_values)
    {
    }

    /**
     * Casting from another matrix type of the same size
     */
    template<class T2>
    constexpr qmat(const qmat<T2, NRow, NCol> &other)
    {
        for (size_t i = 0; i < NRow * NCol; i++)
            this->m_values[i] = static_cast<T>(other.m_values[i]);
    }

    // initializer list, column-major order
    constexpr qmat(std::initializer_list<T> list)
    {
        assert(list.size() == NRow * NCol);
        std::copy(list.begin(), list.end(), m_values.begin());
    }

    // static factory, row-major order
    static qmat row_major(std::initializer_list<T> list)
    {
        assert(list.size() == NRow * NCol);

        qmat result;
        for (size_t i = 0; i < NRow; i++) { // for each row
            for (size_t j = 0; j < NCol; j++) { // for each col
                result.at(i, j) = *(list.begin() + (i * NCol + j));
            }
        }
        return result;
    }

    // Sort support
    [[nodiscard]] constexpr bool operator<(const qmat &other) const { return m_values < other.m_values; }
    [[nodiscard]] constexpr bool operator<=(const qmat &other) const { return m_values <= other.m_values; }
    [[nodiscard]] constexpr bool operator>(const qmat &other) const { return m_values > other.m_values; }
    [[nodiscard]] constexpr bool operator>=(const qmat &other) const { return m_values >= other.m_values; }
    [[nodiscard]] constexpr bool operator==(const qmat &other) const { return m_values == other.m_values; }
    [[nodiscard]] constexpr bool operator!=(const qmat &other) const { return m_values != other.m_values; }

    // access to elements

    [[nodiscard]] constexpr T &at(size_t row, size_t col)
    {
        assert(row >= 0 && row < NRow);
        assert(col >= 0 && col < NCol);
        return m_values[col * NRow + row];
    }

    [[nodiscard]] constexpr T at(size_t row, size_t col) const
    {
        assert(row >= 0 && row < NRow);
        assert(col >= 0 && col < NCol);
        return m_values[col * NRow + row];
    }

    // access row
    [[nodiscard]] inline qvec<T, NCol> row(size_t row) const
    {
        assert(row >= 0 && row < NRow);
        qvec<T, NCol> v;
        for (size_t i = 0; i < NCol; i++) {
            v[i] = at(row, i);
        }
        return v;
    }

    constexpr void set_row(size_t row, const qvec<T, NCol> &values)
    {
        for (size_t i = 0; i < NCol; i++) {
            at(row, i) = values[i];
        }
    }

    [[nodiscard]] constexpr const qvec<T, NRow> &col(size_t col) const
    {
        assert(col >= 0 && col < NCol);
        return reinterpret_cast<const qvec<T, NRow> &>(m_values[col * NRow]);
    }

    inline void set_col(size_t col, const qvec<T, NRow> &values)
    {
        reinterpret_cast<qvec<T, NRow> &>(m_values[col * NRow]) = values;
    }

    // multiplication by a vector

    [[nodiscard]] constexpr qvec<T, NRow> operator*(const qvec<T, NCol> &vec) const
    {
        qvec<T, NRow> res{};
        for (size_t i = 0; i < NRow; i++) { // for each row
            for (size_t j = 0; j < NCol; j++) { // for each col
                res[i] += this->at(i, j) * vec[j];
            }
        }
        return res;
    }

    // multiplication by a matrix

    template<size_t PCol>
    [[nodiscard]] constexpr qmat<T, NRow, PCol> operator*(const qmat<T, NCol, PCol> &other) const
    {
        qmat<T, NRow, PCol> res;
        for (size_t i = 0; i < NRow; i++) {
            for (size_t j = 0; j < PCol; j++) {
                T val = 0;
                for (size_t k = 0; k < NCol; k++) {
                    val += this->at(i, k) * other.at(k, j);
                }
                res.at(i, j) = val;
            }
        }
        return res;
    }

    // multiplication by a scalar

    [[nodiscard]] constexpr qmat operator*(const T &scalar) const
    {
        qmat res(*this);
        for (size_t i = 0; i < NRow * NCol; i++) {
            res.m_values[i] *= scalar;
        }
        return res;
    }

    [[nodiscard]] constexpr qmat<T, NCol, NRow> transpose() const
    {
        qmat<T, NCol, NRow> res;
        for (size_t i = 0; i < NRow; i++) {
            for (size_t j = 0; j < NCol; j++) {
                res.at(j, i) = at(i, j);
            }
        }
        return res;
    }
};

// Fmt support
template<class T, size_t NRow, size_t NCol>
struct fmt::formatter<qmat<T, NRow, NCol>> : formatter<qvec<T, NCol>>
{
    template<typename FormatContext>
    auto format(const qmat<T, NRow, NCol> &p, FormatContext &ctx) -> decltype(ctx.out())
    {
        for (size_t i = 0; i < NRow; i++) {
            fmt::format_to(ctx.out(), "[ ");
            fmt::formatter<qvec<T, NCol>>::format(p.row(i), ctx);
            fmt::format_to(ctx.out(), " ]");

            if (i != NRow - 1) {
                fmt::format_to(ctx.out(), "\n");
            }
        }

        return ctx.out();
    }
};

using qmat2x2f = qmat<float, 2, 2>;
using qmat3x3f = qmat<float, 3, 3>;
using qmat4x4f = qmat<float, 4, 4>;

using qmat2x2d = qmat<double, 2, 2>;
using qmat3x3d = qmat<double, 3, 3>;
using qmat4x4d = qmat<double, 4, 4>;

namespace qv
{
/**
 * These return a matrix filled with NaN if there is no inverse.
 */
[[nodiscard]] qmat4x4f inverse(const qmat4x4f &input);
[[nodiscard]] qmat4x4d inverse(const qmat4x4d &input);

[[nodiscard]] qmat2x2f inverse(const qmat2x2f &input);
[[nodiscard]] qmat3x3f inverse(const qmat3x3f &input);
}; // namespace qv

// returns the normalized direction from `start` to `stop` in the `dir` param
// returns the distance from `start` to `stop`
template<typename Tstart, typename Tstop, typename Tdir>
inline vec_t GetDir(const Tstart &start, const Tstop &stop, Tdir &dir)
{
    return qv::normalizeInPlace(dir = (stop - start));
}

// Stores a normal, tangent and bitangent
struct face_normal_t
{
    qvec3f normal, tangent, bitangent;
};

qmat3x3d RotateAboutX(double radians);
qmat3x3d RotateAboutY(double radians);
qmat3x3d RotateAboutZ(double radians);
qmat3x3f RotateFromUpToSurfaceNormal(const qvec3f &surfaceNormal);

// Returns (0 0 0) if we couldn't determine the normal
qvec3f FaceNormal(std::vector<qvec3f> points);
std::pair<bool, qvec4f> MakeInwardFacingEdgePlane(const qvec3f &v0, const qvec3f &v1, const qvec3f &faceNormal);
std::vector<qvec4f> MakeInwardFacingEdgePlanes(const std::vector<qvec3f> &points);
bool EdgePlanes_PointInside(const std::vector<qvec4f> &edgeplanes, const qvec3f &point);
float EdgePlanes_PointInsideDist(const std::vector<qvec4f> &edgeplanes, const qvec3f &point);
qvec4f MakePlane(const qvec3f &normal, const qvec3f &point);
float DistAbovePlane(const qvec4f &plane, const qvec3f &point);
qvec3f ProjectPointOntoPlane(const qvec4f &plane, const qvec3f &point);
qvec4f PolyPlane(const std::vector<qvec3f> &points);
/// Returns the index of the polygon edge, and the closest point on that edge, to the given point
std::pair<int, qvec3f> ClosestPointOnPolyBoundary(const std::vector<qvec3f> &poly, const qvec3f &point);
/// Returns `true` and the interpolated normal if `point` is in the polygon, otherwise returns false.
std::pair<bool, qvec3f> InterpolateNormal(
    const std::vector<qvec3f> &points, const std::vector<face_normal_t> &normals, const qvec3f &point);
std::pair<bool, qvec3f> InterpolateNormal(
    const std::vector<qvec3f> &points, const std::vector<qvec3f> &normals, const qvec3f &point);
std::vector<qvec3f> ShrinkPoly(const std::vector<qvec3f> &poly, const float amount);
/// Returns (front part, back part)
std::pair<std::vector<qvec3f>, std::vector<qvec3f>> ClipPoly(const std::vector<qvec3f> &poly, const qvec4f &plane);

class poly_random_point_state_t
{
public:
    std::vector<qvec3f> points;
    std::vector<float> triareas;
    std::vector<float> triareas_cdf;
};

poly_random_point_state_t PolyRandomPoint_Setup(const std::vector<qvec3f> &points);
qvec3f PolyRandomPoint(const poly_random_point_state_t &state, float r1, float r2, float r3);

/// projects p onto the vw line.
/// returns 0 for p==v, 1 for p==w
float FractionOfLine(const qvec3f &v, const qvec3f &w, const qvec3f &p);

/**
 * Distance from `p` to the line v<->w (extending infinitely in either direction)
 */
float DistToLine(const qvec3f &v, const qvec3f &w, const qvec3f &p);

qvec3f ClosestPointOnLine(const qvec3f &v, const qvec3f &w, const qvec3f &p);

/**
 * Distance from `p` to the line segment v<->w.
 * i.e., 0 if `p` is between v and w.
 */
float DistToLineSegment(const qvec3f &v, const qvec3f &w, const qvec3f &p);

qvec3f ClosestPointOnLineSegment(const qvec3f &v, const qvec3f &w, const qvec3f &p);

float SignedDegreesBetweenUnitVectors(const qvec3f &start, const qvec3f &end, const qvec3f &normal);

enum class concavity_t
{
    Coplanar,
    Concave,
    Convex
};

concavity_t FacePairConcavity(
    const qvec3f &face1Center, const qvec3f &face1Normal, const qvec3f &face2Center, const qvec3f &face2Normal);

// Returns weights for f(0,0), f(1,0), f(0,1), f(1,1)
// from: https://en.wikipedia.org/wiki/Bilinear_interpolation#Unit_Square
qvec4f bilinearWeights(const float x, const float y);

// This uses a coordinate system where the pixel centers are on integer coords.
// e.g. the corners of a 3x3 pixel bitmap are at (-0.5, -0.5) and (2.5, 2.5).
std::array<std::pair<qvec2i, float>, 4> bilinearWeightsAndCoords(qvec2f pos, const qvec2i &size);

template<typename V>
V bilinearInterpolate(const V &f00, const V &f10, const V &f01, const V &f11, const float x, const float y)
{
    qvec4f weights = bilinearWeights(x, y);

    const V fxy = f00 * weights[0] + f10 * weights[1] + f01 * weights[2] + f11 * weights[3];

    return fxy;
}

template<typename V>
std::vector<V> PointsAlongLine(const V &start, const V &end, const float step)
{
    const V linesegment = end - start;
    const float len = qv::length(linesegment);
    if (len == 0)
        return {};

    std::vector<V> result;
    const int stepCount = static_cast<int>(len / step);
    result.reserve(stepCount + 1);
    const V dir = linesegment / len;
    for (int i = 0; i <= stepCount; i++) {
        result.push_back(start + (dir * (step * i)));
    }
    return result;
}

bool LinesOverlap(const qvec3f &p0, const qvec3f &p1, const qvec3f &q0, const qvec3f &q1,
    const vec_t &on_epsilon = DEFAULT_ON_EPSILON);

template<typename T>
struct twosided
{
    T front, back;

    // 0 is front, 1 is back
    constexpr T &operator[](const int32_t &i)
    {
        switch (i) {
            case 0: return front;
            case 1: return back;
        }

        throw std::exception();
    }
    // 0 is front, 1 is back
    constexpr const T &operator[](const int32_t &i) const
    {
        switch (i) {
            case 0: return front;
            case 1: return back;
        }

        throw std::exception();
    }

    // iterator support
    T *begin() { return &front; }
    T *end() { return (&back) + 1; }

    const T *begin() const { return &front; }
    const T *end() const { return (&back) + 1; }

    // swap the front and back values
    constexpr void swap() { std::swap(front, back); }
};

namespace qv
{

template<typename T>
inline qvec<T, 3> vec_from_mangle(const qvec<T, 3> &m)
{
    const qvec<T, 3> mRadians = m * static_cast<T>(Q_PI / 180.0);
    const qmat3x3d rotations = RotateAboutZ(mRadians[0]) * RotateAboutY(-mRadians[1]);
    return {rotations * qvec3d(1, 0, 0)};
}

} // namespace qv

// for Catch2
template<typename T>
std::ostream &operator<<(std::ostream &os, const qvec<T, 3> &v)
{
    return os << qv::to_string(v);
}
