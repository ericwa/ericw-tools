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

#ifndef __COMMON_OCTREE_HH__
#define __COMMON_OCTREE_HH__

#include <common/aabb.hh>

#include <utility> // for std::pair
#include <vector>
#include <set>
#include <cassert>

static inline aabb3f bboxOctant(const aabb3f &box, int i)
{
    assert(i >= 0 && i < 8);
    
    const qvec3f mid = (box.mins() + box.maxs()) * 0.5f;
    
    const qvec3f octantSigns((i & 1) ? 1.0f : -1.0f,
                             (i & 2) ? 1.0f : -1.0f,
                             (i & 4) ? 1.0f : -1.0f);
    
    qvec3f mins, maxs;
    for (int j=0; j<3; j++) {
        if (octantSigns[j] == -1.0f) {
            mins[j] = box.mins()[j];
            maxs[j] = mid[j];
        } else {
            mins[j] = mid[j];
            maxs[j] = box.maxs()[j];
        }
    }
    
    return aabb3f(mins, maxs);
}

#define MAX_OCTREE_DEPTH 3

using octree_nodeid = int;

template <typename T>
class octree_node_t {
public:
    int m_depth;
    aabb3f m_bbox;
    bool m_leafNode;
    std::vector<std::pair<aabb3f, T>> m_leafObjects; // only nonempty if m_leafNode
    octree_nodeid m_children[8]; // only use if !m_leafNode
    
    octree_node_t(const aabb3f &box, const int depth) :
        m_depth(depth),
        m_bbox(box),
        m_leafNode(true),
        m_leafObjects()
        {
            assert(m_depth <= MAX_OCTREE_DEPTH);
        }
};

template <typename T>
class octree_t {
private:
    std::vector<octree_node_t<T>> m_nodes;

    /**
     * helper for toNode().
     *
     * creates the ith octant child of `thisNode`, and adds it to the end of `octree`.
     * returns the nodeid.
     */
    octree_nodeid createChild(octree_nodeid thisNode, int i) {
        octree_node_t<T> *node = &m_nodes[thisNode];
        
        const aabb3f childBox = bboxOctant(node->m_bbox, i);
        octree_node_t<T> newNode(childBox, node->m_depth + 1);
        
        m_nodes.push_back(newNode); // invalidates `node` reference
        return static_cast<int>(m_nodes.size() - 1);
    }
    
    void toNode(octree_nodeid thisNode) {
        octree_nodeid newNodeIds[8];
        for (int i=0; i<8; i++) {
            newNodeIds[i] = createChild(thisNode, i);
        }
        
        octree_node_t<T> *node = &m_nodes[thisNode];
        assert(node->m_leafNode);
        assert(node->m_leafObjects.empty()); // we always convert leafs to nodes before adding anything
        for (int i=0; i<8; i++) {
            node->m_children[i] = newNodeIds[i];
        }
        node->m_leafNode = false;
    }
    
    void queryTouchingBBox(octree_nodeid thisNode, const aabb3f &query, std::set<T> &dest) const {
        const octree_node_t<T> *node = &m_nodes[thisNode];
        
        if (node->m_leafNode) {
            // Test all objects
            for (const auto &boxObjPair : node->m_leafObjects) {
                if (!query.disjoint(boxObjPair.first)) {
                    dest.insert(boxObjPair.second);
                }
            }
            return;
        }
        
        // Test all children that intersect the query
        for (int i=0; i<8; i++) {
            const octree_nodeid child_i_index = node->m_children[i];
            const octree_node_t<T> *child_i_node = &m_nodes[child_i_index];
            
            const aabb3f::intersection_t intersection = query.intersectWith(child_i_node->m_bbox);
            if (intersection.valid) {
                queryTouchingBBox(child_i_index, intersection.bbox, dest);
            }
        }
    }
    
    void insert(octree_nodeid thisNode, const aabb3f &objBox, const T &obj) {
        octree_node_t<T> *node = &m_nodes[thisNode];
        assert(node->m_bbox.contains(objBox));
        
        if (node->m_leafNode && node->m_depth < MAX_OCTREE_DEPTH) {
            toNode(thisNode);
            // N.B.: `node` pointer was invalidated
            node = &m_nodes[thisNode];
        }
        
        if (node->m_leafNode) {
            node->m_leafObjects.push_back(std::make_pair(objBox, obj));
            return;
        }
        
        // inserting into a non-leaf node
        for (int i=0; i<8; i++) {
            const octree_nodeid child_i_index = node->m_children[i];
            octree_node_t<T> *child_i_node = &m_nodes[child_i_index];
            const aabb3f::intersection_t intersection = objBox.intersectWith(child_i_node->m_bbox);
            if (intersection.valid) {
                insert(child_i_index, intersection.bbox, obj);
                
                // N.B.: `node` pointer was invalidated
                node = &m_nodes[thisNode];
            }
        }
    }
    
public:
    void insert(const aabb3f &objBox, const T &obj) {
        insert(0, objBox, obj);
    }
    
    std::vector<T> queryTouchingBBox(const aabb3f &query) const {
        std::set<T> res;
        queryTouchingBBox(0, query, res);
        
        std::vector<T> res_vec;
        res_vec.reserve(res.size()); //mxd. https://clang.llvm.org/extra/clang-tidy/checks/performance-inefficient-vector-operation.html
        for (const auto &item : res) {
            res_vec.push_back(item);
        }
        return res_vec;
    }
    
    octree_t(const aabb3f &box) {
        this->m_nodes.push_back(octree_node_t<T>(box, 0));
    }
};

template <typename T>
octree_t<T> makeOctree(const std::vector<std::pair<aabb3f, T>> &objects)
{
    if (objects.empty()) {
        octree_t<T> empty{aabb3f{qvec3f(), qvec3f()}};
        return empty;
    }
    
    // take bbox of objects
    aabb3f box = objects.at(0).first;
    for (const auto &pr : objects) {
        box = box.unionWith(pr.first);
    }
    
    octree_t<T> res(box);
    for (const auto &pr : objects) {
        res.insert(pr.first, pr.second);
    }
    return res;
}

#endif /* __COMMON_OCTREE_HH__ */
