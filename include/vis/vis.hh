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
#include <common/prtfile.hh>
#include <vis/leafbits.hh>

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

struct winding_t : polylib::winding_base_t<MAX_WINDING_FIXED>
{
    qvec3d origin; // Bounding sphere for fast clipping tests
    vec_t radius; // Not updated, so won't shrink when clipping

    inline winding_t() : polylib::winding_base_t<MAX_WINDING_FIXED>() { }

    // construct winding from range.
    // iterators must have operator+ and operator-.
    template<typename Iter, std::enable_if_t<is_iterator_v<Iter>, int> = 0>
    inline winding_t(Iter begin, Iter end) : polylib::winding_base_t<MAX_WINDING_FIXED>(begin, end)
    {
        set_winding_sphere();
    }

    // initializer list constructor
    inline winding_t(std::initializer_list<qvec3d> l) : polylib::winding_base_t<MAX_WINDING_FIXED>(l)
    {
        set_winding_sphere();
    }

    // copy constructor
    inline winding_t(const winding_t &copy) : winding_base_t(copy), origin(copy.origin), radius(copy.radius) { }

    // move constructor
    inline winding_t(winding_t &&move) noexcept : winding_base_t(move), origin(move.origin), radius(move.radius) { }

    // sets origin & radius
    inline void set_winding_sphere()
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

    // assignment copy
    inline winding_t &operator=(const winding_t &copy)
    {
        origin = copy.origin;
        radius = copy.radius;

        winding_base_t::operator=(copy);

        return *this;
    }

    // assignment move
    inline winding_t &operator=(winding_t &&move) noexcept
    {
        origin = move.origin;
        radius = move.radius;

        winding_base_t::operator=(move);

        return *this;
    }

    /*
      ============================================================================
      Used for visdist to get the distance from a winding to a portal
      ============================================================================
    */
    inline float distFromPortal(struct portal_t &p);
};

struct portal_t
{
    qplane3d plane; // normal pointing into neighbor
    int leaf; // neighbor
    winding_t winding;
    pstatus_t status;
    leafbits_t visbits, mightsee;
    int nummightsee;
    int numcansee;
};

inline float winding_t::distFromPortal(portal_t &p)
{
    vec_t mindist = 1e20;

    for (size_t i = 0; i < size(); ++i) {
        mindist = std::min(mindist, fabs(p.plane.distance_to(at(i))));
    }

    return mindist;
}

struct sep_t
{
    sep_t *next;
    qplane3d plane; // from portal is on positive side
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
    winding_t *source, *pass;
    winding_t windings[STACK_WINDINGS]; // Fixed size windings
    bool windings_used[STACK_WINDINGS];
    qplane3d portalplane;
    leafbits_t *mightsee; // bit string
    qplane3d separators[2][MAX_SEPARATORS]; /* Separator cache */
    int numseparators[2];
};

winding_t *AllocStackWinding(pstack_t &stack);
void FreeStackWinding(winding_t *&w, pstack_t &stack);
winding_t *ClipStackWinding(winding_t *in, pstack_t &stack, const qplane3d &split);

struct threaddata_t
{
    leafbits_t &leafvis;
    portal_t *base;
    pstack_t pstack_head;
};

extern int numportals;
extern int portalleafs;
extern int portalleafs_real;

extern std::vector<portal_t> portals; // always numportals * 2; front and back
extern std::vector<leaf_t> leafs;

extern int c_noclip;
extern int c_portaltest, c_portalpass, c_portalcheck;
extern int c_vistest, c_mighttest;
extern unsigned long c_chains;

extern bool showgetleaf;

extern std::vector<uint8_t> uncompressed;
extern int leafbytes;
extern int leafbytes_real;
extern int leaflongs;

extern fs::path portalfile, statefile, statetmpfile;

void BasePortalVis(void);

void PortalFlow(portal_t *p);

void CalcAmbientSounds(mbsp_t *bsp);

void CalcPHS(mbsp_t *bsp);

extern time_point starttime, endtime, statetime;

void SaveVisState(void);
bool LoadVisState(void);
void CleanVisState(void);

#include <common/settings.hh>
#include <common/fs.hh>

namespace settings
{
extern setting_group output_group;
extern setting_group advanced_group;

class vis_settings : public common_settings
{
public:
    setting_bool fast{this, "fast", false, &performance_group, "run very simple & fast vis procedure"};
    setting_int32 level{this, "level", 4, 0, 4, &advanced_group, "number of iterations for tests"};
    setting_bool noambientsky{this, "noambientsky", false, &output_group, "don't output ambient sky sounds"};
    setting_bool noambientwater{this, "noambientwater", false, &output_group, "don't output ambient water sounds"};
    setting_bool noambientslime{this, "noambientslime", false, &output_group, "don't output ambient slime sounds"};
    setting_bool noambientlava{this, "noambientlava", false, &output_group, "don't output ambient lava sounds"};
    setting_redirect noambient{this, "noambient", {&noambientsky, &noambientwater, &noambientslime, &noambientlava},
        &output_group, "don't output ambient sounds at all"};
    setting_scalar visdist{
        this, "visdist", 0.0, &advanced_group, "control the distance required for a portal to be considered seen"};
    setting_bool nostate{this, "nostate", false, &advanced_group, "ignore saved state files, for forced re-runs"};
    setting_bool phsonly{
        this, "phsonly", false, &advanced_group, "re-calculate the PHS of a Quake II BSP without touching the PVS"};
    setting_invertible_bool autoclean{
        this, "autoclean", true, &output_group, "remove any extra files on successful completion"};

    fs::path sourceMap;

    void setParameters(int argc, const char **argv) override
    {
        common_settings::setParameters(argc, argv);
        programDescription = "vis calculates the visibility (and hearability) sets for \n.BSP files.\n\n";
        remainderName = "mapname.bsp";
    }
    void initialize(int argc, const char **argv) override;
};

} // namespace settings

extern settings::vis_settings vis_options;

int vis_main(int argc, const char **argv);
