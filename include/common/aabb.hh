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

#include <common/qvec.hh>
#include <common/iterators.hh>
#include <array>
#include <ostream>

/**!
 * touching a side/edge/corner is considered touching
 */
template<class V, size_t N>
class aabb
{
public:
    using value_type = qvec<V, N>;

    class intersection_t
    {
    public:
        bool valid;
        aabb bbox;

        constexpr intersection_t()
            : valid(false),
              bbox(value_type{}, value_type{})
        {
        }

        constexpr intersection_t(const aabb &i)
            : valid(true),
              bbox(i)
        {
        }

        constexpr auto operator<=>(const intersection_t &other) const = default;

        constexpr operator bool() const { return valid; }
    };

private:
    template<typename V2, size_t N2>
    friend class aabb;

    std::array<value_type, 2> m_corners;

    constexpr void fix()
    {
        for (size_t i = 0; i < N; i++) {
            if (m_corners[1][i] < m_corners[0][i]) {
                m_corners[1][i] = m_corners[0][i];
            }
        }
    }

public:
    constexpr aabb()
        : m_corners({value_type{std::numeric_limits<V>::max()}, value_type{std::numeric_limits<V>::lowest()}})
    {
    }

    constexpr aabb(const value_type &mins, const value_type &maxs)
        : m_corners({mins, maxs})
    {
        fix();
    }

    constexpr aabb(const value_type &points)
        : aabb(points, points)
    {
    }

    template<typename V2>
    constexpr aabb(const aabb<V2, N> &other)
        : aabb(other.m_corners[0], other.m_corners[1])
    {
    }

    template<typename Iter, std::enable_if_t<is_iterator_v<Iter>, int> = 0>
    constexpr aabb(Iter start, Iter end)
        : aabb()
    {
        for (auto it = start; it != end; it++) {
            *this += *it;
        }
    }

    constexpr auto operator<=>(const aabb &other) const = default;

    constexpr const value_type &mins() const { return m_corners[0]; }

    constexpr const value_type &maxs() const { return m_corners[1]; }

    constexpr aabb translate(const value_type &vec) const { return {mins() + vec, maxs() + vec}; }

    template<typename F>
    constexpr bool disjoint(const aabb<F, N> &other, const F &epsilon = 0) const
    {
        for (size_t i = 0; i < N; i++) {
            if (maxs()[i] < (other.mins()[i] - epsilon) || mins()[i] > (other.maxs()[i] + epsilon)) {
                return true;
            }
        }
        return false;
    }

    template<typename F>
    constexpr bool disjoint_or_touching(const aabb<F, N> &other, const F &epsilon = 0) const
    {
        for (size_t i = 0; i < N; i++) {
            if (maxs()[i] <= (other.mins()[i] - epsilon) || mins()[i] >= (other.maxs()[i] + epsilon)) {
                return true;
            }
        }
        return false;
    }

    constexpr bool contains(const aabb &other) const
    {
        for (size_t i = 0; i < N; i++) {
            if (other.mins()[i] < mins()[i] || other.maxs()[i] > maxs()[i]) {
                return false;
            }
        }
        return true;
    }

    constexpr bool containsPoint(const value_type &p) const
    {
        for (size_t i = 0; i < N; i++) {
            if (!(p[i] >= mins()[i] && p[i] <= maxs()[i])) {
                return false;
            }
        }
        return true;
    }

    constexpr aabb expand(const value_type &pt) const
    {
        auto corners = m_corners;
        for (size_t i = 0; i < N; i++) {
            corners[0][i] = std::min(corners[0][i], pt[i]);
            corners[1][i] = std::max(corners[1][i], pt[i]);
        }
        return {corners[0], corners[1]};
    }

    constexpr aabb operator+(const value_type &pt) const { return expand(pt); }

    constexpr aabb operator+(const aabb &other) const { return unionWith(other); }

    constexpr aabb unionWith(const aabb &other) const { return expand(other.mins()).expand(other.maxs()); }

    // in-place expansions

    constexpr aabb &expand_in_place(const value_type &pt)
    {
        for (size_t i = 0; i < N; i++) {
            m_corners[0][i] = std::min(m_corners[0][i], pt[i]);
            m_corners[1][i] = std::max(m_corners[1][i], pt[i]);
        }

        return *this;
    }

    constexpr aabb &operator+=(const value_type &pt) { return expand_in_place(pt); }

    constexpr aabb &operator+=(const aabb &other) { return unionWith_in_place(other); }

    constexpr aabb &unionWith_in_place(const aabb &other)
    {
        for (size_t i = 0; i < N; i++) {
            m_corners[0][i] = std::min({m_corners[0][i], other.mins()[i], other.maxs()[i]});
            m_corners[1][i] = std::max({m_corners[1][i], other.mins()[i], other.maxs()[i]});
        }

        return *this;
    }

    constexpr intersection_t intersectWith(const aabb &other) const
    {
        auto corners = m_corners;
        for (size_t i = 0; i < N; i++) {
            corners[0][i] = std::max(corners[0][i], other.mins()[i]);
            corners[1][i] = std::min(corners[1][i], other.maxs()[i]);
            if (corners[0][i] > corners[1][i]) {
                // empty intersection
                return {};
            }
        }
        return {{corners[0], corners[1]}};
    }

    constexpr value_type size() const { return maxs() - mins(); }

    constexpr bool valid() const
    {
        value_type our_size = size();

        if (our_size[0] < static_cast<V>(0) || our_size[1] < static_cast<V>(0) || our_size[2] < static_cast<V>(0)) {
            return false;
        }
        return true;
    }

    constexpr aabb grow(const value_type &size) const { return {mins() - size, maxs() + size}; }

    constexpr value_type &operator[](size_t index) { return m_corners[index]; }

    constexpr const value_type &operator[](size_t index) const { return m_corners[index]; }

    constexpr value_type centroid() const { return (mins() + maxs()) * 0.5; }

    constexpr V volume() const
    {
        auto s = size();
        return s[0] * s[1] * s[2];
    }

    constexpr auto begin() { return m_corners.begin(); }
    constexpr auto end() { return m_corners.end(); }

    constexpr auto begin() const { return m_corners.begin(); }
    constexpr auto end() const { return m_corners.end(); }

    // stream support
    auto stream_data() { return std::tie(m_corners); }

    // gtest support
    friend std::ostream &operator<<(std::ostream &os, const aabb &aabb)
    {
        os << fmt::format("{{mins: ({}), maxs: ({})}}", aabb.m_corners[0], aabb.m_corners[1]);
        return os;
    }
};

template<class V>
inline std::array<qplane3<V>, 6> aabb_planes(const aabb<V, 3> &bbox)
{
    return {
        qplane3<V>{qvec<V, 3>(1, 0, 0), bbox.maxs()[0]}, // +X
        qplane3<V>{qvec<V, 3>(-1, 0, 0), -bbox.mins()[0]}, // -X

        qplane3<V>{qvec<V, 3>(0, 1, 0), bbox.maxs()[1]}, // +Y
        qplane3<V>{qvec<V, 3>(0, -1, 0), -bbox.mins()[1]}, // -Y

        qplane3<V>{qvec<V, 3>(0, 0, 1), bbox.maxs()[2]}, // +Z
        qplane3<V>{qvec<V, 3>(0, 0, -1), -bbox.mins()[2]}, // -Z
    };
}

// Fmt support
template<class T, size_t Dim>
struct fmt::formatter<aabb<T, Dim>> : formatter<qvec<T, Dim>>
{
    template<typename FormatContext>
    auto format(const aabb<T, Dim> &b, FormatContext &ctx) -> decltype(ctx.out())
    {
        fmt::format_to(ctx.out(), "{{mins: ");
        fmt::formatter<qvec<T, Dim>>::format(b.mins(), ctx);
        fmt::format_to(ctx.out(), ", maxs: ");
        fmt::formatter<qvec<T, Dim>>::format(b.maxs(), ctx);
        fmt::format_to(ctx.out(), "}}");
        return ctx.out();
    }
};

using aabb3d = aabb<double, 3>;
using aabb2d = aabb<double, 2>;

using aabb3f = aabb<float, 3>;
using aabb2f = aabb<float, 2>;
