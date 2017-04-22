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

#ifndef __COMMON_AABB_HH__
#define __COMMON_AABB_HH__

#include <common/qvec.hh>

/**
 * touching a side/edge/corner is considered touching
 */
template <int N, class V>
class aabb {
public:
    class intersection_t {
    public:
        bool valid;
        aabb<N,V> bbox;
        
        intersection_t()
        : valid(false),
          bbox(V(0), V(0)) {}
        
        intersection_t(const aabb<N,V> &i)
        : valid(true),
          bbox(i) {}
        
        bool operator==(const intersection_t &other) const {
            return valid == other.valid && bbox == other.bbox;
        }
    };
    
private:
    V m_mins, m_maxs;
    
    void fix() {
        for (int i=0; i<N; i++) {
            if (m_maxs[i] < m_mins[i]) {
                m_maxs[i] = m_mins[i];
            }
        }
    }
    
public:
    aabb(const V &mins, const V &maxs) : m_mins(mins), m_maxs(maxs) {
        fix();
    }
    
    aabb(const aabb<N,V> &other) : m_mins(other.m_mins), m_maxs(other.m_maxs) {
        fix();
    }
    
    bool operator==(const aabb<N,V> &other) const {
        return m_mins == other.m_mins
            && m_maxs == other.m_maxs;
    }
    
    const V &mins() const {
        return m_mins;
    }
    
    const V &maxs() const {
        return m_maxs;
    }
    
    bool disjoint(const aabb<N,V> &other) const {
        for (int i=0; i<N; i++) {
            if (m_maxs[i] < other.m_mins[i]) return true;
            if (m_mins[i] > other.m_maxs[i]) return true;
        }
        return false;
    }
    
    bool contains(const aabb<N,V> &other) const {
        for (int i=0; i<3; i++) {
            if (other.m_mins[i] < m_mins[i])
                return false;
            if (other.m_maxs[i] > m_maxs[i])
                return false;
        }
        return true;
    }
    
    bool containsPoint(const V &p) const {
        for (int i=0; i<N; i++) {
            if (!(p[i] >= m_mins[i] && p[i] <= m_maxs[i])) return false;
        }
        return true;
    }

    aabb<N,V> expand(const V &pt) const {
        V mins, maxs;
        for (int i=0; i<N; i++) {
            mins[i] = qmin(m_mins[i], pt[i]);
            maxs[i] = qmax(m_maxs[i], pt[i]);
        }
        return aabb<N,V>(mins, maxs);
    }
    
    aabb<N,V> unionWith(const aabb<N,V> &other) const {
        return expand(other.m_mins).expand(other.m_maxs);
    }

    intersection_t intersectWith(const aabb<N,V> &other) const {
        V mins, maxs;
        for (int i=0; i<N; i++) {
            mins[i] = qmax(m_mins[i], other.m_mins[i]);
            maxs[i] = qmin(m_maxs[i], other.m_maxs[i]);
            if (mins[i] > maxs[i]) {
                // empty intersection
                return intersection_t();
            }
        }
        return intersection_t(aabb<N,V>(mins, maxs));
    }
    
    V size() const {
        V result = m_maxs - m_mins;
        return result;
    }
    
    aabb<N,V> grow(const V &size) const {
        return aabb<N,V>(m_mins - size, m_maxs + size);
    }
};

using aabb3d = aabb<3, qvec3d>;
using aabb2d = aabb<2, qvec2d>;

using aabb3f = aabb<3, qvec3f>;
using aabb2f = aabb<2, qvec2f>;

#endif /* __COMMON_AABB_HH__ */
