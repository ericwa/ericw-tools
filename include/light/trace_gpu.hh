/* GPU trace backend
 * Prototype overlay generated for Linux/Vulkan ray-query development.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

struct mbsp_t;
class modelinfo_t;

#ifndef HAVE_GPU_LIGHT
#define GPU_LIGHT_COMPILED 0
#else
#define GPU_LIGHT_COMPILED 1
#endif

namespace gpu_light {

struct ray_t {
    float origin[3] = {0, 0, 0};
    float tmin = 0.01f;
    float direction[3] = {0, 0, 1};
    float tmax = 0.0f;
    std::uint32_t shadow_mask = 0xffffffffu;
    std::uint32_t user_index = 0;
};

struct occlusion_result_t {
    std::uint32_t occluded = 0;
    std::uint32_t reserved0 = 0;
    float transmittance[3] = {1.0f, 1.0f, 1.0f};
};

struct direct_job_t {
    float ox = 0, oy = 0, oz = 0, tmin = 0.01f;
    float dx = 0, dy = 0, dz = 1, tmax = 0.0f;
    float cr = 0, cg = 0, cb = 0, pad0 = 0;
    float nr = 0, ng = 0, nb = 0, pad1 = 0;
    std::uint32_t sample_index = 0;
    std::uint32_t flags = 0;
    std::uint32_t reserved0 = 0;
    std::uint32_t reserved1 = 0;
};

struct direct_sample_range_t {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

struct direct_accum_t {
    float cr = 0, cg = 0, cb = 0, pad0 = 0;
    float nr = 0, ng = 0, nb = 0, pad1 = 0;
    std::uint32_t hit = 0;
    std::uint32_t reserved0 = 0;
    std::uint32_t reserved1 = 0;
    std::uint32_t reserved2 = 0;
};

struct direct_phase_sample_t {
    float px = 0, py = 0, pz = 0, occlusion = 1;
    float nx = 0, ny = 0, nz = 1, twosided = 0;
};

struct direct_phase_source_t {
    float px = 0, py = 0, pz = 0, light = 0;
    float dx = 0, dy = 0, dz = 1, dist = 65536.0f;
    float cr = 1, cg = 1, cb = 1, atten = 1;
    std::uint32_t type = 0;      // 0 = point, 1 = sun
    std::uint32_t formula = 0;   // light_formula_t for point lights
    std::uint32_t flags = 0;     // bit 0: dirt
    std::uint32_t reserved0 = 0;
    float anglescale = 1;
    float dirt = 0;
    float falloff = 0;
    float pad0 = 0;
};

using direct_phase_accum_t = direct_accum_t;


enum class backend_state_t {
    unavailable,
    initialized,
    failed
};

struct stats_t {
    std::uint64_t batches = 0;
    std::uint64_t rays = 0;
    std::uint64_t gpu_batches = 0;
    std::uint64_t fallback_batches = 0;
};

bool requested();
backend_state_t state();
const char *state_string();
const char *last_error();
stats_t stats();

bool init(const mbsp_t *bsp);
void shutdown();

// Returns true when the batch was handled by the GPU backend. Returns false to
// tell the caller to run the existing CPU/Embree path.
bool trace_occlusion_batch(
    const modelinfo_t *self,
    std::uint32_t shadow_mask,
    const ray_t *rays,
    occlusion_result_t *results,
    std::size_t count);


bool trace_direct_phase_batch(
    const direct_phase_source_t *sources,
    std::size_t source_count,
    const direct_phase_sample_t *samples,
    direct_phase_accum_t *accum,
    std::size_t sample_count);

bool trace_direct_accumulate_batch(
    const modelinfo_t *self,
    std::uint32_t shadow_mask,
    const direct_job_t *jobs,
    std::size_t job_count,
    const direct_sample_range_t *ranges,
    direct_accum_t *accum,
    std::size_t sample_count);

} // namespace gpu_light

bool GPU_TraceInit(const mbsp_t *bsp);
void GPU_TraceShutdown();
bool GPU_TraceAvailable();
const char *GPU_TraceLastError();

// Flushes pending sample-driven direct-light work.
void GPU_DirectQueue_Flush(const mbsp_t *bsp);
