#include <light/trace_gpu.hh>

#if defined(HAVE_GPU_LIGHT)

#include <common/bspfile.hh>
#include <common/log.hh>
#include <light/light.hh>
#include <light/trace_embree.hh>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

namespace gpu_light::vulkan_backend {
namespace {

struct buffer_t {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

struct as_t {
    VkAccelerationStructureKHR as = VK_NULL_HANDLE;
    buffer_t storage;
    VkDeviceAddress address = 0;
};

// host-visible buffer kept alive and persistently mapped across flushes
struct persistent_buffer_t {
    buffer_t buf;
    void *mapped = nullptr;
    VkDeviceSize capacity = 0;
};

// One in-flight unit for the direct phase. Two slots let one thread upload and
// read back batch N+1 while another thread's batch N executes on the GPU, and
// keep flush threads from blocking each other on a shared command buffer.
struct direct_slot_t {
    persistent_buffer_t samples;
    persistent_buffer_t sources;
    persistent_buffer_t face_ranges;
    persistent_buffer_t face_source_indices;
    persistent_buffer_t accum;
    // surflight-kernel buffers. Only `samples` is shared with the direct
    // kernel: the combined direct-phase batch dispatches both kernels against
    // one sample upload, so the surflight side needs its own ranges/indices/
    // accum (the two kernels carry different per-face source lists).
    persistent_buffer_t surflight_sources;
    persistent_buffer_t surflight_points;
    persistent_buffer_t surflight_face_ranges;
    persistent_buffer_t surflight_face_source_indices;
    persistent_buffer_t surflight_accum;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet surflight_descriptor_set = VK_NULL_HANDLE;
    bool descriptors_dirty = true;
    bool surflight_descriptors_dirty = true;
};

struct vertex_t {
    float x, y, z;
};

struct gpu_ray_host_t {
    float ox, oy, oz, tmin;
    float dx, dy, dz, tmax;
    std::uint32_t shadow_mask;
    std::uint32_t user_index;
};

struct gpu_result_host_t {
    std::uint32_t occluded;
    std::uint32_t reserved0;
    float tr, tg, tb;
};

struct gpu_direct_job_host_t {
    float ox, oy, oz, tmin;
    float dx, dy, dz, tmax;
    float cr, cg, cb, pad0;
    float nr, ng, nb, pad1;
    std::uint32_t sample_index;
    std::uint32_t flags;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
};

struct gpu_direct_range_host_t {
    std::uint32_t first;
    std::uint32_t count;
};

struct gpu_direct_accum_host_t {
    float cr, cg, cb, pad0;
    float nr, ng, nb, pad1;
    std::uint32_t hit;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
};



struct gpu_direct_phase_sample_host_t {
    float px, py, pz, occlusion;
    float nx, ny, nz, twosided;
    std::uint32_t face_index;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
};

struct gpu_direct_phase_face_range_host_t {
    std::uint32_t source_begin;
    std::uint32_t source_count;
};

struct gpu_direct_phase_source_host_t {
    float px, py, pz, light;
    float dx, dy, dz, dist;
    float cr, cg, cb, atten;
    std::uint32_t type;
    std::uint32_t formula;
    std::uint32_t flags;
    std::uint32_t reserved0;
    float anglescale;
    float dirt;
    float falloff;
    float pad0;
};
struct push_constants_t {
    std::uint32_t ray_count;
    std::uint32_t flags;
};

struct direct_push_constants_t {
    std::uint32_t sample_count;
    std::uint32_t source_count;
    float scaledist;
    float gate;
};

struct gpu_surflight_source_host_t {
    float nx, ny, nz, intensity;
    float cr, cg, cb, atten;
    std::uint32_t flags;
    std::uint32_t point_begin;
    std::uint32_t point_count;
    std::uint32_t reserved0;
};

struct gpu_surflight_point_host_t {
    float x, y, z, pad0;
};

struct surflight_push_constants_t {
    std::uint32_t sample_count;
    std::uint32_t source_count;
    float scaledist;
    float gate;
    float standard_scale;
    float sky_scale;
    float hotspot_clamp;
    float surflightskydist;
};

static_assert(sizeof(gpu_ray_host_t) == 40, "GPU ray layout must match shader");
static_assert(sizeof(gpu_result_host_t) == 20, "GPU result layout must match shader");
static_assert(sizeof(gpu_direct_job_host_t) == 80, "GPU direct job layout must match shader");
static_assert(sizeof(gpu_direct_range_host_t) == 8, "GPU direct range layout must match shader");
static_assert(sizeof(gpu_direct_accum_host_t) == 48, "GPU direct accum layout must match shader");
static_assert(sizeof(gpu_direct_phase_sample_host_t) == 48, "GPU direct phase sample layout must match shader");
static_assert(sizeof(gpu_direct_phase_face_range_host_t) == 8, "GPU direct phase face range layout must match shader");
static_assert(sizeof(gpu_direct_phase_source_host_t) == 80, "GPU direct phase source layout must match shader");
static_assert(sizeof(gpu_surflight_source_host_t) == 48, "GPU surflight source layout must match shader");
static_assert(sizeof(gpu_surflight_point_host_t) == 16, "GPU surflight point layout must match shader");

// the public API structs are memcpy'd straight into mapped GPU memory
static_assert(sizeof(direct_phase_sample_t) == sizeof(gpu_direct_phase_sample_host_t), "API sample struct must match GPU layout");
static_assert(sizeof(direct_phase_source_t) == sizeof(gpu_direct_phase_source_host_t), "API source struct must match GPU layout");
static_assert(sizeof(direct_phase_face_range_t) == sizeof(gpu_direct_phase_face_range_host_t), "API face range struct must match GPU layout");
static_assert(sizeof(direct_phase_accum_t) == sizeof(gpu_direct_accum_host_t), "API accum struct must match GPU layout");
static_assert(sizeof(surflight_source_t) == sizeof(gpu_surflight_source_host_t), "API surflight source struct must match GPU layout");
static_assert(sizeof(surflight_point_t) == sizeof(gpu_surflight_point_host_t), "API surflight point struct must match GPU layout");

struct context_t {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;

    VkPhysicalDeviceMemoryProperties memory_props{};

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR_ = nullptr;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR_ = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR_ = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR_ = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR_ = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR_ = nullptr;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR_ = nullptr;

    buffer_t vertices;
    buffer_t indices;
    buffer_t instances;
    as_t blas;
    as_t tlas;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

    VkDescriptorSetLayout direct_descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout direct_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline direct_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool direct_descriptor_pool = VK_NULL_HANDLE;

    VkDescriptorSetLayout surflight_descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout surflight_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline surflight_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool surflight_descriptor_pool = VK_NULL_HANDLE;

    std::array<direct_slot_t, 2> direct_slots;

    std::size_t triangle_count = 0;
    bool has_filtered_embree_geometry = false;
};

std::mutex g_mutex;
context_t g;

// Slot ownership: a thread holds a slot's lock for its whole batch (upload,
// submit, fence wait, readback). Kept outside context_t so `g = {}` stays valid.
std::array<std::mutex, 2> g_direct_slot_locks;
std::atomic<std::uint32_t> g_direct_slot_counter{0};

static std::string vk_result_string(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    default: return "VkResult(" + std::to_string(static_cast<int>(r)) + ")";
    }
}

static bool check(VkResult r, const char *what, std::string &error) {
    if (r == VK_SUCCESS) return true;
    error = std::string(what) + " failed: " + vk_result_string(r);
    return false;
}

static bool has_extension(const std::vector<VkExtensionProperties> &props, const char *name) {
    return std::any_of(props.begin(), props.end(), [&](const VkExtensionProperties &p) {
        return std::strcmp(p.extensionName, name) == 0;
    });
}

static void destroy_buffer(buffer_t &b) {
    if (b.buffer) vkDestroyBuffer(g.device, b.buffer, nullptr);
    if (b.memory) vkFreeMemory(g.device, b.memory, nullptr);
    b = {};
}

static void destroy_as(as_t &a) {
    if (a.as) g.vkDestroyAccelerationStructureKHR_(g.device, a.as, nullptr);
    destroy_buffer(a.storage);
    a = {};
}

static void destroy_locked() {
    if (g.device) vkDeviceWaitIdle(g.device);

    if (g.surflight_pipeline) vkDestroyPipeline(g.device, g.surflight_pipeline, nullptr);
    if (g.surflight_pipeline_layout) vkDestroyPipelineLayout(g.device, g.surflight_pipeline_layout, nullptr);
    if (g.surflight_descriptor_pool) vkDestroyDescriptorPool(g.device, g.surflight_descriptor_pool, nullptr);
    if (g.surflight_descriptor_set_layout) vkDestroyDescriptorSetLayout(g.device, g.surflight_descriptor_set_layout, nullptr);

    if (g.direct_pipeline) vkDestroyPipeline(g.device, g.direct_pipeline, nullptr);
    if (g.direct_pipeline_layout) vkDestroyPipelineLayout(g.device, g.direct_pipeline_layout, nullptr);
    if (g.direct_descriptor_pool) vkDestroyDescriptorPool(g.device, g.direct_descriptor_pool, nullptr);
    if (g.direct_descriptor_set_layout) vkDestroyDescriptorSetLayout(g.device, g.direct_descriptor_set_layout, nullptr);

    if (g.pipeline) vkDestroyPipeline(g.device, g.pipeline, nullptr);
    if (g.pipeline_layout) vkDestroyPipelineLayout(g.device, g.pipeline_layout, nullptr);
    if (g.descriptor_pool) vkDestroyDescriptorPool(g.device, g.descriptor_pool, nullptr);
    if (g.descriptor_set_layout) vkDestroyDescriptorSetLayout(g.device, g.descriptor_set_layout, nullptr);

    // vkFreeMemory implicitly unmaps the persistent mappings
    for (auto &slot : g.direct_slots) {
        destroy_buffer(slot.samples.buf);
        destroy_buffer(slot.sources.buf);
        destroy_buffer(slot.face_ranges.buf);
        destroy_buffer(slot.face_source_indices.buf);
        destroy_buffer(slot.accum.buf);
        destroy_buffer(slot.surflight_sources.buf);
        destroy_buffer(slot.surflight_points.buf);
        destroy_buffer(slot.surflight_face_ranges.buf);
        destroy_buffer(slot.surflight_face_source_indices.buf);
        destroy_buffer(slot.surflight_accum.buf);
        if (slot.fence) vkDestroyFence(g.device, slot.fence, nullptr);
        if (slot.command_pool) vkDestroyCommandPool(g.device, slot.command_pool, nullptr);
    }

    destroy_as(g.tlas);
    destroy_as(g.blas);
    destroy_buffer(g.instances);
    destroy_buffer(g.indices);
    destroy_buffer(g.vertices);

    if (g.command_pool) vkDestroyCommandPool(g.device, g.command_pool, nullptr);
    if (g.device) vkDestroyDevice(g.device, nullptr);
    if (g.instance) vkDestroyInstance(g.instance, nullptr);
    g = {};
}

static bool find_memory_type(std::uint32_t type_bits, VkMemoryPropertyFlags props, std::uint32_t &type_index) {
    for (std::uint32_t i = 0; i < g.memory_props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) && ((g.memory_props.memoryTypes[i].propertyFlags & props) == props)) {
            type_index = i;
            return true;
        }
    }
    return false;
}

static bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, buffer_t &out,
    std::string &error, const void *initial_data = nullptr) {
    out = {};
    out.size = size;

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(g.device, &bi, nullptr, &out.buffer), "vkCreateBuffer", error)) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(g.device, out.buffer, &req);

    std::uint32_t mem_type = 0;
    if (!find_memory_type(req.memoryTypeBits, props, mem_type)) {
        error = "no compatible Vulkan memory type for buffer";
        destroy_buffer(out);
        return false;
    }

    VkMemoryAllocateFlagsInfo flags{};
    flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags.flags = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.pNext = flags.flags ? &flags : nullptr;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mem_type;

    if (!check(vkAllocateMemory(g.device, &ai, nullptr, &out.memory), "vkAllocateMemory", error)) {
        destroy_buffer(out);
        return false;
    }
    if (!check(vkBindBufferMemory(g.device, out.buffer, out.memory, 0), "vkBindBufferMemory", error)) {
        destroy_buffer(out);
        return false;
    }

    if (initial_data) {
        void *mapped = nullptr;
        if (!check(vkMapMemory(g.device, out.memory, 0, size, 0, &mapped), "vkMapMemory", error)) {
            destroy_buffer(out);
            return false;
        }
        std::memcpy(mapped, initial_data, static_cast<std::size_t>(size));
        vkUnmapMemory(g.device, out.memory);
    }

    return true;
}

static VkDeviceAddress buffer_address(const buffer_t &b) {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = b.buffer;
    return g.vkGetBufferDeviceAddressKHR_(g.device, &info);
}

// Grows (never shrinks) a persistently-mapped host-visible buffer. Only safe
// while the owning slot's previous submission has completed (the slot lock plus
// its fence wait guarantee this). Sets descriptors_dirty when recreated.
// host_cached: request HOST_CACHED memory so CPU readback isn't crippled by
// uncached/write-combined reads; falls back to plain coherent memory.
static bool ensure_persistent_buffer(
    persistent_buffer_t &pb, VkDeviceSize needed, bool &descriptors_dirty, std::string &error, bool host_cached = false) {
    if (pb.buf.buffer && pb.capacity >= needed) return true;

    destroy_buffer(pb.buf); // implicitly unmaps
    pb.mapped = nullptr;
    pb.capacity = 0;

    const VkDeviceSize capacity = needed + needed / 4; // headroom so batch-size jitter doesn't reallocate
    bool created = false;
    if (host_cached) {
        std::string cached_error;
        created = create_buffer(capacity,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            pb.buf,
            cached_error);
    }
    if (!created && !create_buffer(capacity,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            pb.buf,
            error)) return false;

    if (!check(vkMapMemory(g.device, pb.buf.memory, 0, VK_WHOLE_SIZE, 0, &pb.mapped), "vkMapMemory(persistent)", error)) {
        destroy_buffer(pb.buf);
        return false;
    }

    pb.capacity = capacity;
    descriptors_dirty = true;
    return true;
}

static bool one_time_submit(const std::function<void(VkCommandBuffer)> &record, std::string &error) {
    if (!check(vkResetCommandBuffer(g.command_buffer, 0), "vkResetCommandBuffer", error)) return false;

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(g.command_buffer, &bi), "vkBeginCommandBuffer", error)) return false;
    record(g.command_buffer);
    if (!check(vkEndCommandBuffer(g.command_buffer), "vkEndCommandBuffer", error)) return false;

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g.command_buffer;
    if (!check(vkQueueSubmit(g.queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit", error)) return false;
    if (!check(vkQueueWaitIdle(g.queue), "vkQueueWaitIdle", error)) return false;
    return true;
}

static bool create_instance(std::string &error) {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ericw-tools light gpu";
    app.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
    app.pEngineName = "ericw-tools";
    app.engineVersion = VK_MAKE_VERSION(0, 2, 0);
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    return check(vkCreateInstance(&ci, nullptr, &g.instance), "vkCreateInstance", error);
}

static bool pick_device(std::string &error) {
    std::uint32_t count = 0;
    if (!check(vkEnumeratePhysicalDevices(g.instance, &count, nullptr), "vkEnumeratePhysicalDevices(count)", error)) return false;
    if (!count) { error = "no Vulkan physical devices found"; return false; }

    std::vector<VkPhysicalDevice> devices(count);
    if (!check(vkEnumeratePhysicalDevices(g.instance, &count, devices.data()), "vkEnumeratePhysicalDevices(list)", error)) return false;

    for (VkPhysicalDevice dev : devices) {
        std::uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> exts(ext_count);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, exts.data());

        if (!has_extension(exts, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) ||
            !has_extension(exts, VK_KHR_RAY_QUERY_EXTENSION_NAME) ||
            !has_extension(exts, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) ||
            !has_extension(exts, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)) {
            continue;
        }

        VkPhysicalDeviceBufferDeviceAddressFeatures bda{};
        bda.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        VkPhysicalDeviceRayQueryFeaturesKHR rq{};
        rq.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rq.pNext = &bda;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR as{};
        as.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        as.pNext = &rq;
        VkPhysicalDeviceFeatures2 f2{};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &as;
        vkGetPhysicalDeviceFeatures2(dev, &f2);
        if (!as.accelerationStructure || !rq.rayQuery || !bda.bufferDeviceAddress) continue;

        std::uint32_t q_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &q_count, nullptr);
        std::vector<VkQueueFamilyProperties> qs(q_count);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &q_count, qs.data());
        for (std::uint32_t i = 0; i < q_count; ++i) {
            if (qs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                g.physical = dev;
                g.queue_family = i;
                vkGetPhysicalDeviceMemoryProperties(dev, &g.memory_props);
                return true;
            }
        }
    }

    error = "no Vulkan device with acceleration_structure + ray_query + buffer_device_address + compute queue found";
    return false;
}

static bool create_device(std::string &error) {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g.queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkPhysicalDeviceBufferDeviceAddressFeatures bda{};
    bda.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bda.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceRayQueryFeaturesKHR rq{};
    rq.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rq.rayQuery = VK_TRUE;
    rq.pNext = &bda;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{};
    as.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    as.accelerationStructure = VK_TRUE;
    as.pNext = &rq;

    const char *extensions[] = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    };

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext = &as;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<std::uint32_t>(sizeof(extensions) / sizeof(extensions[0]));
    dci.ppEnabledExtensionNames = extensions;

    if (!check(vkCreateDevice(g.physical, &dci, nullptr, &g.device), "vkCreateDevice", error)) return false;
    vkGetDeviceQueue(g.device, g.queue_family, 0, &g.queue);

#define LOAD_DEVICE_PROC(name) \
    g.name##_ = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(g.device, #name)); \
    if (!g.name##_) { error = "missing device proc " #name; return false; }
    LOAD_DEVICE_PROC(vkGetBufferDeviceAddressKHR);
    LOAD_DEVICE_PROC(vkCreateAccelerationStructureKHR);
    LOAD_DEVICE_PROC(vkDestroyAccelerationStructureKHR);
    LOAD_DEVICE_PROC(vkGetAccelerationStructureBuildSizesKHR);
    LOAD_DEVICE_PROC(vkCmdBuildAccelerationStructuresKHR);
    LOAD_DEVICE_PROC(vkGetAccelerationStructureDeviceAddressKHR);
#undef LOAD_DEVICE_PROC

    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = g.queue_family;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (!check(vkCreateCommandPool(g.device, &pci, nullptr, &g.command_pool), "vkCreateCommandPool", error)) return false;

    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = g.command_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (!check(vkAllocateCommandBuffers(g.device, &cai, &g.command_buffer), "vkAllocateCommandBuffers", error)) return false;

    return true;
}

static void gather_face_triangles(
    const mbsp_t *bsp, const std::set<const mface_t *> &faces, std::vector<vertex_t> &vertices, std::vector<std::uint32_t> &indices) {
    for (const mface_t *face : faces) {
        if (!face || face->numedges < 3) continue;
        const modelinfo_t *modelinfo = ModelInfoForFace(bsp, Face_GetNum(bsp, face));
        if (!modelinfo) continue;

        for (int j = 2; j < face->numedges; ++j) {
            const int v0 = Face_VertexAtIndex(bsp, face, j - 1);
            const int v1 = Face_VertexAtIndex(bsp, face, j);
            const int v2 = Face_VertexAtIndex(bsp, face, 0);
            const qvec3f p0 = Vertex_GetPos(bsp, v0) + modelinfo->offset;
            const qvec3f p1 = Vertex_GetPos(bsp, v1) + modelinfo->offset;
            const qvec3f p2 = Vertex_GetPos(bsp, v2) + modelinfo->offset;

            const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back({p0[0], p0[1], p0[2]});
            vertices.push_back({p1[0], p1[1], p1[2]});
            vertices.push_back({p2[0], p2[1], p2[2]});
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        }
    }
}

// Gathers solid faces first, then sky faces. The split lets build_blas emit
// them as BLAS geometry 0 (solid) and geometry 1 (sky) so the shader can
// require sun rays to hit sky first, matching the Embree scene's semantics.
static bool gather_geometry(const mbsp_t *bsp, std::vector<vertex_t> &vertices, std::vector<std::uint32_t> &indices,
    std::size_t &solid_index_count, std::string &error) {
    vertices.clear();
    indices.clear();

    const auto &faces = ShadowCastingSolidFacesSet();
    if (faces.empty()) {
        error = "no shadow-casting solid faces found for GPU BLAS; call Embree_TraceInit before GPU_TraceInit";
        return false;
    }

    gather_face_triangles(bsp, faces, vertices, indices);
    solid_index_count = indices.size();

    // sky faces, deduped from the Embree sky scene's triangle list
    std::set<const mface_t *> sky_faces;
    for (const triinfo &ti : skygeom.triInfo) {
        if (ti.face) {
            sky_faces.insert(ti.face);
        }
    }
    gather_face_triangles(bsp, sky_faces, vertices, indices);

    if (indices.empty()) {
        error = "GPU geometry gather produced zero triangles";
        return false;
    }
    return true;
}

static bool create_acceleration_structure(VkAccelerationStructureTypeKHR type, VkDeviceSize size, as_t &out, std::string &error) {
    if (!create_buffer(size,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            out.storage,
            error)) return false;

    VkAccelerationStructureCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    ci.type = type;
    ci.size = size;
    ci.buffer = out.storage.buffer;
    if (!check(g.vkCreateAccelerationStructureKHR_(g.device, &ci, nullptr, &out.as), "vkCreateAccelerationStructureKHR", error)) return false;

    VkAccelerationStructureDeviceAddressInfoKHR ai{};
    ai.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    ai.accelerationStructure = out.as;
    out.address = g.vkGetAccelerationStructureDeviceAddressKHR_(g.device, &ai);
    return true;
}

static bool build_blas(const std::vector<vertex_t> &vertices, const std::vector<std::uint32_t> &indices,
    std::size_t solid_index_count, std::string &error) {
    if (!create_buffer(sizeof(vertex_t) * vertices.size(),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            g.vertices,
            error,
            vertices.data())) return false;

    if (!create_buffer(sizeof(std::uint32_t) * indices.size(),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            g.indices,
            error,
            indices.data())) return false;

    VkDeviceAddress vertex_addr = buffer_address(g.vertices);
    VkDeviceAddress index_addr = buffer_address(g.indices);

    const std::uint32_t solid_prims = static_cast<std::uint32_t>(solid_index_count / 3);
    const std::uint32_t sky_prims = static_cast<std::uint32_t>((indices.size() - solid_index_count) / 3);
    g.triangle_count = solid_prims + sky_prims;

    // geometry 0 = solid, geometry 1 = sky (GEOMETRY_SOLID/GEOMETRY_SKY in direct_phase.comp)
    std::array<VkAccelerationStructureGeometryKHR, 2> geoms{};
    std::array<std::uint32_t, 2> prim_counts{solid_prims, sky_prims};
    std::array<VkAccelerationStructureBuildRangeInfoKHR, 2> ranges{};

    for (std::size_t i = 0; i < 2; ++i) {
        auto &geom = geoms[i];
        geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geom.geometry.triangles.vertexData.deviceAddress = vertex_addr;
        geom.geometry.triangles.vertexStride = sizeof(vertex_t);
        geom.geometry.triangles.maxVertex = static_cast<std::uint32_t>(vertices.size() - 1);
        geom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geom.geometry.triangles.indexData.deviceAddress =
            index_addr + (i == 0 ? 0 : sizeof(std::uint32_t) * solid_index_count);
        ranges[i].primitiveCount = prim_counts[i];
    }

    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = static_cast<std::uint32_t>(geoms.size());
    build.pGeometries = geoms.data();

    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    g.vkGetAccelerationStructureBuildSizesKHR_(g.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, prim_counts.data(), &sizes);

    if (!create_acceleration_structure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize, g.blas, error)) return false;

    buffer_t scratch;
    if (!create_buffer(sizes.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            scratch,
            error)) return false;

    build.dstAccelerationStructure = g.blas.as;
    build.scratchData.deviceAddress = buffer_address(scratch);

    const VkAccelerationStructureBuildRangeInfoKHR *range_ptr = ranges.data();

    bool ok = one_time_submit([&](VkCommandBuffer cmd) {
        g.vkCmdBuildAccelerationStructuresKHR_(cmd, 1, &build, &range_ptr);
    }, error);

    destroy_buffer(scratch);
    return ok;
}

static bool build_tlas(std::string &error) {
    VkAccelerationStructureInstanceKHR inst{};
    inst.transform.matrix[0][0] = 1.0f;
    inst.transform.matrix[1][1] = 1.0f;
    inst.transform.matrix[2][2] = 1.0f;
    inst.instanceCustomIndex = 0;
    inst.mask = 0xff;
    inst.instanceShaderBindingTableRecordOffset = 0;
    inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    inst.accelerationStructureReference = g.blas.address;

    if (!create_buffer(sizeof(inst),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            g.instances,
            error,
            &inst)) return false;

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geom.geometry.instances.arrayOfPointers = VK_FALSE;
    geom.geometry.instances.data.deviceAddress = buffer_address(g.instances);

    const std::uint32_t prim_count = 1;

    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geom;

    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    g.vkGetAccelerationStructureBuildSizesKHR_(g.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &prim_count, &sizes);

    if (!create_acceleration_structure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, sizes.accelerationStructureSize, g.tlas, error)) return false;

    buffer_t scratch;
    if (!create_buffer(sizes.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            scratch,
            error)) return false;

    build.dstAccelerationStructure = g.tlas.as;
    build.scratchData.deviceAddress = buffer_address(scratch);

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = prim_count;
    const VkAccelerationStructureBuildRangeInfoKHR *range_ptr = &range;

    bool ok = one_time_submit([&](VkCommandBuffer cmd) {
        g.vkCmdBuildAccelerationStructuresKHR_(cmd, 1, &build, &range_ptr);
    }, error);

    destroy_buffer(scratch);
    return ok;
}

static std::filesystem::path exe_dir() {
#if defined(__linux__)
    std::array<char, PATH_MAX> buf{};
    ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (len > 0) {
        buf[static_cast<std::size_t>(len)] = '\0';
        return std::filesystem::path(buf.data()).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

static bool read_file(const std::filesystem::path &path, std::vector<std::uint32_t> &words, std::string &error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { error = "could not open shader: " + path.string(); return false; }
    const std::streamsize size = f.tellg();
    if (size <= 0 || (size % 4) != 0) { error = "shader has invalid SPIR-V size: " + path.string(); return false; }
    f.seekg(0, std::ios::beg);
    words.resize(static_cast<std::size_t>(size / 4));
    if (!f.read(reinterpret_cast<char *>(words.data()), size)) { error = "failed to read shader: " + path.string(); return false; }
    return true;
}

static bool create_pipeline(std::string &error) {
    VkDescriptorSetLayoutBinding b0{};
    b0.binding = 0;
    b0.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    b0.descriptorCount = 1;
    b0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding b1{};
    b1.binding = 1;
    b1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b1.descriptorCount = 1;
    b1.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding b2{};
    b2.binding = 2;
    b2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b2.descriptorCount = 1;
    b2.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorSetLayoutBinding, 3> bindings{b0, b1, b2};
    VkDescriptorSetLayoutCreateInfo dlci{};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = static_cast<std::uint32_t>(bindings.size());
    dlci.pBindings = bindings.data();
    if (!check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.descriptor_set_layout), "vkCreateDescriptorSetLayout", error)) return false;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(push_constants_t);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g.descriptor_set_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (!check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.pipeline_layout), "vkCreatePipelineLayout", error)) return false;

    std::vector<std::uint32_t> spv;
    const auto shader_path = exe_dir() / "gpu_shaders" / "occlusion.comp.spv";
    if (!read_file(shader_path, spv, error)) return false;

    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = spv.size() * sizeof(std::uint32_t);
    smci.pCode = spv.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    if (!check(vkCreateShaderModule(g.device, &smci, nullptr, &shader), "vkCreateShaderModule", error)) return false;

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = shader;
    cpci.stage.pName = "main";
    cpci.layout = g.pipeline_layout;
    bool ok = check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &g.pipeline), "vkCreateComputePipelines", error);
    vkDestroyShaderModule(g.device, shader, nullptr);
    if (!ok) return false;

    VkDescriptorPoolSize ps0{};
    ps0.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    ps0.descriptorCount = 1;
    VkDescriptorPoolSize ps1{};
    ps1.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps1.descriptorCount = 2;
    std::array<VkDescriptorPoolSize, 2> sizes{ps0, ps1};

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    dpci.pPoolSizes = sizes.data();
    if (!check(vkCreateDescriptorPool(g.device, &dpci, nullptr, &g.descriptor_pool), "vkCreateDescriptorPool", error)) return false;

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = g.descriptor_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g.descriptor_set_layout;
    if (!check(vkAllocateDescriptorSets(g.device, &dsai, &g.descriptor_set), "vkAllocateDescriptorSets", error)) return false;

    return true;
}

static bool create_direct_pipeline(std::string &error) {
    VkDescriptorSetLayoutBinding b0{};
    b0.binding = 0;
    b0.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    b0.descriptorCount = 1;
    b0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding b1{};
    b1.binding = 1;
    b1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b1.descriptorCount = 1;
    b1.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding b2{};
    b2.binding = 2;
    b2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b2.descriptorCount = 1;
    b2.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding b3{};
    b3.binding = 3;
    b3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b3.descriptorCount = 1;
    b3.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding b4{};
    b4.binding = 4;
    b4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b4.descriptorCount = 1;
    b4.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding b5{};
    b5.binding = 5;
    b5.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b5.descriptorCount = 1;
    b5.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorSetLayoutBinding, 6> bindings{b0, b1, b2, b3, b4, b5};
    VkDescriptorSetLayoutCreateInfo dlci{};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = static_cast<std::uint32_t>(bindings.size());
    dlci.pBindings = bindings.data();
    if (!check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.direct_descriptor_set_layout), "vkCreateDescriptorSetLayout(direct)", error)) return false;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(direct_push_constants_t);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g.direct_descriptor_set_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (!check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.direct_pipeline_layout), "vkCreatePipelineLayout(direct)", error)) return false;

    std::vector<std::uint32_t> spv;
    const auto shader_path = exe_dir() / "gpu_shaders" / "direct_phase.comp.spv";
    if (!read_file(shader_path, spv, error)) return false;

    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = spv.size() * sizeof(std::uint32_t);
    smci.pCode = spv.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    if (!check(vkCreateShaderModule(g.device, &smci, nullptr, &shader), "vkCreateShaderModule(direct)", error)) return false;

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = shader;
    cpci.stage.pName = "main";
    cpci.layout = g.direct_pipeline_layout;
    bool ok = check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &g.direct_pipeline), "vkCreateComputePipelines(direct)", error);
    vkDestroyShaderModule(g.device, shader, nullptr);
    if (!ok) return false;

    const std::uint32_t slot_count = static_cast<std::uint32_t>(g.direct_slots.size());

    VkDescriptorPoolSize ps0{};
    ps0.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    ps0.descriptorCount = slot_count;
    VkDescriptorPoolSize ps1{};
    ps1.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps1.descriptorCount = 5 * slot_count;
    std::array<VkDescriptorPoolSize, 2> sizes{ps0, ps1};

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = slot_count;
    dpci.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    dpci.pPoolSizes = sizes.data();
    if (!check(vkCreateDescriptorPool(g.device, &dpci, nullptr, &g.direct_descriptor_pool), "vkCreateDescriptorPool(direct)", error)) return false;

    for (auto &slot : g.direct_slots) {
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = g.direct_descriptor_pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &g.direct_descriptor_set_layout;
        if (!check(vkAllocateDescriptorSets(g.device, &dsai, &slot.descriptor_set), "vkAllocateDescriptorSets(direct)", error)) return false;

        // per-slot command pool: lets two threads record concurrently without
        // sharing pool state; only the queue submit needs global serialization
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = g.queue_family;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (!check(vkCreateCommandPool(g.device, &pci, nullptr, &slot.command_pool), "vkCreateCommandPool(direct slot)", error)) return false;

        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = slot.command_pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        if (!check(vkAllocateCommandBuffers(g.device, &cai, &slot.command_buffer), "vkAllocateCommandBuffers(direct slot)", error)) return false;

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (!check(vkCreateFence(g.device, &fci, nullptr, &slot.fence), "vkCreateFence(direct slot)", error)) return false;
    }

    return true;
}

// Second compute pipeline over the same slots: the surflight (bounce) kernel.
// Reuses each slot's command pool/buffer/fence and the shared sample/range/
// index/accum buffers; only the descriptor layout, pool, and sets are new.
static bool create_surflight_pipeline(std::string &error) {
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    for (std::uint32_t i = 1; i < bindings.size(); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dlci{};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = static_cast<std::uint32_t>(bindings.size());
    dlci.pBindings = bindings.data();
    if (!check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.surflight_descriptor_set_layout), "vkCreateDescriptorSetLayout(surflight)", error)) return false;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(surflight_push_constants_t);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g.surflight_descriptor_set_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (!check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.surflight_pipeline_layout), "vkCreatePipelineLayout(surflight)", error)) return false;

    std::vector<std::uint32_t> spv;
    const auto shader_path = exe_dir() / "gpu_shaders" / "surflight.comp.spv";
    if (!read_file(shader_path, spv, error)) return false;

    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = spv.size() * sizeof(std::uint32_t);
    smci.pCode = spv.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    if (!check(vkCreateShaderModule(g.device, &smci, nullptr, &shader), "vkCreateShaderModule(surflight)", error)) return false;

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = shader;
    cpci.stage.pName = "main";
    cpci.layout = g.surflight_pipeline_layout;
    bool ok = check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &g.surflight_pipeline), "vkCreateComputePipelines(surflight)", error);
    vkDestroyShaderModule(g.device, shader, nullptr);
    if (!ok) return false;

    const std::uint32_t slot_count = static_cast<std::uint32_t>(g.direct_slots.size());

    VkDescriptorPoolSize ps0{};
    ps0.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    ps0.descriptorCount = slot_count;
    VkDescriptorPoolSize ps1{};
    ps1.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps1.descriptorCount = 6 * slot_count;
    std::array<VkDescriptorPoolSize, 2> sizes{ps0, ps1};

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = slot_count;
    dpci.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    dpci.pPoolSizes = sizes.data();
    if (!check(vkCreateDescriptorPool(g.device, &dpci, nullptr, &g.surflight_descriptor_pool), "vkCreateDescriptorPool(surflight)", error)) return false;

    for (auto &slot : g.direct_slots) {
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = g.surflight_descriptor_pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &g.surflight_descriptor_set_layout;
        if (!check(vkAllocateDescriptorSets(g.device, &dsai, &slot.surflight_descriptor_set), "vkAllocateDescriptorSets(surflight)", error)) return false;
    }

    return true;
}

static void update_surflight_descriptor_set(direct_slot_t &slot) {
    VkWriteDescriptorSetAccelerationStructureKHR as_info{};
    as_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    as_info.accelerationStructureCount = 1;
    as_info.pAccelerationStructures = &g.tlas.as;

    VkWriteDescriptorSet w0{};
    w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w0.pNext = &as_info;
    w0.dstSet = slot.surflight_descriptor_set;
    w0.dstBinding = 0;
    w0.descriptorCount = 1;
    w0.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    const std::array<const buffer_t *, 6> buffers{
        &slot.samples.buf, &slot.surflight_sources.buf, &slot.surflight_points.buf,
        &slot.surflight_face_ranges.buf, &slot.surflight_face_source_indices.buf, &slot.surflight_accum.buf};

    std::array<VkDescriptorBufferInfo, 6> buffer_infos{};
    std::array<VkWriteDescriptorSet, 7> writes{};
    writes[0] = w0;
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        buffer_infos[i].buffer = buffers[i]->buffer;
        buffer_infos[i].offset = 0;
        buffer_infos[i].range = buffers[i]->size;

        auto &w = writes[i + 1];
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = slot.surflight_descriptor_set;
        w.dstBinding = static_cast<std::uint32_t>(i + 1);
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo = &buffer_infos[i];
    }

    vkUpdateDescriptorSets(g.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

static void update_direct_descriptor_set(direct_slot_t &slot) {
    VkWriteDescriptorSetAccelerationStructureKHR as_info{};
    as_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    as_info.accelerationStructureCount = 1;
    as_info.pAccelerationStructures = &g.tlas.as;

    VkWriteDescriptorSet w0{};
    w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w0.pNext = &as_info;
    w0.dstSet = slot.descriptor_set;
    w0.dstBinding = 0;
    w0.descriptorCount = 1;
    w0.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    const std::array<const buffer_t *, 5> buffers{
        &slot.samples.buf, &slot.sources.buf, &slot.face_ranges.buf, &slot.face_source_indices.buf, &slot.accum.buf};

    std::array<VkDescriptorBufferInfo, 5> buffer_infos{};
    std::array<VkWriteDescriptorSet, 6> writes{};
    writes[0] = w0;
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        buffer_infos[i].buffer = buffers[i]->buffer;
        buffer_infos[i].offset = 0;
        buffer_infos[i].range = buffers[i]->size;

        auto &w = writes[i + 1];
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = slot.descriptor_set;
        w.dstBinding = static_cast<std::uint32_t>(i + 1);
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo = &buffer_infos[i];
    }

    vkUpdateDescriptorSets(g.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

static void update_descriptor_set(const buffer_t &ray_buffer, const buffer_t &result_buffer) {
    VkWriteDescriptorSetAccelerationStructureKHR as_info{};
    as_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    as_info.accelerationStructureCount = 1;
    as_info.pAccelerationStructures = &g.tlas.as;

    VkWriteDescriptorSet w0{};
    w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w0.pNext = &as_info;
    w0.dstSet = g.descriptor_set;
    w0.dstBinding = 0;
    w0.descriptorCount = 1;
    w0.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorBufferInfo ray_info{};
    ray_info.buffer = ray_buffer.buffer;
    ray_info.offset = 0;
    ray_info.range = ray_buffer.size;
    VkWriteDescriptorSet w1{};
    w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w1.dstSet = g.descriptor_set;
    w1.dstBinding = 1;
    w1.descriptorCount = 1;
    w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w1.pBufferInfo = &ray_info;

    VkDescriptorBufferInfo result_info{};
    result_info.buffer = result_buffer.buffer;
    result_info.offset = 0;
    result_info.range = result_buffer.size;
    VkWriteDescriptorSet w2{};
    w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w2.dstSet = g.descriptor_set;
    w2.dstBinding = 2;
    w2.descriptorCount = 1;
    w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w2.pBufferInfo = &result_info;

    std::array<VkWriteDescriptorSet, 3> writes{w0, w1, w2};
    vkUpdateDescriptorSets(g.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

} // namespace

bool init(const mbsp_t *bsp, std::string &error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    destroy_locked();

    if (!create_instance(error)) { destroy_locked(); return false; }
    if (!pick_device(error)) { destroy_locked(); return false; }
    if (!create_device(error)) { destroy_locked(); return false; }

    g.has_filtered_embree_geometry = !filtergeom.triInfo.empty();
    if (g.has_filtered_embree_geometry) {
        logging::print("GPU light: filtered Embree geometry exists ({} tris); GPU will fall back for correctness.\n", filtergeom.triInfo.size());
    }

    std::vector<vertex_t> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t solid_index_count = 0;
    if (!gather_geometry(bsp, vertices, indices, solid_index_count, error)) { destroy_locked(); return false; }
    if (!build_blas(vertices, indices, solid_index_count, error)) { destroy_locked(); return false; }
    if (!build_tlas(error)) { destroy_locked(); return false; }
    if (!create_pipeline(error)) { destroy_locked(); return false; }
    if (!create_direct_pipeline(error)) { destroy_locked(); return false; }
    if (!create_surflight_pipeline(error)) { destroy_locked(); return false; }

    logging::print("GPU light: Vulkan ray-query BLAS/TLAS ready ({} solid + {} sky triangles).\n",
        solid_index_count / 3, (indices.size() - solid_index_count) / 3);
    return true;
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    destroy_locked();
}

bool has_filtered_geometry() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g.has_filtered_embree_geometry;
}

bool trace_occlusion_batch(
    const modelinfo_t *self,
    std::uint32_t shadow_mask,
    const gpu_light::ray_t *rays,
    gpu_light::occlusion_result_t *results,
    std::size_t count,
    std::string &error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g.device || !g.pipeline || !g.tlas.as) {
        error = "Vulkan GPU backend is not initialized";
        return false;
    }

    // Correctness guard: the GPU fast path only contains opaque solid/default geometry.
    // If Embree has filtered geometry, let CPU handle batches so glass/fence/dynamic/channel filters remain correct.
    if (g.has_filtered_embree_geometry) {
        return false;
    }
    if (shadow_mask != CHANNEL_MASK_DEFAULT) {
        return false;
    }
    (void)self;

    std::vector<gpu_ray_host_t> gpu_rays(count);
    for (std::size_t i = 0; i < count; ++i) {
        gpu_rays[i].ox = rays[i].origin[0];
        gpu_rays[i].oy = rays[i].origin[1];
        gpu_rays[i].oz = rays[i].origin[2];
        gpu_rays[i].tmin = rays[i].tmin;
        gpu_rays[i].dx = rays[i].direction[0];
        gpu_rays[i].dy = rays[i].direction[1];
        gpu_rays[i].dz = rays[i].direction[2];
        gpu_rays[i].tmax = rays[i].tmax;
        gpu_rays[i].shadow_mask = rays[i].shadow_mask;
        gpu_rays[i].user_index = rays[i].user_index;
    }

    buffer_t ray_buffer;
    buffer_t result_buffer;
    std::vector<gpu_result_host_t> zero_results(count);

    bool ok = create_buffer(sizeof(gpu_ray_host_t) * count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ray_buffer,
        error,
        gpu_rays.data());
    if (!ok) return false;

    ok = create_buffer(sizeof(gpu_result_host_t) * count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        result_buffer,
        error,
        zero_results.data());
    if (!ok) {
        destroy_buffer(ray_buffer);
        return false;
    }

    update_descriptor_set(ray_buffer, result_buffer);

    push_constants_t pc{};
    pc.ray_count = static_cast<std::uint32_t>(count);
    pc.flags = 0;

    ok = one_time_submit([&](VkCommandBuffer cmd) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.pipeline_layout, 0, 1, &g.descriptor_set, 0, nullptr);
        vkCmdPushConstants(cmd, g.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (pc.ray_count + 127u) / 128u, 1, 1);
    }, error);

    if (ok) {
        void *mapped = nullptr;
        ok = check(vkMapMemory(g.device, result_buffer.memory, 0, result_buffer.size, 0, &mapped), "vkMapMemory(result)", error);
        if (ok) {
            const auto *gpu_results = static_cast<const gpu_result_host_t *>(mapped);
            for (std::size_t i = 0; i < count; ++i) {
                results[i].occluded = gpu_results[i].occluded;
                results[i].reserved0 = gpu_results[i].reserved0;
                results[i].transmittance[0] = gpu_results[i].tr;
                results[i].transmittance[1] = gpu_results[i].tg;
                results[i].transmittance[2] = gpu_results[i].tb;
            }
            vkUnmapMemory(g.device, result_buffer.memory);
        }
    }

    destroy_buffer(result_buffer);
    destroy_buffer(ray_buffer);
    return ok;
}


// One submission for the whole direct phase of a batch: the entity/sun kernel
// and the direct-surflight kernel run back to back against a single sample
// upload, each writing its own accum buffer (no barrier needed between them),
// and the results are summed at readback. Either side may be absent.
bool trace_direct_combined_batch(
    const gpu_light::direct_combined_batch_t &batch,
    gpu_light::direct_phase_accum_t *accum,
    std::string &error) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g.device || !g.direct_pipeline || !g.surflight_pipeline || !g.tlas.as) {
            error = "Vulkan GPU direct phase backend is not initialized";
            return false;
        }
        if (g.has_filtered_embree_geometry) {
            return false;
        }
    }

    const bool direct_work = batch.sources && batch.source_count && batch.face_ranges && batch.face_range_count &&
        batch.face_source_indices && batch.face_source_index_count;
    const bool surflight_work = batch.surflight_sources && batch.surflight_source_count && batch.surflight_points &&
        batch.surflight_point_count && batch.surflight_face_ranges && batch.surflight_face_range_count &&
        batch.surflight_face_source_indices && batch.surflight_face_source_index_count;
    if (!batch.samples || !accum || batch.sample_count == 0 || (!direct_work && !surflight_work)) {
        return true;
    }

    // Acquire a slot for the whole batch. With two slots, a second thread can
    // upload/read back its batch while this one executes on the GPU; only the
    // queue submit below takes the global mutex.
    const std::uint32_t slot_index =
        g_direct_slot_counter.fetch_add(1u, std::memory_order_relaxed) % static_cast<std::uint32_t>(g.direct_slots.size());
    std::lock_guard<std::mutex> slot_lock(g_direct_slot_locks[slot_index]);
    direct_slot_t &slot = g.direct_slots[slot_index];

    // the samples buffer backs both descriptor sets, so growth dirties both
    bool buffers_grew = false;
    if (!ensure_persistent_buffer(slot.samples, sizeof(gpu_direct_phase_sample_host_t) * batch.sample_count, buffers_grew, error)) {
        return false;
    }
    if (direct_work) {
        if (!ensure_persistent_buffer(slot.sources, sizeof(gpu_direct_phase_source_host_t) * batch.source_count, buffers_grew, error) ||
            !ensure_persistent_buffer(slot.face_ranges, sizeof(gpu_direct_phase_face_range_host_t) * batch.face_range_count, buffers_grew, error) ||
            !ensure_persistent_buffer(slot.face_source_indices, sizeof(std::uint32_t) * batch.face_source_index_count, buffers_grew, error) ||
            !ensure_persistent_buffer(slot.accum, sizeof(gpu_direct_accum_host_t) * batch.sample_count, buffers_grew, error, /*host_cached=*/true)) {
            return false;
        }
    }
    if (surflight_work) {
        if (!ensure_persistent_buffer(slot.surflight_sources, sizeof(gpu_surflight_source_host_t) * batch.surflight_source_count, buffers_grew, error) ||
            !ensure_persistent_buffer(slot.surflight_points, sizeof(gpu_surflight_point_host_t) * batch.surflight_point_count, buffers_grew, error) ||
            !ensure_persistent_buffer(slot.surflight_face_ranges, sizeof(gpu_direct_phase_face_range_host_t) * batch.surflight_face_range_count, buffers_grew, error) ||
            !ensure_persistent_buffer(slot.surflight_face_source_indices, sizeof(std::uint32_t) * batch.surflight_face_source_index_count, buffers_grew, error) ||
            !ensure_persistent_buffer(slot.surflight_accum, sizeof(gpu_direct_accum_host_t) * batch.sample_count, buffers_grew, error, /*host_cached=*/true)) {
            return false;
        }
    }
    slot.descriptors_dirty |= buffers_grew;
    slot.surflight_descriptors_dirty |= buffers_grew;

    std::memcpy(slot.samples.mapped, batch.samples, sizeof(gpu_direct_phase_sample_host_t) * batch.sample_count);
    if (direct_work) {
        std::memcpy(slot.sources.mapped, batch.sources, sizeof(gpu_direct_phase_source_host_t) * batch.source_count);
        std::memcpy(slot.face_ranges.mapped, batch.face_ranges, sizeof(gpu_direct_phase_face_range_host_t) * batch.face_range_count);
        std::memcpy(slot.face_source_indices.mapped, batch.face_source_indices, sizeof(std::uint32_t) * batch.face_source_index_count);
    }
    if (surflight_work) {
        std::memcpy(slot.surflight_sources.mapped, batch.surflight_sources, sizeof(gpu_surflight_source_host_t) * batch.surflight_source_count);
        std::memcpy(slot.surflight_points.mapped, batch.surflight_points, sizeof(gpu_surflight_point_host_t) * batch.surflight_point_count);
        std::memcpy(slot.surflight_face_ranges.mapped, batch.surflight_face_ranges, sizeof(gpu_direct_phase_face_range_host_t) * batch.surflight_face_range_count);
        std::memcpy(slot.surflight_face_source_indices.mapped, batch.surflight_face_source_indices, sizeof(std::uint32_t) * batch.surflight_face_source_index_count);
    }
    // no accum pre-clear needed: each shader writes every accum slot below sample_count

    // must happen before recording the bind: descriptor sets may not be updated
    // between recording and execution. Only sets that will be bound are
    // updated (a skipped side's buffers may not exist yet).
    if (direct_work && slot.descriptors_dirty) {
        update_direct_descriptor_set(slot);
        slot.descriptors_dirty = false;
    }
    if (surflight_work && slot.surflight_descriptors_dirty) {
        update_surflight_descriptor_set(slot);
        slot.surflight_descriptors_dirty = false;
    }

    // record with the slot's own command pool; no global lock needed
    if (!check(vkResetCommandBuffer(slot.command_buffer, 0), "vkResetCommandBuffer(direct)", error)) return false;
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(slot.command_buffer, &bi), "vkBeginCommandBuffer(direct)", error)) return false;

    if (direct_work) {
        direct_push_constants_t pc{};
        pc.sample_count = static_cast<std::uint32_t>(batch.sample_count);
        pc.source_count = static_cast<std::uint32_t>(batch.source_count);
        pc.scaledist = light_options.scaledist.value();
        pc.gate = light_options.gate.value();

        vkCmdBindPipeline(slot.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, g.direct_pipeline);
        vkCmdBindDescriptorSets(slot.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, g.direct_pipeline_layout, 0, 1, &slot.descriptor_set, 0, nullptr);
        vkCmdPushConstants(slot.command_buffer, g.direct_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(slot.command_buffer, (pc.sample_count + 63u) / 64u, 1, 1);
    }

    if (surflight_work) {
        surflight_push_constants_t pc{};
        pc.sample_count = static_cast<std::uint32_t>(batch.sample_count);
        pc.source_count = static_cast<std::uint32_t>(batch.surflight_source_count);
        pc.scaledist = batch.surflight_params.scaledist;
        pc.gate = batch.surflight_params.gate;
        pc.standard_scale = batch.surflight_params.standard_scale;
        pc.sky_scale = batch.surflight_params.sky_scale;
        pc.hotspot_clamp = batch.surflight_params.hotspot_clamp;
        pc.surflightskydist = batch.surflight_params.surflightskydist;

        vkCmdBindPipeline(slot.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, g.surflight_pipeline);
        vkCmdBindDescriptorSets(slot.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, g.surflight_pipeline_layout, 0, 1, &slot.surflight_descriptor_set, 0, nullptr);
        vkCmdPushConstants(slot.command_buffer, g.surflight_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(slot.command_buffer, (pc.sample_count + 63u) / 64u, 1, 1);
    }

    // make the shaders' accum writes visible to the persistently-mapped host readback
    VkMemoryBarrier to_host{};
    to_host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    to_host.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(slot.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
        1, &to_host, 0, nullptr, 0, nullptr);

    if (!check(vkEndCommandBuffer(slot.command_buffer), "vkEndCommandBuffer(direct)", error)) return false;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &slot.command_buffer;
        if (!check(vkQueueSubmit(g.queue, 1, &si, slot.fence), "vkQueueSubmit(direct)", error)) return false;
    }

    // wait on this batch's fence without blocking the other slot
    if (!check(vkWaitForFences(g.device, 1, &slot.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(direct)", error)) return false;
    if (!check(vkResetFences(g.device, 1, &slot.fence), "vkResetFences(direct)", error)) return false;

    if (direct_work) {
        std::memcpy(accum, slot.accum.mapped, sizeof(gpu_direct_accum_host_t) * batch.sample_count);
    }
    if (surflight_work) {
        const auto *surf = static_cast<const gpu_direct_accum_host_t *>(slot.surflight_accum.mapped);
        if (direct_work) {
            // the surflight kernel contributes color/hit only (its normal
            // output is always zero); summing single per-kernel totals here
            // matches the CPU's sequential apply exactly
            for (std::size_t i = 0; i < batch.sample_count; ++i) {
                accum[i].cr += surf[i].cr;
                accum[i].cg += surf[i].cg;
                accum[i].cb += surf[i].cb;
                accum[i].hit |= surf[i].hit;
            }
        } else {
            std::memcpy(accum, surf, sizeof(gpu_direct_accum_host_t) * batch.sample_count);
        }
    }
    return true;
}

bool trace_surflight_batch(
    const gpu_light::surflight_source_t *sources,
    std::size_t source_count,
    const gpu_light::surflight_point_t *points,
    std::size_t point_count,
    const gpu_light::direct_phase_sample_t *samples,
    gpu_light::direct_phase_accum_t *accum,
    std::size_t sample_count,
    const gpu_light::direct_phase_face_range_t *face_ranges,
    std::size_t face_range_count,
    const std::uint32_t *face_source_indices,
    std::size_t face_source_index_count,
    const gpu_light::surflight_params_t &params,
    std::string &error) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g.device || !g.surflight_pipeline || !g.tlas.as) {
            error = "Vulkan GPU surflight backend is not initialized";
            return false;
        }
        if (g.has_filtered_embree_geometry) {
            return false;
        }
    }
    if (!sources || !points || !samples || !accum || !face_ranges || !face_source_indices || source_count == 0 ||
        point_count == 0 || sample_count == 0 || face_range_count == 0 || face_source_index_count == 0) {
        return true;
    }

    const std::uint32_t slot_index =
        g_direct_slot_counter.fetch_add(1u, std::memory_order_relaxed) % static_cast<std::uint32_t>(g.direct_slots.size());
    std::lock_guard<std::mutex> slot_lock(g_direct_slot_locks[slot_index]);
    direct_slot_t &slot = g.direct_slots[slot_index];

    // the samples buffer backs both descriptor sets, so growth dirties both
    bool buffers_grew = false;
    if (!ensure_persistent_buffer(slot.samples, sizeof(gpu_direct_phase_sample_host_t) * sample_count, buffers_grew, error) ||
        !ensure_persistent_buffer(slot.surflight_sources, sizeof(gpu_surflight_source_host_t) * source_count, buffers_grew, error) ||
        !ensure_persistent_buffer(slot.surflight_points, sizeof(gpu_surflight_point_host_t) * point_count, buffers_grew, error) ||
        !ensure_persistent_buffer(slot.surflight_face_ranges, sizeof(gpu_direct_phase_face_range_host_t) * face_range_count, buffers_grew, error) ||
        !ensure_persistent_buffer(slot.surflight_face_source_indices, sizeof(std::uint32_t) * face_source_index_count, buffers_grew, error) ||
        !ensure_persistent_buffer(slot.surflight_accum, sizeof(gpu_direct_accum_host_t) * sample_count, buffers_grew, error, /*host_cached=*/true)) {
        return false;
    }
    slot.descriptors_dirty |= buffers_grew;
    slot.surflight_descriptors_dirty |= buffers_grew;

    std::memcpy(slot.samples.mapped, samples, sizeof(gpu_direct_phase_sample_host_t) * sample_count);
    std::memcpy(slot.surflight_sources.mapped, sources, sizeof(gpu_surflight_source_host_t) * source_count);
    std::memcpy(slot.surflight_points.mapped, points, sizeof(gpu_surflight_point_host_t) * point_count);
    std::memcpy(slot.surflight_face_ranges.mapped, face_ranges, sizeof(gpu_direct_phase_face_range_host_t) * face_range_count);
    std::memcpy(slot.surflight_face_source_indices.mapped, face_source_indices, sizeof(std::uint32_t) * face_source_index_count);
    // no accum pre-clear needed: the shader writes every accum slot below sample_count

    // must happen before recording the bind: descriptor sets may not be updated
    // between recording and execution
    if (slot.surflight_descriptors_dirty) {
        update_surflight_descriptor_set(slot);
        slot.surflight_descriptors_dirty = false;
    }

    surflight_push_constants_t pc{};
    pc.sample_count = static_cast<std::uint32_t>(sample_count);
    pc.source_count = static_cast<std::uint32_t>(source_count);
    pc.scaledist = params.scaledist;
    pc.gate = params.gate;
    pc.standard_scale = params.standard_scale;
    pc.sky_scale = params.sky_scale;
    pc.hotspot_clamp = params.hotspot_clamp;
    pc.surflightskydist = params.surflightskydist;

    if (!check(vkResetCommandBuffer(slot.command_buffer, 0), "vkResetCommandBuffer(surflight)", error)) return false;
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(slot.command_buffer, &bi), "vkBeginCommandBuffer(surflight)", error)) return false;

    vkCmdBindPipeline(slot.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, g.surflight_pipeline);
    vkCmdBindDescriptorSets(slot.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, g.surflight_pipeline_layout, 0, 1, &slot.surflight_descriptor_set, 0, nullptr);
    vkCmdPushConstants(slot.command_buffer, g.surflight_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(slot.command_buffer, (pc.sample_count + 63u) / 64u, 1, 1);

    // make the shader's accum writes visible to the persistently-mapped host readback
    VkMemoryBarrier to_host{};
    to_host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    to_host.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(slot.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
        1, &to_host, 0, nullptr, 0, nullptr);

    if (!check(vkEndCommandBuffer(slot.command_buffer), "vkEndCommandBuffer(surflight)", error)) return false;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &slot.command_buffer;
        if (!check(vkQueueSubmit(g.queue, 1, &si, slot.fence), "vkQueueSubmit(surflight)", error)) return false;
    }

    if (!check(vkWaitForFences(g.device, 1, &slot.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(surflight)", error)) return false;
    if (!check(vkResetFences(g.device, 1, &slot.fence), "vkResetFences(surflight)", error)) return false;

    std::memcpy(accum, slot.surflight_accum.mapped, sizeof(gpu_direct_accum_host_t) * sample_count);
    return true;
}

bool trace_direct_accumulate_batch(
    const modelinfo_t *self,
    std::uint32_t shadow_mask,
    const gpu_light::direct_job_t *jobs,
    std::size_t job_count,
    const gpu_light::direct_sample_range_t *ranges,
    gpu_light::direct_accum_t *accum,
    std::size_t sample_count,
    std::string &error) {
    (void)self;
    (void)shadow_mask;
    (void)jobs;
    (void)job_count;
    (void)ranges;
    (void)accum;
    (void)sample_count;
    error = "old direct job buffer path disabled in v5; use trace_direct_combined_batch";
    return false;
}

} // namespace gpu_light::vulkan_backend

#endif // HAVE_GPU_LIGHT
