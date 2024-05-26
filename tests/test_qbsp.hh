#include <common/bspfile.hh>
#include <common/prtfile.hh>
#include <common/litfile.hh>

#include <string>
#include <vector>
#include <map>
#include <variant>

class mapbrush_t;
struct mapface_t;
class mapentity_t;

const mapface_t *Mapbrush_FirstFaceWithTextureName(const mapbrush_t &brush, const std::string &texname);
mapentity_t &LoadMap(const char *map);
mapentity_t &LoadMapPath(const std::filesystem::path &name);
std::tuple<mbsp_t, bspxentries_t, std::optional<prtfile_t>> LoadTestmap(
    const std::filesystem::path &name, std::vector<std::string> extra_args = {});
std::tuple<mbsp_t, bspxentries_t, std::optional<prtfile_t>> LoadTestmapQ2(
    const std::filesystem::path &name, std::vector<std::string> extra_args = {});
std::tuple<mbsp_t, bspxentries_t, std::optional<prtfile_t>> LoadTestmapQ1(
    const std::filesystem::path &name, std::vector<std::string> extra_args = {});
void CheckFilled(const mbsp_t &bsp, hull_index_t hullnum);
void CheckFilled(const mbsp_t &bsp);
std::map<std::string, std::vector<const mface_t *>> MakeTextureToFaceMap(const mbsp_t &bsp);
const texvecf &GetTexvecs(const char *map, const char *texname);
std::vector<std::string> TexNames(const mbsp_t &bsp, std::vector<const mface_t *> faces);
std::vector<const mface_t *> FacesWithTextureName(const mbsp_t &bsp, const std::string &name);
std::map<int, int> CountClipnodeLeafsByContentType(const mbsp_t &bsp, int hullnum);
int CountClipnodeNodes(const mbsp_t &bsp, int hullnum);
bool PortalMatcher(const prtfile_winding_t &a, const prtfile_winding_t &b);
std::map<int, int> CountClipnodeLeafsByContentType(const mbsp_t &bsp, int hullnum);
int CountClipnodeNodes(const mbsp_t &bsp, int hullnum);

struct testresults_t
{
    mbsp_t bsp;
    bspxentries_t bspx;
};

struct testresults_lit_t
{
    mbsp_t bsp;
    bspxentries_t bspx;
    lit_variant_t lit;
};

enum class runvis_t
{
    no,
    yes
};

testresults_lit_t QbspVisLight_Q1(
    const std::filesystem::path &name, std::vector<std::string> extra_light_args, runvis_t run_vis = runvis_t::no);
testresults_t QbspVisLight_Q2(
    const std::filesystem::path &name, std::vector<std::string> extra_light_args, runvis_t run_vis = runvis_t::no);
testresults_t QbspVisLight_HL(
        const std::filesystem::path &name, std::vector<std::string> extra_light_args, runvis_t run_vis = runvis_t::no);
