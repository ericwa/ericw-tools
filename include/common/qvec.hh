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
#include <string>
#include <algorithm>
#include <array>
#include <fmt/format.h>

#define qmax std::max
#define qmin std::min
#define qclamp std::clamp

template<size_t N, class T>
class qvec
{
protected:
    std::array<T, N> v;

    template<size_t N2, class T2>
    friend class qvec;

public:
    using value_type = T;

    constexpr qvec() = default;
    
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
#endif
    template<typename... Args,
        typename = std::enable_if_t<sizeof...(Args) && std::is_convertible_v<std::common_type_t<Args...>, T>>>
    constexpr qvec(Args... a) :
        v({})
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
            constexpr size_t copy_size = qmin(N, count);
            size_t i = 0;
            ((i++ < copy_size ? (v[i - 1] = a, true) : false), ...);
        }
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    // copy from C-style array, exact lengths only
    template<typename T2>
    constexpr qvec(const T2 (&array)[N])
    {
        for (size_t i = 0; i < N; i++)
            v[i] = static_cast<T>(array[i]);
    }

    // copy from std::array, exact lengths only
    template<typename T2>
    constexpr qvec(const std::array<T2, N> &array)
    {
        for (size_t i = 0; i < N; i++)
            v[i] = static_cast<T>(array[i]);
    }

    /**
     * Casting from another vector type of the same length
     */
    template<class T2>
    constexpr qvec(const qvec<N, T2> &other)
    {
        for (size_t i = 0; i < N; i++)
            v[i] = static_cast<T>(other[i]);
    }

    /**
     * Casting from another vector type of the same type but
     * different length
     */
    template<size_t N2>
    constexpr qvec(const qvec<N2, T> &other)
    {
        constexpr size_t minSize = qmin(N, N2);

        // truncates if `other` is longer than `this`
        for (size_t i = 0; i < minSize; i++)
            v[i] = other[i];

        // zero-fill if `other` is smaller than `this`
        if constexpr (N2 < N) {
            for (size_t i = minSize; i < N; i++)
                v[i] = 0;
        }
    }

    /**
     * Extending a vector
     */
    constexpr qvec(const qvec<N - 1, T> &other, T value)
    {
        for (size_t i = 0; i < N - 1; ++i) {
            v[i] = other[i];
        }
        v[N - 1] = value;
    }

    [[nodiscard]] constexpr size_t size() const { return N; }

    // Sort support
    [[nodiscard]] constexpr bool operator<(const qvec<N, T> &other) const { return v < other.v; }
    [[nodiscard]] constexpr bool operator<=(const qvec<N, T> &other) const { return v <= other.v; }
    [[nodiscard]] constexpr bool operator>(const qvec<N, T> &other) const { return v > other.v; }
    [[nodiscard]] constexpr bool operator>=(const qvec<N, T> &other) const { return v >= other.v; }
    [[nodiscard]] constexpr bool operator==(const qvec<N, T> &other) const { return v == other.v; }
    [[nodiscard]] constexpr bool operator!=(const qvec<N, T> &other) const { return v != other.v; }

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

    template<typename F>
    constexpr void operator+=(const qvec<N, F> &other)
    {
        for (size_t i = 0; i < N; i++)
            v[i] += other.v[i];
    }
    template<typename F>
    constexpr void operator-=(const qvec<N, F> &other)
    {
        for (size_t i = 0; i < N; i++)
            v[i] -= other.v[i];
    }
    constexpr void operator*=(const T &scale)
    {
        for (size_t i = 0; i < N; i++)
            v[i] *= scale;
    }
    constexpr void operator/=(const T &scale)
    {
        for (size_t i = 0; i < N; i++)
            v[i] /= scale;
    }

    template<typename F>
    [[nodiscard]] constexpr qvec<N, T> operator+(const qvec<N, F> &other) const
    {
        qvec<N, T> res(*this);
        res += other;
        return res;
    }

    template<typename F>
    [[nodiscard]] constexpr qvec<N, T> operator-(const qvec<N, F> &other) const
    {
        qvec<N, T> res(*this);
        res -= other;
        return res;
    }

    [[nodiscard]] constexpr qvec<N, T> operator*(const T &scale) const
    {
        qvec<N, T> res(*this);
        res *= scale;
        return res;
    }

    [[nodiscard]] constexpr qvec<N, T> operator/(const T &scale) const
    {
        qvec<N, T> res(*this);
        res /= scale;
        return res;
    }

    [[nodiscard]] constexpr qvec<N, T> operator-() const
    {
        qvec<N, T> res(*this);
        res *= -1;
        return res;
    }

    [[nodiscard]] constexpr qvec<3, T> xyz() const
    {
        static_assert(N >= 3);
        return qvec<3, T>(*this);
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

namespace qv
{
template<class T>
[[nodiscard]] qvec<3, T> cross(const qvec<3, T> &v1, const qvec<3, T> &v2)
{
    return qvec<3, T>(v1[1] * v2[2] - v1[2] * v2[1], v1[2] * v2[0] - v1[0] * v2[2], v1[0] * v2[1] - v1[1] * v2[0]);
}

template<size_t N, class T>
[[nodiscard]] T dot(const qvec<N, T> &v1, const qvec<N, T> &v2)
{
    T result = 0;
    for (size_t i = 0; i < N; i++) {
        result += v1[i] * v2[i];
    }
    return result;
}

template<size_t N, class T>
[[nodiscard]] qvec<N, T> floor(const qvec<N, T> &v1)
{
    qvec<N, T> res;
    for (size_t i = 0; i < N; i++) {
        res[i] = std::floor(v1[i]);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] qvec<N, T> pow(const qvec<N, T> &v1, const qvec<N, T> &v2)
{
    qvec<N, T> res;
    for (size_t i = 0; i < N; i++) {
        res[i] = std::pow(v1[i], v2[i]);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] T min(const qvec<N, T> &v)
{
    T res = std::numeric_limits<T>::largest();
    for (auto &c : v) {
        res = qmin(c, res);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] T max(const qvec<N, T> &v)
{
    T res = std::numeric_limits<T>::lowest();
    for (auto &c : v) {
        res = qmax(c, res);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] qvec<N, T> min(const qvec<N, T> &v1, const qvec<N, T> &v2)
{
    qvec<N, T> res;
    for (size_t i = 0; i < N; i++) {
        res[i] = qmin(v1[i], v2[i]);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] qvec<N, T> max(const qvec<N, T> &v1, const qvec<N, T> &v2)
{
    qvec<N, T> res;
    for (size_t i = 0; i < N; i++) {
        res[i] = qmax(v1[i], v2[i]);
    }
    return res;
}

template<size_t N, class T>
[[nodiscard]] T length2(const qvec<N, T> &v1)
{
    T len2 = 0;
    for (size_t i = 0; i < N; i++) {
        len2 += (v1[i] * v1[i]);
    }
    return len2;
}

template<size_t N, class T>
[[nodiscard]] T length(const qvec<N, T> &v1)
{
    return std::sqrt(length2(v1));
}

template<size_t N, class T>
[[nodiscard]] qvec<N, T> normalize(const qvec<N, T> &v1)
{
    return v1 / length(v1);
}

template<size_t N, class T>
[[nodiscard]] T distance(const qvec<N, T> &v1, const qvec<N, T> &v2)
{
    return length(v2 - v1);
}

template<typename T>
[[nodiscard]] inline std::string to_string(const qvec<3, T> &v1)
{
    return fmt::format("{}", v1);
}

template<size_t N, class T>
[[nodiscard]] bool epsilonEqual(const qvec<N, T> &v1, const qvec<N, T> &v2, T epsilon)
{
    for (size_t i = 0; i < N; i++) {
        T diff = v1[i] - v2[i];
        if (fabs(diff) > epsilon)
            return false;
    }
    return true;
}

template<size_t N, class T>
[[nodiscard]] bool epsilonEmpty(const qvec<N, T> &v1, T epsilon)
{
    for (size_t i = 0; i < N; i++) {
        if (fabs(v1[i]) > epsilon)
            return false;
    }
    return true;
}

template<size_t N, class T>
[[nodiscard]] bool equalExact(const qvec<N, T> &v1, const qvec<N, T> &v2)
{
    for (size_t i = 0; i < N; i++) {
        if (v1[i] != v2[i])
            return false;
    }
    return true;
}

template<size_t N, class T>
[[nodiscard]] bool emptyExact(const qvec<N, T> &v1)
{
    for (size_t i = 0; i < N; i++) {
        if (v1[i])
            return false;
    }
    return true;
}

template<size_t N, class T>
[[nodiscard]] size_t indexOfLargestMagnitudeComponent(const qvec<N, T> &v)
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
}; // namespace qv

using qvec2f = qvec<2, float>;
using qvec3f = qvec<3, float>;
using qvec4f = qvec<4, float>;

using qvec2d = qvec<2, double>;
using qvec3d = qvec<3, double>;
using qvec4d = qvec<4, double>;

using qvec2i = qvec<2, int32_t>;
using qvec3i = qvec<3, int32_t>;

using qvec3s = qvec<3, int16_t>;

template<class T>
class qplane3
{
private:
    qvec<3, T> m_normal;
    T m_dist;

public:
    constexpr qplane3() = default;
    constexpr qplane3(const qvec<3, T> &normal, const T &dist) : m_normal(normal), m_dist(dist) { }

    template<typename T2>
    constexpr qplane3(const qplane3<T2> &plane) : qplane3(plane.normal(), plane.dist())
    {
    }

    [[nodiscard]] inline T distAbove(const qvec<3, T> &pt) const { return qv::dot(pt, m_normal) - m_dist; }
    [[nodiscard]] constexpr const qvec<3, T> &normal() const { return m_normal; }
    [[nodiscard]] constexpr const T dist() const { return m_dist; }

    [[nodiscard]] constexpr const qvec<4, T> vec4() const
    {
        return qvec<4, T>(m_normal[0], m_normal[1], m_normal[2], m_dist);
    }
};

using qplane3f = qplane3<float>;
using qplane3d = qplane3<double>;

/**
 * M row, N column matrix.
 */
template<size_t M, size_t N, class T>
class qmat
{
public:
    /**
     * Column-major order. [ (row0,col0), (row1,col0), .. ]
     */
    std::array<T, M * N> m_values;

public:
    /**
     * Identity matrix if square, otherwise fill with 0
     */
    constexpr qmat() : m_values({})
    {
        if constexpr (M == N) {
            // identity matrix
            for (size_t i = 0; i < N; i++) {
                this->at(i, i) = 1;
            }
        }
    }

    /**
     * Fill with a value
     */
    inline qmat(const T &val) { m_values.fill(val); }

    // copy constructor
    constexpr qmat(const qmat<M, N, T> &other) : m_values(other.m_values) { }

    /**
     * Casting from another matrix type of the same size
     */
    template<class T2>
    constexpr qmat(const qmat<M, N, T2> &other)
    {
        for (size_t i = 0; i < M * N; i++)
            this->m_values[i] = static_cast<T>(other.m_values[i]);
    }

    // initializer list, column-major order
    constexpr qmat(std::initializer_list<T> list)
    {
        assert(list.size() == M * N);
        std::copy(list.begin(), list.end(), m_values.begin());
    }

    constexpr bool operator==(const qmat<M, N, T> &other) const { return m_values == other.m_values; }

    // access to elements

    [[nodiscard]] constexpr T &at(size_t row, size_t col)
    {
        assert(row >= 0 && row < M);
        assert(col >= 0 && col < N);
        return m_values[col * M + row];
    }

    [[nodiscard]] constexpr T at(size_t row, size_t col) const
    {
        assert(row >= 0 && row < M);
        assert(col >= 0 && col < N);
        return m_values[col * M + row];
    }

    // hacky accessor for mat[col][row] access
    [[nodiscard]] constexpr const T *operator[](size_t col) const
    {
        assert(col >= 0 && col < N);
        return &m_values[col * M];
    }

    [[nodiscard]] constexpr T *operator[](size_t col)
    {
        assert(col >= 0 && col < N);
        return &m_values[col * M];
    }

    // multiplication by a vector

    [[nodiscard]] constexpr qvec<M, T> operator*(const qvec<N, T> &vec) const
    {
        qvec<M, T> res{};
        for (size_t i = 0; i < M; i++) { // for each row
            for (size_t j = 0; j < N; j++) { // for each col
                res[i] += this->at(i, j) * vec[j];
            }
        }
        return res;
    }

    // multiplication by a matrix

    template<size_t P>
    [[nodiscard]] constexpr qmat<M, P, T> operator*(const qmat<N, P, T> &other) const
    {
        qmat<M, P, T> res;
        for (size_t i = 0; i < M; i++) {
            for (size_t j = 0; j < P; j++) {
                T val = 0;
                for (size_t k = 0; k < N; k++) {
                    val += this->at(i, k) * other.at(k, j);
                }
                res.at(i, j) = val;
            }
        }
        return res;
    }

    // multiplication by a scalar

    [[nodiscard]] constexpr qmat<M, N, T> operator*(const T scalar) const
    {
        qmat<M, N, T> res(*this);
        for (size_t i = 0; i < M * N; i++) {
            res.m_values[i] *= scalar;
        }
        return res;
    }
};

using qmat2x2f = qmat<2, 2, float>;
using qmat2x3f = qmat<2, 3, float>;
using qmat2x4f = qmat<2, 4, float>;

using qmat3x2f = qmat<3, 2, float>;
using qmat3x3f = qmat<3, 3, float>;
using qmat3x4f = qmat<3, 4, float>;

using qmat4x2f = qmat<4, 2, float>;
using qmat4x3f = qmat<4, 3, float>;
using qmat4x4f = qmat<4, 4, float>;

using qmat2x2d = qmat<2, 2, double>;
using qmat2x3d = qmat<2, 3, double>;
using qmat2x4d = qmat<2, 4, double>;

using qmat3x2d = qmat<3, 2, double>;
using qmat3x3d = qmat<3, 3, double>;
using qmat3x4d = qmat<3, 4, double>;

using qmat4x2d = qmat<4, 2, double>;
using qmat4x3d = qmat<4, 3, double>;
using qmat4x4d = qmat<4, 4, double>;

namespace qv
{
/**
 * These return a matrix filled with NaN if there is no inverse.
 */
[[nodiscard]] qmat4x4f inverse(const qmat4x4f &input);
[[nodiscard]] qmat4x4d inverse(const qmat4x4d &input);

[[nodiscard]] qmat2x2f inverse(const qmat2x2f &input);
}; // namespace qv

// FMT support
#include <fmt/format.h>

template<size_t N, class T>
struct fmt::formatter<qvec<N, T>> : formatter<T>
{
    template<typename FormatContext>
    auto format(const qvec<N, T> &p, FormatContext &ctx) -> decltype(ctx.out())
    {
        for (size_t i = 0; i < N - 1; i++) {
            formatter<T>::format(p[i], ctx);
            format_to(ctx.out(), " ");
        }

        return formatter<T>::format(p[N - 1], ctx);
    }
};

using vec_t = double;
// "vec3" type. legacy; eventually will be replaced entirely
using vec3_t = vec_t[3];
constexpr vec_t VECT_MAX = std::numeric_limits<vec_t>::max();
