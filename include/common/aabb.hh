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
template<size_t N, class V>
class aabb
{
public:
    class intersection_t
    {
    public:
        bool valid;
        aabb<N, V> bbox;

        constexpr intersection_t() : valid(false), bbox(V(0), V(0)) { }

        constexpr intersection_t(const aabb<N, V> &i) : valid(true), bbox(i) { }

        constexpr bool operator==(const intersection_t &other) const
        {
            return valid == other.valid && bbox == other.bbox;
        }
    };

private:
    V m_mins, m_maxs;

    constexpr void fix()
    {
        for (size_t i = 0; i < N; i++) {
            if (m_maxs[i] < m_mins[i]) {
                m_maxs[i] = m_mins[i];
            }
        }
    }

public:
    constexpr aabb() : m_mins(VECT_MAX, VECT_MAX, VECT_MAX), m_maxs(-VECT_MAX, -VECT_MAX, -VECT_MAX) { }

    constexpr aabb(const V &mins, const V &maxs) : m_mins(mins), m_maxs(maxs) { fix(); }

    constexpr aabb(const V &points) : aabb(points, points) { }

    constexpr aabb(const aabb<N, V> &other) : aabb(other.m_mins, other.m_maxs) { }

    constexpr bool operator==(const aabb<N, V> &other) const
    {
        return m_mins == other.m_mins && m_maxs == other.m_maxs;
    }

    constexpr const V &mins() const { return m_mins; }

    constexpr const V &maxs() const { return m_maxs; }

    constexpr aabb translate(const V &vec) const { return {m_mins + vec, m_maxs + vec}; }

    constexpr bool disjoint(const aabb<N, V> &other, const typename V::value_type &epsilon = 0) const
    {
        for (size_t i = 0; i < N; i++) {
            if (m_maxs[i] < (other.m_mins[i] - epsilon))
                return true;
            if (m_mins[i] > (other.m_maxs[i] + epsilon))
                return true;
        }
        return false;
    }

    constexpr bool contains(const aabb<N, V> &other) const
    {
        for (size_t i = 0; i < 3; i++) {
            if (other.m_mins[i] < m_mins[i])
                return false;
            if (other.m_maxs[i] > m_maxs[i])
                return false;
        }
        return true;
    }

    constexpr bool containsPoint(const V &p) const
    {
        for (size_t i = 0; i < N; i++) {
            if (!(p[i] >= m_mins[i] && p[i] <= m_maxs[i]))
                return false;
        }
        return true;
    }

    constexpr aabb<N, V> expand(const V &pt) const
    {
        V mins = m_mins, maxs = m_maxs;
        for (size_t i = 0; i < N; i++) {
            mins[i] = min(mins[i], pt[i]);
            maxs[i] = max(maxs[i], pt[i]);
        }
        return aabb<N, V>(mins, maxs);
    }

    constexpr aabb<N, V> operator+(const V &pt) const { return expand(pt); }

    constexpr aabb<N, V> operator+(const aabb<N, V> &other) const { return unionWith(other); }

    constexpr aabb<N, V> &operator+=(const V &pt) { return (*this = expand(pt)); }

    constexpr aabb<N, V> &operator+=(const aabb<N, V> &other) { return (*this = unionWith(other)); }

    constexpr aabb<N, V> unionWith(const aabb<N, V> &other) const { return expand(other.m_mins).expand(other.m_maxs); }

    constexpr intersection_t intersectWith(const aabb<N, V> &other) const
    {
        V mins = m_mins, maxs = m_maxs;
        for (size_t i = 0; i < N; i++) {
            mins[i] = max(mins[i], other.m_mins[i]);
            maxs[i] = min(maxs[i], other.m_maxs[i]);
            if (mins[i] > maxs[i]) {
                // empty intersection
                return intersection_t();
            }
        }
        return intersection_t(aabb<N, V>(mins, maxs));
    }

    constexpr V size() const { return m_maxs - m_mins; }

    constexpr aabb<N, V> grow(const V &size) const { return aabb<N, V>(m_mins - size, m_maxs + size); }

    constexpr V &operator[](const size_t &index)
    {
        switch (index) {
            case 0:
                return m_mins;
            case 1:
                return m_maxs;
            default:
                throw std::exception();
        }
    }

    constexpr const V &operator[](const size_t &index) const
    {
        switch (index) {
            case 0:
                return m_mins;
            case 1:
                return m_maxs;
            default:
                throw std::exception();
        }
    }

    constexpr V centroid() const { return (m_mins + m_maxs) * 0.5; }
};

using aabb3d = aabb<3, qvec3d>;
using aabb2d = aabb<2, qvec2d>;

using aabb3f = aabb<3, qvec3f>;
using aabb2f = aabb<2, qvec2f>;
