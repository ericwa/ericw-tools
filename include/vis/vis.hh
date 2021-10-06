/*  Copyright (C) 1996-1997  Id Software, Inc.

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

// vis.h

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/polylib.hh>
#include <vis/leafbits.hh>

constexpr const char *PORTALFILE = "PRT1";
constexpr const char *PORTALFILE2 = "PRT2";
constexpr const char *PORTALFILEAM = "PRT1-AM";
constexpr vec_t ON_EPSILON = 0.1;
constexpr vec_t EQUAL_EPSILON = 0.001;

constexpr size_t MAX_WINDING_FIXED = 24;
constexpr size_t MAX_WINDING = 64;

enum pstatus_t
{
    pstat_none = 0,
    pstat_working,
    pstat_done
};

struct portal_t
{
    plane_t plane; // normal pointing into neighbor
    int leaf; // neighbor
    std::shared_ptr<struct winding_t> winding;
    pstatus_t status;
    leafbits_t visbits, mightsee;
    int nummightsee;
    int numcansee;
};

struct winding_t : polylib::winding_base_t<MAX_WINDING_FIXED>
{
    qvec3d origin; // Bounding sphere for fast clipping tests
    vec_t radius; // Not updated, so won't shrink when clipping

    using winding_base_t::winding_base_t;

    // copy constructor
    winding_t(const winding_t &copy) : origin(copy.origin), radius(copy.radius), winding_base_t(copy) { }

    // move constructor
    winding_t(winding_t &&move) : origin(move.origin), radius(move.radius), winding_base_t(move) { }

    // assignment copy
    inline winding_t &operator=(const winding_t &copy)
    {
        origin = copy.origin;
        radius = copy.radius;

        winding_base_t::operator=(copy);

        return *this;
    }

    // assignment move
    inline winding_t &operator=(winding_t &&move)
    {
        origin = move.origin;
        radius = move.radius;

        winding_base_t::operator=(move);

        return *this;
    }

    void SetWindingSphere()
    {
        // set origin
        origin = {};
        for (auto &point : *this)
            origin += point;
        origin /= size();

        // set radius
        radius = 0;
        for (auto &point : *this) {
            qvec3d dist = point - origin;
            radius = std::max(radius, qv::length(dist));
        }
    }

    /*
      ============================================================================
      Used for visdist to get the distance from a winding to a portal
      ============================================================================
    */
    float distFromPortal(portal_t *p)
    {
        vec_t mindist = 1e20;

        for (size_t i = 0; i < size(); ++i) {
            mindist = std::min(mindist, fabs(DotProduct(at(i), p->plane.normal) - p->plane.dist));
        }

        return mindist;
    }
};

struct sep_t
{
    sep_t *next;
    plane_t plane; // from portal is on positive side
};

struct passage_t
{
    passage_t *next;
    int from, to; // leaf numbers
    sep_t *planes;
};

/* Increased MAX_PORTALS_ON_LEAF from 128 */
constexpr size_t MAX_PORTALS_ON_LEAF = 512;

struct leaf_t
{
    int numportals;
    passage_t *passages;
    portal_t *portals[MAX_PORTALS_ON_LEAF];
};

constexpr size_t MAX_SEPARATORS = MAX_WINDING;
constexpr size_t STACK_WINDINGS = 3; // source, pass and a temp for clipping

struct pstack_t
{
    pstack_t *next;
    leaf_t *leaf;
    portal_t *portal; // portal exiting
    std::shared_ptr<winding_t> source, pass;
    std::shared_ptr<winding_t> windings[STACK_WINDINGS]; // Fixed size windings
    plane_t portalplane;
    leafbits_t *mightsee; // bit string
    plane_t separators[2][MAX_SEPARATORS]; /* Separator cache */
    int numseparators[2];
};

std::shared_ptr<winding_t> &AllocStackWinding(pstack_t *stack);
void FreeStackWinding(std::shared_ptr<winding_t> &w, pstack_t *stack);
std::shared_ptr<winding_t> ClipStackWinding(std::shared_ptr<winding_t> &in, pstack_t *stack, plane_t *split);

struct threaddata_t
{
    leafbits_t &leafvis;
    portal_t *base;
    pstack_t pstack_head;
};

extern int numportals;
extern int portalleafs;
extern int portalleafs_real;

extern portal_t *portals;
extern leaf_t *leafs;

extern int c_noclip;
extern int c_portaltest, c_portalpass, c_portalcheck;
extern int c_vistest, c_mighttest;
extern unsigned long c_chains;

extern bool showgetleaf;

extern int testlevel;
extern bool ambientsky;
extern bool ambientwater;
extern bool ambientslime;
extern bool ambientlava;
extern int visdist;
extern bool nostate;

extern uint8_t *uncompressed;
extern int leafbytes;
extern int leafbytes_real;
extern int leaflongs;

extern std::filesystem::path sourcefile, portalfile, statefile, statetmpfile;

void BasePortalVis(void);

void PortalFlow(portal_t *p);

void CalcAmbientSounds(mbsp_t *bsp);

void CalcPHS(mbsp_t *bsp);

extern time_point starttime, endtime, statetime;

void SaveVisState(void);
bool LoadVisState(void);

void DecompressRow(const uint8_t *in, const int numbytes, uint8_t *decompressed);
int CompressRow(const uint8_t *vis, const int numbytes, uint8_t *out);
