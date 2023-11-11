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

#pragma once

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/polylib.hh>
#include <common/prtfile.hh>
#include <vis/leafbits.hh>

constexpr vec_t VIS_ON_EPSILON = 0.1;
constexpr vec_t VIS_EQUAL_EPSILON = 0.001;

constexpr size_t MAX_WINDING_FIXED = 24;
constexpr size_t MAX_WINDING = 64;

enum pstatus_t
{
    pstat_none = 0,
    pstat_working,
    pstat_done
};

/**
 * 3D polygon with bounding sphere
 *
 * Can be used in 2 modes:
 *
 * - stack allocated. Only holds up to MAX_WINDING_FIXED points. No constructor, user is responsible for initializing
 *   all fields
 *
 * - heap allocated, via new_heap_winding or copy_polylib_winding
 */
struct viswinding_t
{
    qvec3d origin; // Bounding sphere for fast clipping tests
    vec_t radius; // Not updated, so won't shrink when clipping

    size_t numpoints;
    qvec3d points[MAX_WINDING_FIXED];

    // heap allocated mode

    struct viswinding_deleter_t {
        void operator()(viswinding_t *ptr) {
            free(ptr);
        }
    };

    using unique_ptr = std::unique_ptr<viswinding_t, viswinding_deleter_t>;

    static inline unique_ptr new_heap_winding(int size) {
        const size_t bytes = offsetof(viswinding_t, points) + sizeof(qvec3d) * size;

        viswinding_t *result = static_cast<viswinding_t *>(malloc(bytes));
        result->numpoints = size;

        return unique_ptr(result,viswinding_deleter_t());
    }

    template <class W>
    static inline unique_ptr copy_polylib_winding(const W &other) {
        auto result = new_heap_winding(other.size());

        for (size_t i = 0; i < other.size(); ++i)
            result->points[i] = other[i];

        result->set_winding_sphere();
        return result;
    }

    // getters

    inline qvec3d const &at(size_t index) const {
        return points[index];
    }
    inline qvec3d const &operator[](size_t index) const {
        return points[index];
    }
    inline size_t size() const { return numpoints; }

    inline void push_back(const qvec3d &v) {
        points[numpoints++] = v;
    }

    // utils

    // sets origin & radius
    inline void set_winding_sphere()
    {
        // set origin
        origin = {};
        for (size_t i = 0; i < numpoints; ++i)
            origin += points[i];
        origin /= size();

        // set radius
        radius = 0;
        for (size_t i = 0; i < numpoints; ++i) {
            const auto &point = points[i];
            qvec3d dist = point - origin;
            radius = std::max(radius, qv::length(dist));
        }
    }

    /*
      ============================================================================
      Used for visdist to get the distance from a winding to a portal
      ============================================================================
    */
    inline float distFromPortal(struct visportal_t &p);
};

static_assert(std::is_trivially_default_constructible_v<viswinding_t>);

struct visportal_t
{
    qplane3d plane; // normal pointing into neighbor
    int leaf; // neighbor
    viswinding_t::unique_ptr winding;
    pstatus_t status;
    leafbits_t visbits, mightsee;
    int nummightsee;
    int numcansee;
};

inline float viswinding_t::distFromPortal(visportal_t &p)
{
    vec_t mindist = 1e20;

    for (size_t i = 0; i < size(); ++i) {
        mindist = std::min(mindist, fabs(p.plane.distance_to(at(i))));
    }

    return mindist;
}

struct leaf_t
{
    std::vector<visportal_t *> portals;
};

constexpr size_t MAX_SEPARATORS = MAX_WINDING;
constexpr size_t STACK_WINDINGS = 3; // source, pass and a temp for clipping

struct pstack_t
{
    pstack_t *next;
    leaf_t *leaf;
    visportal_t *portal; // portal exiting
    viswinding_t *source, *pass;
    viswinding_t windings[STACK_WINDINGS]; // Fixed size windings
    bool windings_used[STACK_WINDINGS];
    qplane3d portalplane;
    leafbits_t *mightsee; // bit string
    qplane3d separators[2][MAX_SEPARATORS]; /* Separator cache */
    int numseparators[2];
};

// important for perf as a ton of these are stack allocated, needs to be be just a pointer bump
static_assert(std::is_trivially_default_constructible_v<pstack_t>);

struct visstats_t
{
    int64_t c_portaltest = 0;
    int64_t c_portalpass = 0;
    int64_t c_portalcheck = 0;
    int64_t c_mightseeupdate = 0;
    int64_t c_noclip = 0;
    int64_t c_vistest = 0;
    int64_t c_mighttest = 0;
    int64_t c_chains = 0;
    int64_t c_leafskip = 0;
    int64_t c_portalskip = 0;

    visstats_t operator+(const visstats_t& other) const {
        visstats_t result;
        result.c_portaltest = this->c_portaltest + other.c_portaltest;
        result.c_portalpass = this->c_portalpass + other.c_portalpass;
        result.c_portalcheck = this->c_portalcheck + other.c_portalcheck;
        result.c_mightseeupdate = this->c_mightseeupdate + other.c_mightseeupdate;
        result.c_noclip = this->c_noclip + other.c_noclip;
        result.c_vistest = this->c_vistest + other.c_vistest;
        result.c_mighttest = this->c_mighttest + other.c_mighttest;
        result.c_chains = this->c_chains + other.c_chains;
        result.c_leafskip = this->c_leafskip + other.c_leafskip;
        result.c_portalskip = this->c_portalskip + other.c_portalskip;
        return result;
    }
};

viswinding_t *AllocStackWinding(pstack_t &stack);
void FreeStackWinding(viswinding_t *&w, pstack_t &stack);
viswinding_t *ClipStackWinding(visstats_t &stats, viswinding_t *in, pstack_t &stack, const qplane3d &split);

struct threaddata_t
{
    leafbits_t &leafvis;
    visportal_t *base;
    pstack_t pstack_head;
    visstats_t stats;
};

extern int numportals;
extern int portalleafs;
extern int portalleafs_real;

extern std::vector<visportal_t> portals; // always numportals * 2; front and back
extern std::vector<leaf_t> leafs;

extern std::vector<uint8_t> uncompressed;
extern int leafbytes;
extern int leafbytes_real;
extern int leaflongs;

extern fs::path portalfile, statefile, statetmpfile;

void BasePortalVis(void);

visstats_t PortalFlow(visportal_t *p);

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
extern setting_group vis_output_group;
extern setting_group vis_advanced_group;

class vis_settings : public common_settings
{
public:
    setting_bool fast{this, "fast", false, &performance_group, "run very simple & fast vis procedure"};
    setting_int32 level{this, "level", 4, 0, 4, &vis_advanced_group, "number of iterations for tests"};
    setting_bool noambientsky{this, "noambientsky", false, &vis_output_group, "don't output ambient sky sounds"};
    setting_bool noambientwater{this, "noambientwater", false, &vis_output_group, "don't output ambient water sounds"};
    setting_bool noambientslime{this, "noambientslime", false, &vis_output_group, "don't output ambient slime sounds"};
    setting_bool noambientlava{this, "noambientlava", false, &vis_output_group, "don't output ambient lava sounds"};
    setting_redirect noambient{this, "noambient", {&noambientsky, &noambientwater, &noambientslime, &noambientlava},
        &vis_output_group, "don't output ambient sounds at all"};
    setting_scalar visdist{
        this, "visdist", 0.0, &vis_advanced_group, "control the distance required for a portal to be considered seen"};
    setting_bool nostate{this, "nostate", false, &vis_advanced_group, "ignore saved state files, for forced re-runs"};
    setting_bool phsonly{
        this, "phsonly", false, &vis_advanced_group, "re-calculate the PHS of a Quake II BSP without touching the PVS"};
    setting_invertible_bool autoclean{
        this, "autoclean", true, &vis_output_group, "remove any extra files on successful completion"};

    fs::path sourceMap;

    void set_parameters(int argc, const char **argv) override
    {
        common_settings::set_parameters(argc, argv);
        program_description = "vis calculates the visibility (and hearability) sets for \n.BSP files.\n\n";
        remainder_name = "mapname.bsp";
    }
    void initialize(int argc, const char **argv) override;
};

} // namespace settings

extern settings::vis_settings vis_options;

int vis_main(int argc, const char **argv);
int vis_main(const std::vector<std::string> &args);
