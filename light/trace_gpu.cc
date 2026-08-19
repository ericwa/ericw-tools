#include <light/trace_gpu.hh>

#include <atomic>
#include <mutex>
#include <string>

#if defined(HAVE_GPU_LIGHT)
namespace gpu_light::vulkan_backend {
bool init(const mbsp_t *bsp, std::string &error);
void shutdown();
bool trace_occlusion_batch(
    const modelinfo_t *self,
    std::uint32_t shadow_mask,
    const gpu_light::ray_t *rays,
    gpu_light::occlusion_result_t *results,
    std::size_t count,
    std::string &error);

bool trace_direct_phase_batch(
    const gpu_light::direct_phase_source_t *sources,
    std::size_t source_count,
    const gpu_light::direct_phase_sample_t *samples,
    gpu_light::direct_phase_accum_t *accum,
    std::size_t sample_count,
    const gpu_light::direct_phase_face_range_t *face_ranges,
    std::size_t face_range_count,
    const std::uint32_t *face_source_indices,
    std::size_t face_source_index_count,
    std::string &error);

bool trace_direct_accumulate_batch(
    const modelinfo_t *self,
    std::uint32_t shadow_mask,
    const gpu_light::direct_job_t *jobs,
    std::size_t job_count,
    const gpu_light::direct_sample_range_t *ranges,
    gpu_light::direct_accum_t *accum,
    std::size_t sample_count,
    std::string &error);
} // namespace gpu_light::vulkan_backend
#endif

namespace gpu_light {
namespace {
std::mutex g_mutex;
backend_state_t g_state = backend_state_t::unavailable;
std::string g_last_error;
stats_t g_stats;
} // namespace

bool requested() {
    // Keeping this function independent avoids pulling all light settings into this TU.
    return true;
}

backend_state_t state() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state;
}

const char *state_string() {
    switch (state()) {
    case backend_state_t::unavailable: return "unavailable";
    case backend_state_t::initialized: return "initialized";
    case backend_state_t::failed: return "failed";
    }
    return "unknown";
}

const char *last_error() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_last_error.c_str();
}

stats_t stats() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_stats;
}

bool init(const mbsp_t *bsp) {
    std::lock_guard<std::mutex> lock(g_mutex);
#if defined(HAVE_GPU_LIGHT)
    g_last_error.clear();
    if (vulkan_backend::init(bsp, g_last_error)) {
        g_state = backend_state_t::initialized;
        return true;
    }
    g_state = backend_state_t::failed;
    return false;
#else
    (void)bsp;
    g_last_error = "light was built without LIGHT_ENABLE_VULKAN_GPU=ON";
    g_state = backend_state_t::unavailable;
    return false;
#endif
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
#if defined(HAVE_GPU_LIGHT)
    vulkan_backend::shutdown();
#endif
    g_state = backend_state_t::unavailable;
}

bool trace_occlusion_batch(
    const modelinfo_t *self,
    std::uint32_t shadow_mask,
    const ray_t *rays,
    occlusion_result_t *results,
    std::size_t count) {
    if (!rays || !results || count == 0) {
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stats.batches++;
        g_stats.rays += count;
        if (g_state != backend_state_t::initialized) {
            g_stats.fallback_batches++;
            return false;
        }
    }

#if defined(HAVE_GPU_LIGHT)
    std::string error;
    const bool ok = vulkan_backend::trace_occlusion_batch(self, shadow_mask, rays, results, count, error);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (ok) {
        g_stats.gpu_batches++;
        return true;
    }
    g_stats.fallback_batches++;
    if (!error.empty()) {
        g_last_error = error;
    }
    return false;
#else
    (void)self;
    (void)shadow_mask;
    return false;
#endif
}


bool trace_direct_phase_batch(
    const direct_phase_source_t *sources,
    std::size_t source_count,
    const direct_phase_sample_t *samples,
    direct_phase_accum_t *accum,
    std::size_t sample_count,
    const direct_phase_face_range_t *face_ranges,
    std::size_t face_range_count,
    const std::uint32_t *face_source_indices,
    std::size_t face_source_index_count) {
    if (!sources || !samples || !accum || !face_ranges || !face_source_indices || source_count == 0 || sample_count == 0 || face_range_count == 0) {
        return true;
    }

    std::uint64_t implicit_rays = 0;
    for (std::size_t i = 0; i < face_range_count; ++i) {
        implicit_rays += face_ranges[i].source_count;
    }
    if (implicit_rays == 0 || face_source_index_count == 0) {
        return true;
    }
    implicit_rays *= static_cast<std::uint64_t>(sample_count) / static_cast<std::uint64_t>(face_range_count);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stats.batches++;
        g_stats.rays += implicit_rays;
        if (g_state != backend_state_t::initialized) {
            g_stats.fallback_batches++;
            return false;
        }
    }

#if defined(HAVE_GPU_LIGHT)
    std::string error;
    const bool ok = vulkan_backend::trace_direct_phase_batch(
        sources, source_count, samples, accum, sample_count,
        face_ranges, face_range_count, face_source_indices, face_source_index_count, error);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (ok) {
        g_stats.gpu_batches++;
        return true;
    }
    g_stats.fallback_batches++;
    if (!error.empty()) {
        g_last_error = error;
    }
    return false;
#else
    return false;
#endif
}


bool trace_direct_accumulate_batch(
    const modelinfo_t *self,
    std::uint32_t shadow_mask,
    const direct_job_t *jobs,
    std::size_t job_count,
    const direct_sample_range_t *ranges,
    direct_accum_t *accum,
    std::size_t sample_count) {
    if (!jobs || !ranges || !accum || job_count == 0 || sample_count == 0) {
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stats.batches++;
        g_stats.rays += job_count;
        if (g_state != backend_state_t::initialized) {
            g_stats.fallback_batches++;
            return false;
        }
    }

#if defined(HAVE_GPU_LIGHT)
    std::string error;
    const bool ok = vulkan_backend::trace_direct_accumulate_batch(
        self, shadow_mask, jobs, job_count, ranges, accum, sample_count, error);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (ok) {
        g_stats.gpu_batches++;
        return true;
    }
    g_stats.fallback_batches++;
    if (!error.empty()) {
        g_last_error = error;
    }
    return false;
#else
    (void)self;
    (void)shadow_mask;
    return false;
#endif
}

} // namespace gpu_light

bool GPU_TraceInit(const mbsp_t *bsp) { return gpu_light::init(bsp); }
void GPU_TraceShutdown() { gpu_light::shutdown(); }
bool GPU_TraceAvailable() { return gpu_light::state() == gpu_light::backend_state_t::initialized; }
const char *GPU_TraceLastError() { return gpu_light::last_error(); }
