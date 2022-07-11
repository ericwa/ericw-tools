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

size_t c_degenerate;
size_t c_tjunctions;
size_t c_faceoverflows;
size_t c_facecollapse;
size_t c_badstartverts;

/*
==========
TestEdge

Can be recursively reentered
==========
*/
inline void TestEdge (vec_t start, vec_t end, size_t p1, size_t p2, size_t startvert, const std::vector<size_t> &edge_verts, const qvec3d &edge_start, const qvec3d &edge_dir, std::vector<size_t> &superface)
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

		const qvec3d &p = map.bsp.dvertexes[j];
		qvec3d delta = p - edge_start;
		vec_t dist = qv::dot(delta, edge_dir);

		// check if off an end
		if (dist <= start || dist >= end) {
			continue;
		}

		qvec3d exact = edge_start + (edge_dir * dist);
		qvec3d off = p - exact;
		vec_t error = qv::length(off);

		// brushbsp-fixme: this was 0.5 in Q2, check?
		if (fabs(error) > DEFAULT_ON_EPSILON) {
			continue;		// not on the edge
		}

		// break the edge
		c_tjunctions++;
		TestEdge (start, dist, p1, j, k + 1, edge_verts, edge_start, edge_dir, superface);
		TestEdge (dist, end, j, p2, k + 1, edge_verts, edge_start, edge_dir, superface);
		return;
	}

	// the edge p1 to p2 is now free of tjunctions
	superface.push_back(p1);
}

/*
==========
FindEdgeVerts

Forced a dumb check of everything
==========
*/
static void FindEdgeVerts(const qvec3d &, const qvec3d &, std::vector<size_t> &verts)
{
	verts.resize(map.bsp.dvertexes.size() - 1);

	for (size_t i = 0; i < verts.size(); i++) {
		verts[i] = i + 1;
	}
}

/*
==================
FaceFromSuperverts

The faces vertexes have been added to the superverts[] array,
and there may be more there than can be held in a face (MAXEDGES).

If less, the faces vertexnums[] will be filled in, otherwise
face will reference a tree of split[] faces until all of the
vertexnums can be added.

superverts[base] will become face->vertexnums[0], and the others
will be circularly filled in.
==================
*/
inline void FaceFromSuperverts(node_t *node, face_t *f, size_t base, const std::vector<size_t> &superface)
{
	size_t remaining = superface.size();

	// don't need to do any work
	if (remaining <= MAXEDGES) {
		return;
	}

	// split into multiple fragments, because of vertex overload
	while (remaining > MAXEDGES) {
		c_faceoverflows++;

		auto &newf = f->fragments.emplace_back();

		newf.output_vertices.resize(MAXEDGES);

		for (size_t i = 0; i < MAXEDGES; i++) {
			newf.output_vertices[i] = superface[(i + base) % superface.size()];
		}

		remaining -= (MAXEDGES - 2);
		base = (base + MAXEDGES - 1) % superface.size();
	}

	// copy the vertexes back to the face
	f->w.resize(remaining);

	for (size_t i = 0; i < remaining; i++) {
		f->output_vertices[i] = superface[(i + base) % superface.size()];
	}
}

/*
==================
FixFaceEdges
==================
*/
static void FixFaceEdges (node_t *node, face_t *f)
{
	std::vector<size_t> count, start;
	std::vector<size_t> superface;
	superface.reserve(64);
	std::vector<size_t> edge_verts;

	// brushbsp-fixme
	//if (f->merged || f->split[0] || f->split[1])
	//	return;

	for (size_t i = 0; i < f->w.size(); i++) {
		auto &p1 = f->w[i];
		auto &p2 = f->w[(i + 1) % f->w.size()];

		qvec3d edge_start = p1;
		qvec3d e2 = p2;

		FindEdgeVerts (edge_start, e2, edge_verts);

		vec_t len;
		qvec3d edge_dir = qv::normalize(e2 - edge_start, len);

		start.push_back(superface.size());
		TestEdge(0, len, f->output_vertices[i], f->output_vertices[(i + 1) % f->w.size()], 0, edge_verts, edge_start, edge_dir, superface);

		count.push_back(superface.size() - start[i]);
	}

	if (superface.size() < 3) {
		// entire face collapsed
		f->w.clear();
		c_facecollapse++;
		return;
	}

	// we want to pick a vertex that doesn't have tjunctions
	// on either side, which can cause artifacts on trifans,
	// especially underwater
	size_t i = 0;

	for (; i < f->w.size(); i++) {
		if (count[i] == 1 && count[(i + f->w.size() - 1) % f->w.size()] == 1) {
			break;
		}
	}

	size_t base;

	if (i == f->w.size()) {
		c_badstartverts++;
		base = 0;
	} else {
		// rotate the vertex order
		base = start[i];
	}

	// this may fragment the face if > MAXEDGES
	FaceFromSuperverts(node, f, base, superface);
}

/*
==================
FixEdges_r
==================
*/
static void FixEdges_r(node_t *node)
{
	if (node->planenum == PLANENUM_LEAF) {
		return;
	}

	for (auto &f : node->facelist) {
		// might have been omitted earlier, so `output_vertices` will be empty
		if (f->output_vertices.size()) {
			FixFaceEdges(node, f.get());
		}
	}
	
	FixEdges_r(node->children[0].get());
	FixEdges_r(node->children[1].get());
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
	c_badstartverts = 0;

	FixEdges_r (headnode);

	logging::print (logging::flag::STAT, "{:5} edges degenerated\n", c_degenerate);
	logging::print (logging::flag::STAT, "{:5} faces degenerated\n", c_facecollapse);
	logging::print (logging::flag::STAT, "{:5} edges added by tjunctions\n", c_tjunctions);
	logging::print (logging::flag::STAT, "{:5} faces added by tjunctions\n", c_faceoverflows);
	logging::print (logging::flag::STAT, "{:5} bad start verts\n", c_badstartverts);
}
