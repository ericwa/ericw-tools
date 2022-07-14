/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

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
// tjunc.c

#include <qbsp/qbsp.hh>
#include <qbsp/map.hh>
#include <atomic>

std::atomic<size_t> c_degenerate;
std::atomic<size_t> c_tjunctions;
std::atomic<size_t> c_faceoverflows;
std::atomic<size_t> c_facecollapse;
std::atomic<size_t> c_norotates, c_rotates, c_retopology, c_faceretopology;

inline std::optional<vec_t> PointOnEdge(const qvec3d &p, const qvec3d &edge_start, const qvec3d &edge_dir, float start = 0, float end = 1)
{
	qvec3d delta = p - edge_start;
	vec_t dist = qv::dot(delta, edge_dir);

	// check if off an end
	if (dist <= start || dist >= end) {
		return std::nullopt;
	}

	qvec3d exact = edge_start + (edge_dir * dist);
	qvec3d off = p - exact;
	vec_t error = qv::length(off);

	// brushbsp-fixme: this was 0.5 in Q2, check?
	if (fabs(error) > DEFAULT_ON_EPSILON) {
		// not on the edge
		return std::nullopt;
	}

	return dist;
}

/*
==========
TestEdge

Can be recursively reentered
==========
*/
inline void TestEdge(vec_t start, vec_t end, size_t p1, size_t p2, size_t startvert, const std::vector<size_t> &edge_verts, const qvec3d &edge_start, const qvec3d &edge_dir, std::vector<size_t> &superface)
{
	if (p1 == p2) {
		// degenerate edge
		c_degenerate++;
		return;
	}

	for (size_t k = startvert; k < edge_verts.size(); k++) {
		size_t j = edge_verts[k];

		if (j == p1 || j == p2) {
			continue;
		}

		auto dist = PointOnEdge(map.bsp.dvertexes[j], edge_start, edge_dir, start, end);

		if (!dist.has_value()) {
			continue;
		}

		// break the edge
		c_tjunctions++;
		TestEdge (start, dist.value(), p1, j, k + 1, edge_verts, edge_start, edge_dir, superface);
		TestEdge (dist.value(), end, j, p2, k + 1, edge_verts, edge_start, edge_dir, superface);
		return;
	}

	// the edge p1 to p2 is now free of tjunctions
	superface.push_back(p1);
}

/*
==========
FindEdgeVerts_BruteForce

Force a dumb check of everything
==========
*/
static void FindEdgeVerts_BruteForce(const node_t *, const node_t *, const qvec3d &, const qvec3d &, std::vector<size_t> &verts)
{
	verts.resize(map.bsp.dvertexes.size());

	for (size_t i = 0; i < verts.size(); i++) {
		verts[i] = i;
	}
}

/*
==========
FindEdgeVerts_FaceBounds_R

Recursive function for matching nodes that intersect the aabb
for vertex checking.
==========
*/
static void FindEdgeVerts_FaceBounds_R(const node_t *node, const aabb3d &aabb, std::vector<size_t> &verts)
{
	if (node->planenum == PLANENUM_LEAF) {
		return;
	} else if (node->bounds.disjoint(aabb, 0.0)) {
		return;
	}

	for (auto &face : node->facelist) {
		for (auto &v : face->original_vertices) {
			if (aabb.containsPoint(map.bsp.dvertexes[v])) {
				verts.push_back(v);
			}
		}
	}
	
	FindEdgeVerts_FaceBounds_R(node->children[0].get(), aabb, verts);
	FindEdgeVerts_FaceBounds_R(node->children[1].get(), aabb, verts);
}

/*
==========
FindEdgeVerts_FaceBounds

Use a loose AABB around the line and only capture vertices that intersect it.
==========
*/
static void FindEdgeVerts_FaceBounds(const node_t *headnode, const qvec3d &p1, const qvec3d &p2, std::vector<size_t> &verts)
{
	// magic number, average of "usual" points per edge
	verts.reserve(8);

	FindEdgeVerts_FaceBounds_R(headnode, (aabb3d{} + p1 + p2).grow(qvec3d(1.0, 1.0, 1.0)), verts);
}

/*
==================
SplitFaceIntoFragments

The face was created successfully, but may have way too many edges.
Cut it down to the minimum amount of faces that are within the
max edge count.

Modifies `superface`. Adds the results to the end of `output`.
==================
*/
inline void SplitFaceIntoFragments(std::vector<size_t> &superface, std::list<std::vector<size_t>> &output)
{
	const int32_t &maxedges = qbsp_options.maxedges.value();

	// split into multiple fragments, because of vertex overload
	while (superface.size() > maxedges) {
		c_faceoverflows++;

		// copy MAXEDGES from our current face
		std::vector<size_t> &newf = output.emplace_back(maxedges);
		std::copy_n(superface.begin(), maxedges, newf.begin());

		// remove everything in-between from the superface
		// except for the last edge we just wrote (0 and MAXEDGES-1)
		std::copy(superface.begin() + maxedges - 1, superface.end(), superface.begin() + 1);

		// resize superface; we need enough room to store the two extra verts
		superface.resize(superface.size() - maxedges + 2);
	}

	// move the first face to the end, since that's logically where it belongs now
	output.splice(output.end(), output, output.begin());
}

float AngleOfTriangle(const qvec3d &a, const qvec3d &b, const qvec3d &c)
{
    vec_t num = (b[0]-a[0])*(c[0]-a[0])+(b[1]-a[1])*(c[1]-a[1])+(b[2]-a[2])*(c[2]-a[2]);
    vec_t den = sqrt(pow((b[0]-a[0]),2)+pow((b[1]-a[1]),2)+pow((b[2]-a[2]),2))*
                sqrt(pow((c[0]-a[0]),2)+pow((c[1]-a[1]),2)+pow((c[2]-a[2]),2));
 
    return acos(num / den) * (180.0 / 3.141592653589793238463);
}

// Check whether the given input triangle would be valid
// on the given face and not have any other points
// intersecting it.
inline bool TriangleIsValid(size_t v0, size_t v1, size_t v2, const std::vector<size_t> &face, float angle_epsilon)
{
	if (AngleOfTriangle(map.bsp.dvertexes[v0], map.bsp.dvertexes[v1], map.bsp.dvertexes[v2]) < angle_epsilon ||
		AngleOfTriangle(map.bsp.dvertexes[v1], map.bsp.dvertexes[v2], map.bsp.dvertexes[v0]) < angle_epsilon ||
		AngleOfTriangle(map.bsp.dvertexes[v2], map.bsp.dvertexes[v0], map.bsp.dvertexes[v1]) < angle_epsilon) {
		return false;
	}

	return true;
}

/*
==================
CreateSuperFace

Generate a superface (the input face `f` but with all of the
verts in the world added that lay on the line) and return it
==================
*/
static std::vector<size_t> CreateSuperFace(node_t *headnode, face_t *f)
{
	std::vector<size_t> superface;

	superface.reserve(f->output_vertices.size() * 2);

	// stores all of the verts in the world that are close to
	// being on a given edge
	std::vector<size_t> edge_verts;

	// find all of the extra vertices that lay on edges,
	// place them in superface
	for (size_t i = 0; i < f->output_vertices.size(); i++) {
		auto v1 = f->output_vertices[i];
		auto v2 = f->output_vertices[(i + 1) % f->output_vertices.size()];

		qvec3d edge_start = map.bsp.dvertexes[v1];
		qvec3d e2 = map.bsp.dvertexes[v2];

		edge_verts.clear();
		FindEdgeVerts_FaceBounds(headnode, edge_start, e2, edge_verts);

		vec_t len;
		qvec3d edge_dir = qv::normalize(e2 - edge_start, len);

		TestEdge(0, len, v1, v2, 0, edge_verts, edge_start, edge_dir, superface);
	}

	return superface;
}

/*
==================
RetopologizeFace

A face has T-junctions that can't be resolved from rotation.
It's still a convex face with wound vertices, though, so we
can split it into several triangle fans.
==================
*/
static std::list<std::vector<size_t>> RetopologizeFace(const std::vector<size_t> &vertices)
{
	std::list<std::vector<size_t>> result;
	// the copy we're working on
	std::vector<size_t> input(vertices);

	while (input.size()) {
		// failure if we somehow degenerated a triangle
		if (input.size() < 3) {
			return {};
		}

		size_t seed = 0;
		int64_t end = 0;

		// find seed triangle (allowing a wrap around,
		// because it's possible for only the last two triangles
		// to be valid).
		for (; seed < input.size(); seed++) {
			auto v0 = input[seed];
			auto v1 = input[(seed + 1) % input.size()];
			end = (seed + 2) % input.size();
			auto v2 = input[end];

			if (!TriangleIsValid(v0, v1, v2, input, 0.01)) {
				continue;
			}

			// if the next point lays on the edge of v0-v2, this next
			// triangle won't be valid.
			float len;
			qvec3d dir = qv::normalize(map.bsp.dvertexes[v0] - map.bsp.dvertexes[v2], len);
			auto dist = PointOnEdge(map.bsp.dvertexes[input[(end + 1) % input.size()]], map.bsp.dvertexes[v2], dir, 0, len);
			
			if (dist.has_value()) {
				continue;
			}

			// good one!
			break;
		}

		if (seed == input.size()) {
			// can't find a non-zero area triangle; failure
			return {};
		}

		// from the seed vertex, keep winding until we hit a zero-area triangle.
		// we know that triangle (seed, end - 1, end) is valid, so we wind from
		// end + 1 until we fully wrap around. We also can't include a triangle
		// that has a point after it laying on the final edge.
		size_t wrap = end;
		end = (end + 1) % input.size();

		for (; end != wrap; end = (end + 1) % input.size()) {
			auto v0 = input[seed];
			auto v1 = input[(end - 1) < 0 ? (input.size() - 1) : (end - 1)];
			auto v2 = input[end];

			// if the next point lays on the edge of v0-v2, this next
			// triangle won't be valid.
			float len;
			qvec3d dir = qv::normalize(map.bsp.dvertexes[v0] - map.bsp.dvertexes[v2], len);
			auto dist = PointOnEdge(map.bsp.dvertexes[input[(end + 1) % input.size()]], map.bsp.dvertexes[v2], dir, 0, len);

			if (dist.has_value()) {
				// the next point lays on this edge, so back up and stop
				end = (end - 1) < 0 ? input.size() - 1 : (end - 1);
				break;
			}
		}

		// now we have a fan from seed to end that is valid.
		// add it to the result, clip off all of the
		// points between it and restart the algorithm
		// using that edge.
		auto &tri = result.emplace_back();

		// the whole fan can just be moved; we're finished.
		if (seed == end) {
			tri = std::move(input);
			break;
		} else if (end == wrap) {
			// we successfully wrapped around, but the
			// seed vertex isn't at the start, so rotate it.
			// copy base -> end
			tri.resize(input.size());
			auto out = std::copy(input.begin() + seed, input.end(), tri.begin());
			// copy end -> base
			std::copy(input.begin(), input.begin() + seed, out);
			break;
		}
		
		if (end < seed) {
			// the end point is 'behind' the seed, so we're clipping
			// off two sides of the input
			size_t x = seed;
			bool first = true;

			while (true) {
				if (x == end && !first) {
					break;
				}

				tri.emplace_back(input[x]);
				x = (x + 1) % input.size();
				first = false;
			}
		} else {
			// simple case where the end point is ahead of the seed;
			// copy the range over to the output
			std::copy(input.begin() + seed, input.begin() + end + 1, std::back_inserter(tri));
		}

		Q_assert(seed != end);

		if (end < seed) {
			// slightly more complex case: the end point is behind the seed point.
			// 0 end 2 3 seed 5 6
			// end 2 3 seed
			// calculate new size
			size_t new_size = (seed + 1) - end;

			// move back the end to the start
			std::copy(input.begin() + end, input.begin() + seed + 1, input.begin());
			
			// clip
			input.resize(new_size);
		} else {
			// simple case: the end point is ahead of the seed point.
			// collapse the range after it backwards over top of the seed
			// and clip it off
			// 0 1 2 seed 4 5 6 end 8 9
			// 0 1 2 seed end 8 9
			// calculate new size
			size_t new_size = input.size() - (end - seed - 1);

			// move range
			std::copy(input.begin() + end, input.end(), input.begin() + seed + 1);
			
			// clip
			input.resize(new_size);
		}
	}

	// finished
	return result;
}

/*
==================
FixFaceEdges

If the face has any T-junctions, fix them here.
==================
*/
static void FixFaceEdges(node_t *headnode, face_t *f)
{
	std::vector<size_t> superface = CreateSuperFace(headnode, f);

	if (superface.size() < 3) {
		// entire face collapsed
		f->output_vertices.clear();
		c_facecollapse++;
		return;
	} else if (superface.size() == f->output_vertices.size()) {
		// face didn't need any new vertices
		return;
	}

	// brute force rotating the start point until we find a valid winding
	// that doesn't have any T-junctions
	size_t i = 0;

	for (; i < superface.size(); i++) {
		size_t x = 0;

		// try vertex i as the base, see if we find any zero-area triangles
		for (; x < superface.size() - 2; x++) {
			auto v0 = superface[i];
			auto v1 = superface[(i + x + 1) % superface.size()];
			auto v2 = superface[(i + x + 2) % superface.size()];

			if (!TriangleIsValid(v0, v1, v2, superface, 0.01)) {
				break;
			}
		}

		if (x == superface.size() - 2) {
			// found one!
			break;
		}
	}

	// temporary storage for result faces
	std::list<std::vector<size_t>> faces;

	if (i == superface.size()) {
		// can't simply rotate to eliminate zero-area triangles, so we have
		// to do a bit of re-topology.
		if (auto retopology = RetopologizeFace(superface); retopology.size() > 1) {
			c_retopology++;
			c_faceretopology += retopology.size() - 1;
			faces = std::move(retopology);
		} else {
			// unable to re-topologize, so just stick with the superface.
			// it's got zero-area triangles that fill in the gaps.
			c_norotates++;
			faces.emplace_back(std::move(superface));
		}
	} else if (i != 0) {
		// was able to rotate the superface to eliminate zero-area triangles.
		c_rotates++;

		auto &output = faces.emplace_back(superface.size());
		// copy base -> end
		auto out = std::copy(superface.begin() + i, superface.end(), output.begin());
		// copy end -> base
		std::copy(superface.begin(), superface.begin() + i, out);
	} else {
		// no need to change topology
		faces.emplace_back(std::move(superface));
	}

	Q_assert(faces.size());

	// split giant superfaces into subfaces
	if (qbsp_options.maxedges.value()) {
		for (auto &face : faces) {
			SplitFaceIntoFragments(face, faces);
		}
	}

	// move the results into the face
	f->output_vertices = std::move(faces.front());
	f->fragments.resize(faces.size() - 1);

	i = 0;
	for (auto it = ++faces.begin(); it != faces.end(); it++, i++) {
		f->fragments[i].output_vertices = std::move(*it);
	}
}

#include <common/parallel.hh>

/*
==================
FixEdges_r
==================
*/
static void FindFaces_r(node_t *node, std::unordered_set<face_t *> &faces)
{
	if (node->planenum == PLANENUM_LEAF) {
		return;
	}

	for (auto &f : node->facelist) {
		// might have been omitted earlier, so `output_vertices` will be empty
		if (f->output_vertices.size()) {
			faces.insert(f.get());
		}
	}
	
    FindFaces_r(node->children[0].get(), faces);
    FindFaces_r(node->children[1].get(), faces);
}

/*
===========
tjunc
===========
*/
void TJunc(node_t *headnode)
{
    logging::print(logging::flag::PROGRESS, "---- {} ----\n", __func__);

	// break edges on tjunctions
	c_degenerate = 0;
	c_facecollapse = 0;
	c_tjunctions = 0;
	c_faceoverflows = 0;
	c_norotates = 0;
	c_rotates = 0;
	c_retopology = 0;
	c_faceretopology = 0;

	std::unordered_set<face_t *> faces;

	FindFaces_r(headnode, faces);
	
	logging::parallel_for_each(faces, [&](auto &face) {
		FixFaceEdges(headnode, face);
	});

	logging::print (logging::flag::STAT, "{:5} edges degenerated\n", c_degenerate);
	logging::print (logging::flag::STAT, "{:5} faces degenerated\n", c_facecollapse);
	logging::print (logging::flag::STAT, "{:5} edges added by tjunctions\n", c_tjunctions);
	logging::print (logging::flag::STAT, "{:5} faces rotated\n", c_rotates);
	logging::print (logging::flag::STAT, "{:5} faces re-topologized\n", c_retopology);
	logging::print (logging::flag::STAT, "{:5} faces added by re-topology\n", c_faceretopology);
	logging::print (logging::flag::STAT, "{:5} faces added by splitting large faces\n", c_faceoverflows);
	logging::print (logging::flag::STAT, "{:5} faces unable to be rotated or re-topologized\n", c_norotates);
}
