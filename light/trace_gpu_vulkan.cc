#include <light/trace_gpu.hh>

#if defined(HAVE_GPU_LIGHT)

#include <common/bspfile.hh>
#include <common/log.hh>
#include <light/light.hh>
#include <light/trace_embree.hh>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
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
    std::uint32_t flags;
    std::uint32_t reserved0;
};

static_assert(sizeof(gpu_ray_host_t) == 40, "GPU ray layout must match shader");
static_assert(sizeof(gpu_result_host_t) == 20, "GPU result layout must match shader");
static_assert(sizeof(gpu_direct_job_host_t) == 80, "GPU direct job layout must match shader");
static_assert(sizeof(gpu_direct_range_host_t) == 8, "GPU direct range layout must match shader");
static_assert(sizeof(gpu_direct_accum_host_t) == 48, "GPU direct accum layout must match shader");
static_assert(sizeof(gpu_direct_phase_sample_host_t) == 32, "GPU direct phase sample layout must match shader");
static_assert(sizeof(gpu_direct_phase_source_host_t) == 80, "GPU direct phase source layout must match shader");

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
    VkDescriptorSet direct_descriptor_set = VK_NULL_HANDLE;

    std::size_t triangle_count = 0;
    bool has_filtered_embree_geometry = false;
};

std::mutex g_mutex;
context_t g;

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

    if (g.direct_pipeline) vkDestroyPipeline(g.device, g.direct_pipeline, nullptr);
    if (g.direct_pipeline_layout) vkDestroyPipelineLayout(g.device, g.direct_pipeline_layout, nullptr);
    if (g.direct_descriptor_pool) vkDestroyDescriptorPool(g.device, g.direct_descriptor_pool, nullptr);
    if (g.direct_descriptor_set_layout) vkDestroyDescriptorSetLayout(g.device, g.direct_descriptor_set_layout, nullptr);

    if (g.pipeline) vkDestroyPipeline(g.device, g.pipeline, nullptr);
    if (g.pipeline_layout) vkDestroyPipelineLayout(g.device, g.pipeline_layout, nullptr);
    if (g.descriptor_pool) vkDestroyDescriptorPool(g.device, g.descriptor_pool, nullptr);
    if (g.descriptor_set_layout) vkDestroyDescriptorSetLayout(g.device, g.descriptor_set_layout, nullptr);

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

static bool gather_geometry(const mbsp_t *bsp, std::vector<vertex_t> &vertices, std::vector<std::uint32_t> &indices, std::string &error) {
    vertices.clear();
    indices.clear();

    const auto &faces = ShadowCastingSolidFacesSet();
    if (faces.empty()) {
        error = "no shadow-casting solid faces found for GPU BLAS; call Embree_TraceInit before GPU_TraceInit";
        return false;
    }

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

static bool build_blas(const std::vector<vertex_t> &vertices, const std::vector<std::uint32_t> &indices, std::string &error) {
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

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geom.geometry.triangles.vertexData.deviceAddress = vertex_addr;
    geom.geometry.triangles.vertexStride = sizeof(vertex_t);
    geom.geometry.triangles.maxVertex = static_cast<std::uint32_t>(vertices.size() - 1);
    geom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    geom.geometry.triangles.indexData.deviceAddress = index_addr;

    const std::uint32_t prim_count = static_cast<std::uint32_t>(indices.size() / 3);
    g.triangle_count = prim_count;

    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geom;

    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    g.vkGetAccelerationStructureBuildSizesKHR_(g.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &prim_count, &sizes);

    if (!create_acceleration_structure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize, g.blas, error)) return false;

    buffer_t scratch;
    if (!create_buffer(sizes.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            scratch,
            error)) return false;

    build.dstAccelerationStructure = g.blas.as;
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

    std::array<VkDescriptorSetLayoutBinding, 4> bindings{b0, b1, b2, b3};
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

    VkDescriptorPoolSize ps0{};
    ps0.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    ps0.descriptorCount = 1;
    VkDescriptorPoolSize ps1{};
    ps1.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps1.descriptorCount = 3;
    std::array<VkDescriptorPoolSize, 2> sizes{ps0, ps1};

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    dpci.pPoolSizes = sizes.data();
    if (!check(vkCreateDescriptorPool(g.device, &dpci, nullptr, &g.direct_descriptor_pool), "vkCreateDescriptorPool(direct)", error)) return false;

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = g.direct_descriptor_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g.direct_descriptor_set_layout;
    if (!check(vkAllocateDescriptorSets(g.device, &dsai, &g.direct_descriptor_set), "vkAllocateDescriptorSets(direct)", error)) return false;

    return true;
}

static void update_direct_descriptor_set(const buffer_t &job_buffer, const buffer_t &range_buffer, const buffer_t &accum_buffer) {
    VkWriteDescriptorSetAccelerationStructureKHR as_info{};
    as_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    as_info.accelerationStructureCount = 1;
    as_info.pAccelerationStructures = &g.tlas.as;

    VkWriteDescriptorSet w0{};
    w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w0.pNext = &as_info;
    w0.dstSet = g.direct_descriptor_set;
    w0.dstBinding = 0;
    w0.descriptorCount = 1;
    w0.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorBufferInfo job_info{};
    job_info.buffer = job_buffer.buffer;
    job_info.offset = 0;
    job_info.range = job_buffer.size;
    VkWriteDescriptorSet w1{};
    w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w1.dstSet = g.direct_descriptor_set;
    w1.dstBinding = 1;
    w1.descriptorCount = 1;
    w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w1.pBufferInfo = &job_info;

    VkDescriptorBufferInfo range_info{};
    range_info.buffer = range_buffer.buffer;
    range_info.offset = 0;
    range_info.range = range_buffer.size;
    VkWriteDescriptorSet w2{};
    w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w2.dstSet = g.direct_descriptor_set;
    w2.dstBinding = 2;
    w2.descriptorCount = 1;
    w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w2.pBufferInfo = &range_info;

    VkDescriptorBufferInfo accum_info{};
    accum_info.buffer = accum_buffer.buffer;
    accum_info.offset = 0;
    accum_info.range = accum_buffer.size;
    VkWriteDescriptorSet w3{};
    w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w3.dstSet = g.direct_descriptor_set;
    w3.dstBinding = 3;
    w3.descriptorCount = 1;
    w3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w3.pBufferInfo = &accum_info;

    std::array<VkWriteDescriptorSet, 4> writes{w0, w1, w2, w3};
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
    if (!gather_geometry(bsp, vertices, indices, error)) { destroy_locked(); return false; }
    if (!build_blas(vertices, indices, error)) { destroy_locked(); return false; }
    if (!build_tlas(error)) { destroy_locked(); return false; }
    if (!create_pipeline(error)) { destroy_locked(); return false; }
    if (!create_direct_pipeline(error)) { destroy_locked(); return false; }

    logging::print("GPU light: Vulkan ray-query BLAS/TLAS ready ({} opaque triangles).\n", g.triangle_count);
    return true;
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    destroy_locked();
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


bool trace_direct_phase_batch(
    const gpu_light::direct_phase_source_t *sources,
    std::size_t source_count,
    const gpu_light::direct_phase_sample_t *samples,
    gpu_light::direct_phase_accum_t *accum,
    std::size_t sample_count,
    std::string &error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g.device || !g.direct_pipeline || !g.tlas.as) {
        error = "Vulkan GPU direct phase backend is not initialized";
        return false;
    }
    if (g.has_filtered_embree_geometry) {
        return false;
    }
    if (!sources || !samples || !accum || source_count == 0 || sample_count == 0) {
        return true;
    }

    std::vector<gpu_direct_phase_sample_host_t> gpu_samples(sample_count);
    for (std::size_t i = 0; i < sample_count; ++i) {
        gpu_samples[i].px = samples[i].px;
        gpu_samples[i].py = samples[i].py;
        gpu_samples[i].pz = samples[i].pz;
        gpu_samples[i].occlusion = samples[i].occlusion;
        gpu_samples[i].nx = samples[i].nx;
        gpu_samples[i].ny = samples[i].ny;
        gpu_samples[i].nz = samples[i].nz;
        gpu_samples[i].twosided = samples[i].twosided;
    }

    std::vector<gpu_direct_phase_source_host_t> gpu_sources(source_count);
    for (std::size_t i = 0; i < source_count; ++i) {
        gpu_sources[i].px = sources[i].px;
        gpu_sources[i].py = sources[i].py;
        gpu_sources[i].pz = sources[i].pz;
        gpu_sources[i].light = sources[i].light;
        gpu_sources[i].dx = sources[i].dx;
        gpu_sources[i].dy = sources[i].dy;
        gpu_sources[i].dz = sources[i].dz;
        gpu_sources[i].dist = sources[i].dist;
        gpu_sources[i].cr = sources[i].cr;
        gpu_sources[i].cg = sources[i].cg;
        gpu_sources[i].cb = sources[i].cb;
        gpu_sources[i].atten = sources[i].atten;
        gpu_sources[i].type = sources[i].type;
        gpu_sources[i].formula = sources[i].formula;
        gpu_sources[i].flags = sources[i].flags;
        gpu_sources[i].reserved0 = 0;
        gpu_sources[i].anglescale = sources[i].anglescale;
        gpu_sources[i].dirt = sources[i].dirt;
        gpu_sources[i].falloff = sources[i].falloff;
        gpu_sources[i].pad0 = 0.0f;
    }

    std::vector<gpu_direct_accum_host_t> zero_accum(sample_count);

    buffer_t sample_buffer;
    buffer_t source_buffer;
    buffer_t accum_buffer;

    bool ok = create_buffer(sizeof(gpu_direct_phase_sample_host_t) * sample_count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        sample_buffer,
        error,
        gpu_samples.data());
    if (!ok) return false;

    ok = create_buffer(sizeof(gpu_direct_phase_source_host_t) * source_count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        source_buffer,
        error,
        gpu_sources.data());
    if (!ok) {
        destroy_buffer(sample_buffer);
        return false;
    }

    ok = create_buffer(sizeof(gpu_direct_accum_host_t) * sample_count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        accum_buffer,
        error,
        zero_accum.data());
    if (!ok) {
        destroy_buffer(source_buffer);
        destroy_buffer(sample_buffer);
        return false;
    }

    update_direct_descriptor_set(sample_buffer, source_buffer, accum_buffer);

    direct_push_constants_t pc{};
    pc.sample_count = static_cast<std::uint32_t>(sample_count);
    pc.source_count = static_cast<std::uint32_t>(source_count);
    pc.flags = 0;
    pc.reserved0 = 0;

    ok = one_time_submit([&](VkCommandBuffer cmd) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.direct_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.direct_pipeline_layout, 0, 1, &g.direct_descriptor_set, 0, nullptr);
        vkCmdPushConstants(cmd, g.direct_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (pc.sample_count + 63u) / 64u, 1, 1);
    }, error);

    if (ok) {
        void *mapped = nullptr;
        ok = check(vkMapMemory(g.device, accum_buffer.memory, 0, accum_buffer.size, 0, &mapped), "vkMapMemory(direct phase accum)", error);
        if (ok) {
            const auto *gpu_accum = static_cast<const gpu_direct_accum_host_t *>(mapped);
            for (std::size_t i = 0; i < sample_count; ++i) {
                accum[i].cr = gpu_accum[i].cr;
                accum[i].cg = gpu_accum[i].cg;
                accum[i].cb = gpu_accum[i].cb;
                accum[i].pad0 = 0.0f;
                accum[i].nr = gpu_accum[i].nr;
                accum[i].ng = gpu_accum[i].ng;
                accum[i].nb = gpu_accum[i].nb;
                accum[i].pad1 = 0.0f;
                accum[i].hit = gpu_accum[i].hit;
                accum[i].reserved0 = 0;
                accum[i].reserved1 = 0;
                accum[i].reserved2 = 0;
            }
            vkUnmapMemory(g.device, accum_buffer.memory);
        }
    }

    destroy_buffer(accum_buffer);
    destroy_buffer(source_buffer);
    destroy_buffer(sample_buffer);
    return ok;
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
    error = "old direct job buffer path disabled in v5; use trace_direct_phase_batch";
    return false;
}

} // namespace gpu_light::vulkan_backend

#endif // HAVE_GPU_LIGHT
