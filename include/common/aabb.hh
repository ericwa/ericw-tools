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

/**
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

        constexpr intersection_t() : valid(false), bbox(value_type{}, value_type{}) { }

        constexpr intersection_t(const aabb &i) : valid(true), bbox(i) { }

        constexpr bool operator==(const intersection_t &other) const
        {
            return valid == other.valid && bbox == other.bbox;
        }

        constexpr operator bool() const { return valid; }
    };

private:
    template<typename V2, size_t N2>
    friend class aabb;

    value_type m_mins, m_maxs;

    constexpr void fix()
    {
        for (size_t i = 0; i < N; i++) {
            if (m_maxs[i] < m_mins[i]) {
                m_maxs[i] = m_mins[i];
            }
        }
    }

public:
    constexpr aabb() : m_mins(std::numeric_limits<V>::max()), m_maxs(std::numeric_limits<V>::lowest()) { }

    constexpr aabb(const value_type &mins, const value_type &maxs) : m_mins(mins), m_maxs(maxs) { fix(); }

    constexpr aabb(const value_type &points) : aabb(points, points) { }

    template<typename V2>
    constexpr aabb(const aabb<V2, N> &other) : aabb(other.m_mins, other.m_maxs)
    {
    }

    constexpr bool operator==(const aabb &other) const { return m_mins == other.m_mins && m_maxs == other.m_maxs; }

    constexpr const value_type &mins() const { return m_mins; }

    constexpr const value_type &maxs() const { return m_maxs; }

    constexpr aabb translate(const value_type &vec) const { return {m_mins + vec, m_maxs + vec}; }

    template<typename F>
    constexpr bool disjoint(const aabb<F, N> &other, const F &epsilon = 0) const
    {
        for (size_t i = 0; i < N; i++) {
            if (m_maxs[i] < (other.m_mins[i] - epsilon))
                return true;
            if (m_mins[i] > (other.m_maxs[i] + epsilon))
                return true;
        }
        return false;
    }

    constexpr bool contains(const aabb &other) const
    {
        for (size_t i = 0; i < N; i++) {
            if (other.m_mins[i] < m_mins[i])
                return false;
            if (other.m_maxs[i] > m_maxs[i])
                return false;
        }
        return true;
    }

    constexpr bool containsPoint(const value_type &p) const
    {
        for (size_t i = 0; i < N; i++) {
            if (!(p[i] >= m_mins[i] && p[i] <= m_maxs[i]))
                return false;
        }
        return true;
    }

    constexpr aabb expand(const value_type &pt) const
    {
        value_type mins = m_mins, maxs = m_maxs;
        for (size_t i = 0; i < N; i++) {
            mins[i] = min(mins[i], pt[i]);
            maxs[i] = max(maxs[i], pt[i]);
        }
        return {mins, maxs};
    }

    constexpr aabb operator+(const value_type &pt) const { return expand(pt); }

    constexpr aabb operator+(const aabb &other) const { return unionWith(other); }

    constexpr aabb &operator+=(const value_type &pt) { return (*this = expand(pt)); }

    constexpr aabb &operator+=(const aabb &other) { return (*this = unionWith(other)); }

    constexpr aabb unionWith(const aabb &other) const { return expand(other.m_mins).expand(other.m_maxs); }

    constexpr intersection_t intersectWith(const aabb &other) const
    {
        value_type mins = m_mins, maxs = m_maxs;
        for (size_t i = 0; i < N; i++) {
            mins[i] = max(mins[i], other.m_mins[i]);
            maxs[i] = min(maxs[i], other.m_maxs[i]);
            if (mins[i] > maxs[i]) {
                // empty intersection
                return {};
            }
        }
        return {aabb(mins, maxs)};
    }

    constexpr value_type size() const { return m_maxs - m_mins; }

    constexpr aabb grow(const value_type &size) const { return {m_mins - size, m_maxs + size}; }

    constexpr value_type &operator[](const size_t &index)
    {
        switch (index) {
            case 0: return m_mins;
            case 1: return m_maxs;
            default: throw std::exception();
        }
    }

    constexpr const value_type &operator[](const size_t &index) const
    {
        switch (index) {
            case 0: return m_mins;
            case 1: return m_maxs;
            default: throw std::exception();
        }
    }

    constexpr value_type centroid() const { return (m_mins + m_maxs) * 0.5; }

    // stream support
    auto stream_data() { return std::tie(m_mins, m_maxs); }
};

using aabb3d = aabb<vec_t, 3>;
using aabb2d = aabb<vec_t, 2>;

using aabb3f = aabb<float, 3>;
using aabb2f = aabb<float, 2>;
