
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <flat_map>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <iostream>
#include <optional>
#include <print>
#include <chrono>
#include <random>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "../head.hpp"

using Instance = mcs::vulkan::Instance;
using create_instance = mcs::vulkan::tool::create_instance;
using create_debugger = mcs::vulkan::tool::create_debugger;
using physical_device_selector = mcs::vulkan::tool::physical_device_selector;
using mcs::vulkan::vkMakeVersion;
using mcs::vulkan::vkApiVersion;

using mcs::vulkan::tool::enable_intance_build;
using mcs::vulkan::tool::structure_chain;
using mcs::vulkan::tool::queue_family_index_selector;
using mcs::vulkan::tool::create_logical_device;
using mcs::vulkan::tool::create_swap_chain;
using mcs::vulkan::tool::create_pipeline_layout;
using mcs::vulkan::tool::create_graphics_pipeline;
using mcs::vulkan::tool::create_command_pool;
using mcs::vulkan::tool::frame_context;
using mcs::vulkan::tool::sType;
using mcs::vulkan::tool::create_descriptor_set_layout;
using mcs::vulkan::tool::create_descriptor_pool;

using mcs::vulkan::camera::camera_interface;
using mcs::vulkan::input::glfw_input;

using mcs::vulkan::raii_vulkan;
using mcs::vulkan::PhysicalDevice;
using mcs::vulkan::surface_impl;
using surface = mcs::vulkan::wsi::glfw::Window;
using mcs::vulkan::Queue;

using mcs::vulkan::LogicalDevice;

using mcs::vulkan::tool::make_pNext;

using mcs::vulkan::CommandPool;
using mcs::vulkan::CommandBuffers;
using mcs::vulkan::CommandBuffer;
using mcs::vulkan::CommandBufferView;
using mcs::vulkan::Fence;

using raii_vma = mcs::vulkan::raii_vma;

using mcs::vulkan::tool::simple_copy_buffer;

using mcs::vulkan::meta::make_aggregate_ref;
using mcs::vulkan::meta::make_aggregate;

using mcs::vulkan::ecs::gen_soa_aggregate;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";

static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;

struct my_render
{

    struct image_info
    {
        struct trans
        {
            VkImageLayout layout;
            VkAccessFlags2 access_mask;
            VkPipelineStageFlags2 stage_mask;
        };
        VkImage image;
        VkImageAspectFlags aspect_mask;
        trans src;
        trans dst;
    };

    template <std::same_as<image_info>... T>
    static void transition_image_layout(const CommandBufferView &commandBuffer,
                                        T... info) noexcept
    {
        std::array<VkImageMemoryBarrier2, sizeof...(T)> barriers{VkImageMemoryBarrier2{
            .sType = sType<VkImageMemoryBarrier2>(),
            // Specify the pipeline stages and access masks for the barrier
            .srcStageMask = info.src.stage_mask,
            .srcAccessMask = info.src.access_mask,
            .dstStageMask = info.dst.stage_mask,
            .dstAccessMask = info.dst.access_mask,
            // Specify the old and new layouts of the image
            .oldLayout = info.src.layout,
            .newLayout = info.dst.layout,
            // We are not changing the ownership between queues
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            // Specify the image to be affected by this barrier
            .image = info.image,
            // Define the subresource range (which parts of the image are affected)
            .subresourceRange = {.aspectMask = info.aspect_mask,
                                 .baseMipLevel = 0,
                                 .levelCount = VK_REMAINING_MIP_LEVELS,
                                 .baseArrayLayer = 0,
                                 .layerCount = VK_REMAINING_ARRAY_LAYERS}}...};
        commandBuffer.pipelineBarrier2(VkDependencyInfo{
            .sType = sType<VkDependencyInfo>(),
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
            .pImageMemoryBarriers = barriers.data()});
    }
};

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

struct Vertex // NOLINT
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord; // diff: [texture] 添加纹理坐标
};
static_assert(alignof(Vertex) == 4, "check alignof error");
static_assert(sizeof(Vertex) == 32, "check sizeof error");

namespace mesh
{
    using buffer_base = mcs::vulkan::memory::buffer_base;
    using auto_map_buffer = mcs::vulkan::memory::auto_map_buffer;
    using mcs::vulkan::memory::create_simple_buffer;
    using mcs::vulkan::memory::create_staging_buffer;

    using index_type = uint32_t;
    struct vertex_raw_data
    {
        std::vector<Vertex> vertices;
        std::vector<index_type> indices; // NOTE: 需要保留
        int count{};
        void clear()
        {
            vertices.clear();
            indices.clear();
        }
    };
    // 双缓冲顶点数据：每个飞行帧都有自己的缓冲区
    struct vertex_data
    {
        buffer_base vertexBuffer;
        buffer_base indexBuffer;
        std::vector<index_type> index_sequence; // NOTE: 索引序列
        VkDeviceAddress vertexBufferAddress = 0;
    };

    struct object_data_map
    {
        // diff: [test_model_matrix2] start
        auto_map_buffer objectDataBuffer; //diff: [test_dod2]
        VkDeviceAddress objectDataAddress = 0;
        // diff: [test_model_matrix2] end
    };

    using position_3d = glm::vec3;

    // buffer 内存要求
    static constexpr auto REQUIRE_BUFFER_USAGE =
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    static constexpr void createVertexBufferForFrame(auto &world, vertex_data &fb,
                                                     const std::span<const Vertex> &data)
    {
        auto &globalCtx = world.globalCtx;

        auto &commandPool = globalCtx.commandPool;
        auto &queue = globalCtx.queue;
        auto &device = globalCtx.device;

        const VkDeviceSize BUFFER_SIZE = sizeof(data[0]) * data.size();
        auto &destBuffer = fb.vertexBuffer;

        constexpr auto USAGE = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        // 直接使用重构后的 create_buffer 函数
        destBuffer = mcs::vulkan::memory::create_simple_buffer(
            device,
            {.size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | USAGE | REQUIRE_BUFFER_USAGE,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        // 创建暂存缓冲区并复制数据
        auto staging_buffer = create_staging_buffer(device, BUFFER_SIZE);
        ::memcpy(staging_buffer.mapPtr(), data.data(), BUFFER_SIZE);

        simple_copy_buffer(
            commandPool, queue, staging_buffer.buffer(), destBuffer.buffer(),
            VkBufferCopy{.srcOffset = {}, .dstOffset = {}, .size = BUFFER_SIZE});
    }

    static void createIndexBufferForFrame(auto &world, vertex_data &fb,
                                          const std::span<const index_type> &data)
    {
        auto &globalCtx = world.globalCtx;

        auto &commandPool = globalCtx.commandPool;
        auto &queue = globalCtx.queue;
        auto &device = globalCtx.device;

        fb.index_sequence = data | std::ranges::to<std::vector>();

        const VkDeviceSize BUFFER_SIZE = sizeof(data[0]) * data.size();
        auto &destBuffer = fb.indexBuffer;

        constexpr auto USAGE = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        // 直接使用重构后的 create_buffer 函数
        destBuffer = mcs::vulkan::memory::create_simple_buffer(
            device,
            {.size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | USAGE | REQUIRE_BUFFER_USAGE,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        // 创建暂存缓冲区并复制数据

        auto staging_buffer = create_staging_buffer(device, BUFFER_SIZE);
        ::memcpy(staging_buffer.mapPtr(), data.data(), BUFFER_SIZE);

        simple_copy_buffer(
            commandPool, queue, staging_buffer.buffer(), destBuffer.buffer(),
            VkBufferCopy{.srcOffset = {}, .dstOffset = {}, .size = BUFFER_SIZE});
    }

    static constexpr void createObjectBuffer(auto &world, object_data_map &fb,
                                             size_t BUFFER_SIZE)
    {
        auto &globalCtx = world.globalCtx;
        auto &device = globalCtx.device;

        fb.objectDataBuffer = mcs::vulkan::memory::auto_map_buffer(
            mcs::vulkan::memory::create_simple_buffer(
                device,
                {.size = BUFFER_SIZE,
                 .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | REQUIRE_BUFFER_USAGE,
                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            BUFFER_SIZE);
    }

    static void getVertexDataDeviceAddresses(auto &world, vertex_data &fb)
    {
        auto &globalCtx = world.globalCtx;
        auto &device = globalCtx.device;
        if (fb.vertexBuffer.buffer() != VK_NULL_HANDLE)
        {
            fb.vertexBufferAddress = device.getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(),
                 .buffer = fb.vertexBuffer.buffer()});
        }
    }
    static void getObjectDataDeviceAddresses(auto &world, object_data_map &fb)
    {
        auto &globalCtx = world.globalCtx;
        auto &device = globalCtx.device;
        if (fb.objectDataBuffer.buffer() != VK_NULL_HANDLE)
        {
            fb.objectDataAddress = device.getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(),
                 .buffer = fb.objectDataBuffer.buffer()});
        }
    }

    static auto get_topLeftLocal(const vertex_raw_data &raw_data) noexcept
    { // 计算包围盒和左上角坐标
        assert(!raw_data.vertices.empty());
        glm::vec3 minPos = raw_data.vertices[0].pos;
        glm::vec3 maxPos = raw_data.vertices[0].pos;
        for (const auto &v : raw_data.vertices)
        {
            minPos = glm::min(minPos, v.pos);
            maxPos = glm::max(maxPos, v.pos);
        }
        // 左上角 = (minX, maxY, maxZ)
        return glm::vec3(minPos.x, maxPos.y, maxPos.z);
    }

    static constexpr auto make_vertex_data(auto &world,
                                           const std::span<const Vertex> &vertices,
                                           const std::span<const index_type> &indices)
    {
        vertex_data cur;
        createVertexBufferForFrame(world, cur, vertices);
        createIndexBufferForFrame(world, cur, indices);
        getVertexDataDeviceAddresses(world, cur);
        return cur;
    }

    static constexpr auto make_object_data_map(auto &world, size_t buffer_size)
    {
        object_data_map cur;
        createObjectBuffer(world, cur, buffer_size);
        getObjectDataDeviceAddresses(world, cur);
        return cur;
    }

    static constexpr auto queueUpdate = [](std::vector<vertex_raw_data> &vertex_raw_data,
                                           uint32_t slot, std::vector<Vertex> vertices,
                                           std::vector<index_type> indices) {
        auto &queue_data = vertex_raw_data[slot];
        queue_data.vertices = std::move(vertices);
        queue_data.indices = std::move(indices);
        queue_data.count = MAX_FRAMES_IN_FLIGHT;
    };

    template <class T, size_t N>
    static constexpr auto gen_array_value(const auto &gen_value) noexcept(
        noexcept(gen_value()))
    {
        constexpr auto [... I] = std::make_index_sequence<N>{};
        return std::array<T, N>{((void)I, gen_value())...};
    }

}; // namespace mesh

// vma conifg ---------------------------------------------
constexpr auto VMA_USE_BIND_MEMORY2 = true;
constexpr auto VMA_USE_MEMORY_BUDGET = true;
constexpr auto VMA_USE_BUFFER_DEVICE_ADDRESS = true;

static auto addVmaInitExtension(std::vector<const char *> &requiredDeviceExtension)
{
    // NOTE: 配置 vma 的 flags
    VmaAllocatorCreateFlags vma_flags = 0;
    // @see
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html#:~:text=of%20this%20Function.-,Enabling%20extensions,-VMA%20can%20automatically
#if VK_USE_PLATFORM_WIN32_KHR
    std::cout << "vma_flags config with WIN32 \n";
    requiredDeviceExtension.emplace_back("VK_KHR_external_memory_win32");
    vma_flags |= VMA_ALLOCATOR_CREATE_KHR_EXTERNAL_MEMORY_WIN32_BIT;
#endif
    if constexpr (VMA_USE_BIND_MEMORY2)
    {
        std::cout << "vma_flags config with VK_KHR_bind_memory2 \n";
        requiredDeviceExtension.emplace_back("VK_KHR_bind_memory2");
        vma_flags |= VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT;
    }
    if constexpr (VMA_USE_MEMORY_BUDGET)
    {
        std::cout << "vma_flags config with VK_EXT_memory_budget \n";
        requiredDeviceExtension.emplace_back("VK_EXT_memory_budget");
        vma_flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    if constexpr (VMA_USE_BUFFER_DEVICE_ADDRESS)
    {
        std::cout << "vma_flags config with VK_KHR_buffer_device_address \n";
        requiredDeviceExtension.emplace_back("VK_KHR_buffer_device_address");
        vma_flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }
    return vma_flags;
}
static void updateVmaFlag(VmaAllocatorCreateFlags &vma_flags,
                          PhysicalDevice &physicalDevice,
                          std::vector<const char *> &requiredDeviceExtension)
{
    VkPhysicalDeviceProperties properties = physicalDevice.getProperties();
    if (properties.vendorID == 0x1002)
    {
        std::cout << "vma_flags config with amd gpu \n";
        requiredDeviceExtension.emplace_back(
            VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME);
        vma_flags |= VMA_ALLOCATOR_CREATE_AMD_DEVICE_COHERENT_MEMORY_BIT;
    }
}

// diff: [Uniform] start
class UniformBufferObject
{
  public:
    // glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};
// diff: [Uniform] end

// diff: [texture] start

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#endif //! STB_IMAGE_IMPLEMENTATION

void generateCheckerboardTexture(uint8_t *pixels, int width, int height,
                                 uint8_t color1[4], uint8_t color2[4],
                                 int checkerSize = 32)
{
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int tileX = x / checkerSize;
            int tileY = y / checkerSize;
            bool isEven = ((tileX + tileY) % 2) == 0;

            uint8_t *pixel = &pixels[(y * width + x) * 4];
            if (isEven)
            {
                pixel[0] = color1[0];
                pixel[1] = color1[1];
                pixel[2] = color1[2];
                pixel[3] = color1[3];
            }
            else
            {
                pixel[0] = color2[0];
                pixel[1] = color2[1];
                pixel[2] = color2[2];
                pixel[3] = color2[3];
            }
        }
    }
}

void generateGradientTexture(uint8_t *pixels, int width, int height, uint8_t topColor[4],
                             uint8_t bottomColor[4])
{
    for (int y = 0; y < height; ++y)
    {
        float t = static_cast<float>(y) / (height - 1);
        for (int x = 0; x < width; ++x)
        {
            uint8_t *pixel = &pixels[(y * width + x) * 4];

            pixel[0] = static_cast<uint8_t>(topColor[0] * (1 - t) + bottomColor[0] * t);
            pixel[1] = static_cast<uint8_t>(topColor[1] * (1 - t) + bottomColor[1] * t);
            pixel[2] = static_cast<uint8_t>(topColor[2] * (1 - t) + bottomColor[2] * t);
            pixel[3] = 255;
        }
    }
}

enum class perspective_member : std::uint8_t
{
    eFOVY,
    eASPECT,
    eZ_NEAR,
    eZ_FAR
};
// NOTE: WS 依旧有效，超过+-10就完蛋
template <perspective_member member>
auto update_perspective(camera_interface &camera, float step) noexcept
{
    using enum perspective_member;
    if (member == eFOVY)
        camera.refPerspectiveMatrix().fovy += step;
    else if (member == eASPECT)
        camera.refPerspectiveMatrix().aspect += step;
    else if (member == eZ_NEAR)
        camera.refPerspectiveMatrix().zNear += step;
    else
        camera.refPerspectiveMatrix().zFar += step;

    auto [fovy, aspect, zNear, zFar] = camera.refPerspectiveMatrix();
    std::print("Key::eW: [fovy: {:f},aspect: {:f},zNear: {:f},zFar: {:f}]\n", fovy,
               aspect, zNear, zFar);
}
// diff: [camera_perspective] end

// diff: [camera_model] start
struct model_matrix
{
    // NOLINTBEGIN
    glm::vec3 translation = glm::vec3(0.0F, 0.0F, 0.0F);    // 平移（默认无偏移）
    glm::quat rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F); // 旋转（默认无旋转）
    glm::vec3 scale = glm::vec3(1.0F, 1.0F, 1.0F);          // 缩放（默认无缩放）
    // NOLINTEND

    // ========== 核心：计算最终的model矩阵（固定顺序：缩放→旋转→平移） ==========
    constexpr auto operator()() const noexcept
    {
        // 1. 缩放 → 2. 旋转 → 3. 平移（3D变换的标准顺序）
        auto model = glm::mat4(1.0F);
        // 平移
        model = glm::translate(model, translation);
        // 旋转（四元数转矩阵）
        model = model * glm::toMat4(rotation);
        // 缩放
        model = glm::scale(model, scale);
        return model;
    }
};

// diff: [camera_model] end

//diff: [test_indirectdraw] start
// ---- 批量绘制相关 ----
struct ObjectInfo
{
    uint64_t vertexBufferAddress;
    uint64_t objectDataAddress;
    uint32_t textureIndex;
    uint32_t samplerIndex;
    uint32_t objectId; // diff: [test_indirectdraw_no_pick] 新增
};

struct PerFrameBatch
{
    mcs::vulkan::memory::auto_map_buffer mergedIndexBuffer{};
    VkDeviceSize mergedIndexCapacity = 0;

    mcs::vulkan::memory::auto_map_buffer objectInfoBuffer{};
    VkDeviceSize objectInfoCapacity = 0;

    mcs::vulkan::memory::auto_map_buffer indirectDrawBuffer{};
    VkDeviceSize indirectCmdCapacity = 0;

    uint32_t drawCount = 0;
};
//diff: [test_indirectdraw] end

//diff: [test_indirectdraw2] start
// NOLINTBEGIN

using mcs::vulkan::load::raw_stbi_image;

using Sampler = mcs::vulkan::Sampler;
using create_sampler = mcs::vulkan::tool::create_sampler;

auto generateGradientTexture(const LogicalDevice &device, const CommandPool &pool,
                             const Queue &queue, int textureType = 0, bool mipmap = true)
{
    auto texWidth_ = 256;
    auto texHeight_ = 256;

    mcs::vulkan::memory::create_texture create_texture{
        [](mcs::vulkan::memory::create_texture::image_info imageInfo)
            -> mcs::vulkan::memory::create_texture::create_info {
            return {.imageType = VK_IMAGE_TYPE_2D,
                    .format = VK_FORMAT_R8G8B8A8_SRGB,
                    .extent = {.width = imageInfo.extent.width,
                               .height = imageInfo.extent.height,
                               .depth = 1},
                    .mipLevels = imageInfo.mipLevels,
                    .arrayLayers = 1,
                    .samples = VK_SAMPLE_COUNT_1_BIT,
                    .tiling = VK_IMAGE_TILING_OPTIMAL,
                    .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        },
        mcs::vulkan::memory::gen_memory_allocate_info(
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        [](VkImageCreateInfo imageCreateInfo,
           VkImage image) -> mcs::vulkan::memory::create_texture::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = imageCreateInfo.mipLevels,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        }};

    auto imageSize = texWidth_ * texHeight_ * STBI_rgb_alpha;
    auto pixels = std::make_unique_for_overwrite<uint8_t[]>(imageSize);
    if (textureType == 1)
    {
        // 生成红蓝棋盘纹理
        uint8_t color1[4] = {255, 0, 0, 255}; // 红色
        uint8_t color2[4] = {0, 0, 255, 255}; // 蓝色
        generateCheckerboardTexture(pixels.get(), texWidth_, texHeight_, color1, color2);
    }
    else if (textureType == 2)
    {
        // 生成绿色到黄色渐变纹理
        uint8_t topColor[4] = {0, 255, 0, 255};      // 绿色
        uint8_t bottomColor[4] = {255, 255, 0, 255}; // 黄色
        generateGradientTexture(pixels.get(), texWidth_, texHeight_, topColor,
                                bottomColor);
    }
    return create_texture.build(
        device, pool, queue,
        mcs::vulkan::memory::create_texture::image_info{
            .extent = {.width = static_cast<uint32_t>(texWidth_),
                       .height = static_cast<uint32_t>(texHeight_)},
            .pixels =
                std::span<const uint8_t>{pixels.get(), static_cast<uint64_t>(imageSize)},
            .mipLevels = mipmap ? mcs::vulkan::memory::create_texture::getMipLevels(
                                      texWidth_, texHeight_)
                                : 1});
}
// NOLINTEND
//diff: [test_indirectdraw2] end

//diff: [test_dod4] start
consteval int parse_int(std::string_view sv)
{
    int result = 0;
    for (char c : sv)
    {
        if (c < '0' || c > '9')
            throw std::meta::exception{"sv is not number", std::meta::current_function()};
        result = result * 10 + (c - '0');
    }
    return result;
}
template <mcs::vulkan::ecs::static_string prefix, size_t min, size_t max>
constexpr void invoke_aggregate_ranges(auto &object, auto &&...args)
{
    using T = std::remove_cvref_t<decltype(object)>;
    constexpr auto members = T::members; //NOTE: 来自继承的
    template for (constexpr auto I : std::views::indices(members.size()))
    {
        constexpr auto member_name = T::template get_member_name<I>().view();
        constexpr auto prefix_name = prefix.view();
        if constexpr (member_name.starts_with(prefix_name))
        {
            constexpr auto num_str = member_name.substr(prefix_name.size());
            constexpr int num = parse_int(num_str);
            if constexpr (num >= min && num <= max)
                object.[:members[I]:](std::forward<decltype(args)>(args)...);
        }
    }
};
//diff: [test_dod4] end

int main()
try
{
#ifdef VERT_SHADER_PATH
    std::cout << "VERT_SHADER_PATH: " << VERT_SHADER_PATH << '\n';
#endif
#ifdef FRAG_SHADER_PATH
    std::cout << "FRAG_SHADER_PATH: " << FRAG_SHADER_PATH << '\n';
#endif
    raii_vulkan ctx{};
    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT

    constexpr auto APIVERSION = VK_API_VERSION_1_4;
    static_assert(vkApiVersion(1, 4, 0) == VK_API_VERSION_1_4);
    static_assert(vkApiVersion(0, 1, 4, 0) == VK_API_VERSION_1_4);

    auto enables = enable_intance_build{}
                       .enableDebugExtension()
                       .enableValidationLayer()
                       .enableSurfaceExtension<surface>();

    enables.check();
    enables.print();

    Instance instance =
        create_instance{}
            .setCreateInfo(
                {.applicationInfo = {.pApplicationName = "Hello Triangle",
                                     .applicationVersion = vkMakeVersion(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = vkMakeVersion(1, 0, 0),
                                     .apiVersion = APIVERSION},
                 .enabledLayers = enables.enabledLayers(),
                 .enabledExtensions = enables.enabledExtensions()})
            .build();
    auto debuger = create_debugger{}
                       .setCreateInfo(create_debugger::defaultCreateInfo())
                       .build(instance);

    std::vector<const char *> requiredDeviceExtension = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME}; // NOLINTEND
    requiredDeviceExtension.emplace_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceVulkan12Features, VkPhysicalDeviceVulkan11Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        enablefeatureChain = {
            {.features =
                 {
                     /*
                    当不同附件的混合状态（包括 colorWriteMask）不同时，即使所有附件的 blendEnable 都是 VK_FALSE，也必须启用 independentBlend 特性，否则就是非法。
                    你的两个附件 colorWriteMask 不同（一个包含 A，一个只有 R|G），因此需要该特性
                    */
                     //diff: [test_indirectdraw_no_pick] start
                     .independentBlend = VK_TRUE, // 新增
                     //diff: [test_indirectdraw_no_pick] end
                     //NOTE: gcc 要求。严格的初始化顺序。 multiDrawIndirect 要在前面
                     .multiDrawIndirect = VK_TRUE, //diff: [test_indirectdraw]
                     .samplerAnisotropy = VK_TRUE,
                     .shaderInt64 = VK_TRUE,

                 }},
            {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
            {
                .descriptorIndexing = VK_TRUE,
                .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
                .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
                .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
                .descriptorBindingPartiallyBound = VK_TRUE,
                .descriptorBindingVariableDescriptorCount = VK_TRUE,

                .runtimeDescriptorArray = VK_TRUE,

                .scalarBlockLayout = VK_TRUE,
                .bufferDeviceAddress = VK_TRUE,

            },
            {
                .shaderDrawParameters = VK_TRUE //diff: [test_indirectdraw]
            },
            {.extendedDynamicState = VK_TRUE}};
    VmaAllocatorCreateFlags vma_flags = addVmaInitExtension(requiredDeviceExtension);

    auto [id [[maybe_unused]], physical_device [[maybe_unused]]] =
        physical_device_selector{instance}
            .requiredDeviceExtension(requiredDeviceExtension)
            .requiredProperties([](const VkPhysicalDeviceProperties
                                       &device_properties) constexpr noexcept {
                return device_properties.apiVersion >= VK_API_VERSION_1_3;
            })
            .requiredQueueFamily(
                [](const VkQueueFamilyProperties &qfp) constexpr noexcept {
                    return !!(qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT);
                })
            .requiredFeatures([](const PhysicalDevice &physicalDevice) constexpr noexcept
                                  -> bool {
                auto query = structure_chain<
                    VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceVulkan12Features, VkPhysicalDeviceVulkan11Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>{{}, {}, {}, {}, {}};
                physicalDevice.getFeatures2(&query.head());
                auto &features2 = query.template get<VkPhysicalDeviceFeatures2>();
                auto &query_vulkan13_features =
                    query.template get<VkPhysicalDeviceVulkan13Features>();
                auto &query_vulkan12_features =
                    query.template get<VkPhysicalDeviceVulkan12Features>();
                auto &query_vulkan11_features =
                    query.template get<VkPhysicalDeviceVulkan11Features>();
                auto &query_extended_dynamic_state_features =
                    query.template get<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>();
                return features2.features.samplerAnisotropy &&
                       features2.features.shaderInt64 &&
                       features2.features.multiDrawIndirect && //diff: [test_indirectdraw]
                       features2.features
                           .independentBlend && //diff: [test_indirectdraw_no_pick]
                       query_vulkan13_features.dynamicRendering &&
                       query_vulkan13_features.synchronization2 &&
                       query_vulkan12_features.bufferDeviceAddress &&
                       query_vulkan12_features.scalarBlockLayout &&
                       query_vulkan12_features.runtimeDescriptorArray &&
                       query_vulkan12_features.descriptorBindingPartiallyBound &&
                       query_vulkan12_features.descriptorBindingVariableDescriptorCount &&
                       query_vulkan12_features.descriptorIndexing &&
                       query_vulkan12_features
                           .shaderSampledImageArrayNonUniformIndexing &&
                       query_vulkan11_features
                           .shaderDrawParameters && //diff: [test_indirectdraw]
                       query_extended_dynamic_state_features.extendedDynamicState;
            })
            .select()[0];

    // NOTE: 根据GPU 配置 vma
    // updateVmaFlag(vma_flags, physical_device, requiredDeviceExtension);

    mcs::vulkan::surface auto surface = surface_impl(physical_device, window);
    const uint32_t GRAPHICS_QUEUE_FAMILY_IDX =
        queue_family_index_selector{physical_device}
            .requiredQueueFamily([&](const VkQueueFamilyProperties &qfp,
                                     uint32_t queueFamilyIndex) -> bool {
                return (qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                       physical_device.getSurfaceSupportKHR(queueFamilyIndex, *surface);
            })
            .select()[0];

    LogicalDevice device =
        create_logical_device{}
            .setCreateInfo({
                .pNext = make_pNext(enablefeatureChain),
                .queueCreateInfos = create_logical_device::makeQueueCreateInfos(
                    create_logical_device::queue_create_info{
                        .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX,
                        .queueCount = 1,
                        .queuePrioritie = 1.0}),
                .enabledExtensions = requiredDeviceExtension,
            })
            .build(physical_device);
    requiredDeviceExtension.clear();

    // NOTE: 逻辑设备确定后，就能初始化 vma了
    // raii_vma vma{{.flags = vma_flags,
    //               .physicalDevice = *physical_device,
    //               .device = *device,
    //               .instance = *instance,
    //               .vulkanApiVersion = APIVERSION}};
    // [[maybe_unused]] VmaAllocator allocator = vma.allocator();

    const auto GRAPHICS_AND_PRESENT = Queue(
        device, {.queue_family_index = GRAPHICS_QUEUE_FAMILY_IDX, .queue_index = 0});

    auto swapchainBuild =
        create_swap_chain{surface, device}
            .setCreateInfo(
                {.changeMinImageCount =
                     [](uint32_t minImageCount) noexcept { return minImageCount + 1; },
                 .candidateSurfaceFormats = {{.format = VK_FORMAT_B8G8R8A8_SRGB,
                                              .colorSpace =
                                                  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}},
                 .imageArrayLayers = 1,
                 .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                 .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                 .queueFamilyIndices = {},
                 .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                 .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                 .candidatePresentModes = {VK_PRESENT_MODE_MAILBOX_KHR,
                                           VK_PRESENT_MODE_IMMEDIATE_KHR},
                 .clipped = VK_TRUE})
            .setViewCreateInfo(
                {.viewType = VK_IMAGE_VIEW_TYPE_2D,
                 .components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                .a = VK_COMPONENT_SWIZZLE_IDENTITY},
                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1}});
    auto swapchain = swapchainBuild.build();

    CommandPool commandPool =
        create_command_pool{}
            .setCreateInfo({.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                            .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX})
            .build(device);

    constexpr int MAX_TEXTURES = 64;       // 预分配最大纹理槽位数（足够大）
    std::set<uint32_t> activeTextureSlots; // 记录当前已占用的槽位
    constexpr int SAMPLER_COUNT = 2;       // 创建2个不同的采样器

    std::array<VkDescriptorBindingFlags, 4> bindingFlags = {
        // 绑定0：Uniform Buffer - 通常不需要绑定后更新
        0,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, //允许不连续占用纹理槽（如 1、3、5、33）
        0,
        0, // 对应 binding 3（Storage Buffer）
    };

    auto descriptorSetLayout =
        create_descriptor_set_layout{}
            .setCreateInfo(
                {.pNext = make_pNext(
                     structure_chain<VkDescriptorSetLayoutBindingFlagsCreateInfo>{
                         {.bindingCount = static_cast<uint32_t>(bindingFlags.size()),
                          .pBindingFlags = bindingFlags.data()}}),
                 .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                 .bindings =
                     {
                         VkDescriptorSetLayoutBinding{
                             .binding = 0,
                             .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
                         VkDescriptorSetLayoutBinding{
                             .binding = 1,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .descriptorCount = MAX_TEXTURES,
                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                             .pImmutableSamplers = nullptr,
                         },
                         VkDescriptorSetLayoutBinding{
                             .binding = 2,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                             .descriptorCount = SAMPLER_COUNT,
                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                             .pImmutableSamplers = nullptr,
                         }, //diff: [test_indirectdraw] end
                         VkDescriptorSetLayoutBinding{
                             .binding = 3,
                             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                         }, //diff: [test_indirectdraw] end
                     }})
            .build(device);

    auto descriptorPool =
        create_descriptor_pool{}
            .setCreateInfo(
                {.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
                          VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                 .maxSets = MAX_FRAMES_IN_FLIGHT,
                 .poolSizes =
                     {
                         VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                              .descriptorCount = MAX_FRAMES_IN_FLIGHT},
                         VkDescriptorPoolSize{
                             .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .descriptorCount = MAX_TEXTURES * static_cast<uint32_t>(
                                                                   MAX_FRAMES_IN_FLIGHT),
                         },
                         VkDescriptorPoolSize{
                             .type = VK_DESCRIPTOR_TYPE_SAMPLER,
                             .descriptorCount =
                                 (SAMPLER_COUNT) *
                                 static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                         }, //diff: [test_indirectdraw] start
                         VkDescriptorPoolSize{
                             .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .descriptorCount =
                                 MAX_FRAMES_IN_FLIGHT}, //diff: [test_indirectdraw] end
                     }})
            .build(device);
    auto descriptorSets = descriptorPool.allocateDescriptorSets(
        {.descriptorSets = std::vector<VkDescriptorSetLayout>{MAX_FRAMES_IN_FLIGHT,
                                                              *descriptorSetLayout}});
    constexpr VkDeviceSize BUFFER_SIZE = sizeof(UniformBufferObject);
    // diff: [test_dod7] start: 不再是 vma 的内存
    std::array<mcs::vulkan::memory::auto_map_buffer, MAX_FRAMES_IN_FLIGHT> uniformBuffers;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        uniformBuffers[i] = mcs::vulkan::memory::auto_map_buffer(
            mcs::vulkan::memory::create_simple_buffer(
                device,
                {.size = BUFFER_SIZE,
                 .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            BUFFER_SIZE);
    }
    // diff: [test_dod7] end
    uint32_t create_texture_mipLevels = 1;
    mcs::vulkan::memory::create_texture create_texture{
        [&](mcs::vulkan::memory::create_texture::image_info imageInfo)
            -> mcs::vulkan::memory::create_texture::create_info {
            create_texture_mipLevels = imageInfo.mipLevels;
            return {.imageType = VK_IMAGE_TYPE_2D,
                    .format = VK_FORMAT_R8G8B8A8_SRGB,
                    .extent = {.width = imageInfo.extent.width,
                               .height = imageInfo.extent.height,
                               .depth = 1},
                    .mipLevels = imageInfo.mipLevels,
                    .arrayLayers = 1,
                    .samples = VK_SAMPLE_COUNT_1_BIT,
                    .tiling = VK_IMAGE_TILING_OPTIMAL,
                    .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        },
        mcs::vulkan::memory::gen_memory_allocate_info(
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        [](VkImageCreateInfo imageCreateInfo,
           VkImage image) -> mcs::vulkan::memory::create_texture::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = imageCreateInfo.mipLevels,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        }};

    std::map<uint32_t, mcs::vulkan::memory::resource> textureMap; // 槽位 → 纹理
    {
        raw_stbi_image{"textures/texture.jpg", STBI_rgb_alpha};

        textureMap.emplace(0, create_texture.templateForImage2d(
                                  device, commandPool, GRAPHICS_AND_PRESENT,
                                  raw_stbi_image{"textures/texture.jpg", STBI_rgb_alpha},
                                  true));
    }
    activeTextureSlots.insert(0);

    textureMap.emplace(3,
                       generateGradientTexture(device, commandPool, GRAPHICS_AND_PRESENT,
                                               1)); // 棋盘
    activeTextureSlots.insert(3);

    textureMap.emplace(9,
                       generateGradientTexture(device, commandPool, GRAPHICS_AND_PRESENT,
                                               2)); // 渐变
    activeTextureSlots.insert(9);

    std::vector<Sampler> samplers;
    samplers.reserve(SAMPLER_COUNT);
    samplers.emplace_back(
        create_sampler{}
            .setCreateInfo(create_sampler::templateLinear())
            .enableAnisotropy(
                device.physicalDevice()->getProperties().limits.maxSamplerAnisotropy)
            .updateMaxLodByMipmap(create_texture_mipLevels)
            .build(device)); // 线性采样器
    samplers.emplace_back(create_sampler{}
                              .setCreateInfo(create_sampler::templateNearest())
                              .build(device)); // 最近邻采样器

    //NOTE: 不需要保存 VkDescriptorImageInfo。 有textureMap和索引够了
    {
        // 采样器信息：只一份，所有帧共用
        auto samplerInfos =
            samplers | std::views::transform([](const auto &s) constexpr noexcept {
                return VkDescriptorImageInfo{.sampler = *s,
                                             .imageView = nullptr,
                                             .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
            }) |
            std::ranges::to<std::vector>();

        // 纹理信息：只一份，所有帧共用
        std::vector<VkDescriptorImageInfo> imageInfos(MAX_TEXTURES);
        for (uint32_t slot : activeTextureSlots)
        {
            imageInfos[slot] = VkDescriptorImageInfo{
                .sampler = nullptr,
                .imageView = textureMap[slot].imageView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }

        // 一个循环搞定所有帧的描述符更新
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            std::vector<VkWriteDescriptorSet> writes;

            // 绑定 0: Uniform Buffer
            VkDescriptorBufferInfo uniformInfo{
                .buffer = uniformBuffers[i].buffer(), .offset = 0, .range = BUFFER_SIZE};
            writes.push_back({
                .sType = sType<VkWriteDescriptorSet>(),
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &uniformInfo,
            });

            // 绑定 1: Sampled Images（指向全局 imageInfos 的对应槽位）
            for (uint32_t slot : activeTextureSlots)
            {
                writes.push_back({
                    .sType = sType<VkWriteDescriptorSet>(),
                    .dstSet = descriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = slot,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .pImageInfo =
                        &imageInfos[slot], // 安全：imageInfos 生命周期远长于此处
                });
            }

            // 绑定 2: Samplers
            writes.push_back({
                .sType = sType<VkWriteDescriptorSet>(),
                .dstSet = descriptorSets[i],
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = SAMPLER_COUNT,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .pImageInfo = samplerInfos.data(),
            });
            device.updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                        writes.data(), 0, nullptr);
        }
    }

    // diff: [test_indirectdraw] start 为每个帧初始化 SSBO（binding 3）占位缓冲，并更新描述符
    std::array<PerFrameBatch, MAX_FRAMES_IN_FLIGHT> batches;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        // 创建 16 字节的极小 SSBO，仅用于满足描述符绑定要求
        constexpr auto BUFFER_SIZE = 16;
        batches[i].objectInfoBuffer = mcs::vulkan::memory::auto_map_buffer(
            mcs::vulkan::memory::create_simple_buffer(
                device,
                {.size = BUFFER_SIZE,
                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            BUFFER_SIZE);
    }
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo objInfo = {
            .buffer = batches[i].objectInfoBuffer.buffer(),
            .offset = 0,
            .range = VK_WHOLE_SIZE,
        };
        VkWriteDescriptorSet write = {
            .sType = sType<VkWriteDescriptorSet>(),
            .dstSet = descriptorSets[i],
            .dstBinding = 3,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &objInfo,
        };
        device.updateDescriptorSets(1, &write, 0, nullptr);
    } // diff: [test_indirectdraw] end

    auto depthResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = mcs::vulkan::memory::create_resources::findSupportedFormat(
             physical_device,
             std::array<VkFormat, 3>{VkFormat::VK_FORMAT_D32_SFLOAT,
                                     VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT,
                                     VkFormat::VK_FORMAT_D24_UNORM_S8_UINT},
             VkImageTiling::VK_IMAGE_TILING_OPTIMAL,
             VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
         .extent = {.width = swapchain.refImageExtent().width,
                    .height = swapchain.refImageExtent().height,
                    .depth = 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = physical_device.getMaxUsableSampleCount(),
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VkImageUsageFlagBits::VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
         .sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED},
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        [](VkImageCreateInfo imageCreateInfo, VkImage image) noexcept
            -> mcs::vulkan::memory::create_image::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        });

    auto depthResource = depthResourcesBuild.build(device);
    const auto &depthFormat_ref = depthResourcesBuild.refCreateInfoFormat();
    std::cout << "depthResource hasStencilComponent: "
              << depthResourcesBuild.hasStencilComponent() << '\n';

    auto msaaResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = swapchainBuild.refImageFormat(),
         .extent = {.width = swapchain.refImageExtent().width,
                    .height = swapchain.refImageExtent().height,
                    .depth = 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = physical_device.getMaxUsableSampleCount(),
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED},
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        [](VkImageCreateInfo imageCreateInfo, VkImage image) noexcept
            -> mcs::vulkan::memory::create_image::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        });
    auto msaaResource = msaaResourcesBuild.build(device);

    auto pipelineLayout =
        create_pipeline_layout{}
            .setCreateInfo({.setLayouts = {*descriptorSetLayout},
                            // diff: [test_indirectdraw] 主绘制不再使用 push constants
                            .pushConstantRanges = {}})
            .build(device);

    using stage_info = create_graphics_pipeline::stage_info;

    //diff: [test_indirectdraw_no_pick] start
    // 定义两个颜色附件格式
    std::array<VkFormat, 2> mainColorFormats = {
        swapchainBuild.refImageFormat(), // location 0 (swapchain)
        VK_FORMAT_R32G32_UINT            // location 1 (picking)
    }; //diff: [test_indirectdraw_no_pick] end

    auto graphicsPipeline =
        create_graphics_pipeline{}
            .setCreateInfo(
                {.pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                     {//diff: [test_indirectdraw_no_pick] start
                      .colorAttachmentCount =
                          static_cast<uint32_t>(mainColorFormats.size()),
                      .pColorAttachmentFormats = mainColorFormats.data(),
                      //diff: [test_indirectdraw_no_pick] end
                      .depthAttachmentFormat = depthFormat_ref}}),
                 .stages = create_graphics_pipeline::makeStages(
                     stage_info{.stage = VK_SHADER_STAGE_VERTEX_BIT,
                                .filePath = VERT_SHADER_PATH,
                                .pName = "main"},
                     stage_info{.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                .filePath = FRAG_SHADER_PATH,
                                .pName = "main"}),
                 .vertexInputState = {},
                 .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                        .primitiveRestartEnable = VK_FALSE},
                 .tessellationState = {},
                 .viewportState = {.viewports = {VkViewport{}}, .scissors = {VkRect2D{}}},
                 .rasterizationState = {.depthClampEnable = VK_FALSE,
                                        .rasterizerDiscardEnable = VK_FALSE,
                                        .polygonMode = VK_POLYGON_MODE_FILL,
                                        .cullMode = VK_CULL_MODE_NONE,
                                        .frontFace = VK_FRONT_FACE_CLOCKWISE,
                                        .depthBiasEnable = VK_FALSE,
                                        .lineWidth = 1.0F},
                 .multisampleState =
                     {
                         .rasterizationSamples =
                             physical_device.getMaxUsableSampleCount(),
                         .sampleShadingEnable = VK_FALSE,
                         .minSampleShading = 1.0F,
                         .sampleMask = {},
                         .alphaToCoverageEnable = VK_FALSE,
                         .alphaToOneEnable = VK_FALSE,
                     },
                 .depthStencilState = {.depthTestEnable = VK_TRUE,
                                       .depthWriteEnable = VK_TRUE,
                                       .depthCompareOp = VK_COMPARE_OP_LESS,
                                       .depthBoundsTestEnable = VK_FALSE,
                                       .stencilTestEnable = VK_FALSE,
                                       .front = {},
                                       .back = {},
                                       .minDepthBounds = 0.0F,
                                       .maxDepthBounds = 1.0F},
                 .colorBlendState = {.logicOpEnable = VK_FALSE,
                                     .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                                     .attachments =
                                         {
                                             {.blendEnable = VK_FALSE, // 关闭混合
                                              .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                                VK_COLOR_COMPONENT_G_BIT |
                                                                VK_COLOR_COMPONENT_B_BIT |
                                                                VK_COLOR_COMPONENT_A_BIT},
                                             //diff: [test_indirectdraw_no_pick] start
                                             // location 1 (R32G32_UINT，不能混合)
                                             {.blendEnable = VK_FALSE,
                                              .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                                VK_COLOR_COMPONENT_G_BIT},
                                             //diff: [test_indirectdraw_no_pick] end
                                         }},
                 .dynamicState = {.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR}},
                 .layout = *pipelineLayout})
            .build(device);

    static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;
    CommandBuffers commandBuffers =
        commandPool.allocateCommandBuffers({.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                            .commandBufferCount = MAX_FRAMES_IN_FLIGHT});
    frame_context<MAX_FRAMES_IN_FLIGHT> frameContext{device, swapchain.imagesSize()};

    // NOLINTBEGIN
    // 顶点数据（四边形由两个三角形组成）
    std::vector<Vertex> vertices = {
        {{-0.5f, -0.5f, 0}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}}, // 左下
        {{0.5, -0.5, 0}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},    // 右下
        {{0.5, 0.5, 0}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},     // 右上
        {{-0.5, 0.5, 0}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F}}     // 左上
    };
    // 索引数据
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    // mesh_base &input_mesh = autoSpinMeshes[0];

    // diff: [test_dod2] start 定义全部数据

    struct model_state
    {
        mcs::vulkan::event::position2d_event last_pos{};
        bool is_middle_button_pressed = false;
        bool is_left_button_pressed = false;
        bool is_right_button_pressed_last = false; // 新增：记录上一帧右键状态
        mcs::vulkan::event::position2d_event last_left_pos{};
        model_matrix model_matrix{};
    };
    struct object_data
    {
        glm::mat4 matrix;
    };

    // diff: [test_dod2] end

    //diff: [test_dod3.cpp] start
    // updateVertexData
    using auto_spin_data_type =
        gen_soa_aggregate<{"vertexData", ^^mesh::vertex_data, MAX_FRAMES_IN_FLIGHT},
                          {"objectDataMap", ^^mesh::object_data_map,
                           MAX_FRAMES_IN_FLIGHT},
                          {"vertexUpdateQueue", ^^mesh::vertex_raw_data},
                          {"textureData", ^^uint32_t}, {"samplerData", ^^uint32_t}>;
    auto_spin_data_type autoSpinStore{1};

    using interactive_type =
        gen_soa_aggregate<{"vertexData", ^^mesh::vertex_data, MAX_FRAMES_IN_FLIGHT},
                          {"objectDataMap", ^^mesh::object_data_map,
                           MAX_FRAMES_IN_FLIGHT},
                          {"vertexUpdateQueue", ^^mesh::vertex_raw_data},
                          {"textureData", ^^uint32_t}, {"samplerData", ^^uint32_t},
                          {"topLeftLocal", ^^glm::vec3}, {"modelState", ^^model_state}>;
    interactive_type interactiveStore{1};

    // NOTE: spinElapsed 全部mesh共享？
    auto autoSpinStoreShareData =
        make_aggregate<"autoSpinStoreShareData", "spinElapsed">(float{});
    auto soaCtx = make_aggregate_ref<"soaCtx", "autoSpinStore", "interactiveStore",
                                     "autoSpinStoreShareData">(
        autoSpinStore, interactiveStore, autoSpinStoreShareData);

    //diff: [test_dod3.cpp] end

    struct record_info
    {
        uint32_t current_frame;
        uint32_t image_index;
    };

    camera_interface camera =
        camera_interface{}
            .setViewsMatrix({.eye = glm::vec3(0.0F, 0.0F, 2.0F),
                             .center = glm::vec3(0.0F, 0.0F, 0.0F),
                             .up = glm::vec3(0.0F, 1.0F, 0.0F)})
            .setPerspectiveMatrix(
                {.fovy = glm::radians(45.0F),
                 .aspect = swapchain.refImageExtent().width /
                           static_cast<float>(swapchain.refImageExtent().height),
                 .zNear = 0.1f,
                 .zFar = 10.0F});

    glfw_input input{};

    // ========== 拾取资源 ==========
    // 创建拾取图像（R32G32_UINT）

    auto pickResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = VK_FORMAT_R32G32_UINT,
         .extent = {.width = WIDTH, .height = HEIGHT, .depth = 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = physical_device.getMaxUsableSampleCount(), //NOTE: 和主管线一样
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage =
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT //diff: [test_indirectdraw_no_pick] 没有任何代码从 pickingImage 进行拷贝或读取，因此 TRANSFER_SRC_BIT 完全没有必要
         // | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
         ,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED},
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        [](VkImageCreateInfo imageCreateInfo, VkImage image) noexcept
            -> mcs::vulkan::memory::create_image::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        });
    auto pickResource = pickResourcesBuild.build(device);

    //  创建解析图像（单样本）

    auto resolveResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = pickResourcesBuild.refCreateInfo().imageType,
         .format = pickResourcesBuild.refCreateInfo().format,
         .extent = pickResourcesBuild.refCreateInfo().extent,
         .mipLevels = pickResourcesBuild.refCreateInfo().mipLevels,
         .arrayLayers = pickResourcesBuild.refCreateInfo().arrayLayers,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = pickResourcesBuild.refCreateInfo().tiling,
         .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |   // 作为 resolve 目标
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT |   // 用于复制像素到缓冲区
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT // ← 必须！
         ,
         .sharingMode = pickResourcesBuild.refCreateInfo().sharingMode,
         .initialLayout = pickResourcesBuild.refCreateInfo().initialLayout},
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        [](VkImageCreateInfo imageCreateInfo, VkImage image) noexcept
            -> mcs::vulkan::memory::create_image::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        });
    auto resolveResource = resolveResourcesBuild.build(device);

    // 每个飞行帧的暂存缓冲区（8 字节 = R32G32_UINT 像素）
    std::array<mcs::vulkan::memory::auto_map_buffer, MAX_FRAMES_IN_FLIGHT> pickingFrames;
    // 当前鼠标位置（已翻转 Y 轴，与 Vulkan 视口一致）
    struct pick_mouse
    {
        glm::ivec2 pos{0, 0};
        bool valid = false;
    };
    pick_mouse pickMouse;
    for (auto &pf : pickingFrames)
    {
        constexpr auto BUFFER_SIZE = 8;
        pf = mcs::vulkan::memory::auto_map_buffer(
            mcs::vulkan::memory::create_simple_buffer(
                device,
                {.size = BUFFER_SIZE,
                 .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            BUFFER_SIZE);
    }

    auto pickCtx =
        make_aggregate_ref<"pickCtx", "pickResourcesBuild", "pickResource",
                           "resolveResourcesBuild", "resolveResource", "frames", "mouse">(
            pickResourcesBuild, pickResource, resolveResourcesBuild, resolveResource,
            pickingFrames, pickMouse);
    auto globalCtx =
        make_aggregate_ref<"globalCtx", "device", "window", "surface", "swapchainBuild",
                           "swapchain", "camera", "frameContext", "commandPool",
                           "commandBuffers", "queue">(
            device, window, surface, swapchainBuild, swapchain, camera, frameContext,
            commandPool, commandBuffers, GRAPHICS_AND_PRESENT);

    auto mainCtx =
        make_aggregate_ref<"mainCtx", "pipeline", "pipelineLayout", "depthResourcesBuild",
                           "depthResource", "msaaResourcesBuild", "msaaResource">(
            graphicsPipeline, pipelineLayout, depthResourcesBuild, depthResource,
            msaaResourcesBuild, msaaResource);

    auto mainShaderCtx =
        make_aggregate_ref<"mainShaderCtx", "indirectDrawBatches", "uniformBuffers",
                           "descriptorSets">(batches, uniformBuffers, descriptorSets);

    float deltaTime = 0.0F; // 每帧由主循环更新
    auto inputCtx =
        make_aggregate_ref<"inputCtx", "input", "deltaTime">(input, deltaTime);

    // record_info
    auto recordCtx = make_aggregate<"recordCtx", "info">(record_info{});
    auto world = make_aggregate_ref<"world", "globalCtx", "mainCtx", "mainShaderCtx",
                                    "pickCtx", "inputCtx", "soaCtx", "recordCtx">(
        globalCtx, mainCtx, mainShaderCtx, pickCtx, inputCtx, soaCtx, recordCtx);
    using world_type = decltype(world);

    // diff: [test_dod2] start:  world之后 才能调用 数据API

    // init data

    //diff: [test_dod3] start
    auto autoSpinId = autoSpinStore.new_entity(
        mesh::gen_array_value<mesh::vertex_data, MAX_FRAMES_IN_FLIGHT>(
            [&] { return mesh::make_vertex_data(world, vertices, indices); }),
        mesh::gen_array_value<mesh::object_data_map, MAX_FRAMES_IN_FLIGHT>(
            [&] { return mesh::make_object_data_map(world, sizeof(object_data)); }),
        mesh::vertex_raw_data{}, 0, 0);
    auto interactiveId = interactiveStore.new_entity(
        mesh::gen_array_value<mesh::vertex_data, MAX_FRAMES_IN_FLIGHT>([&] {
            return mesh::make_vertex_data(
                world, vertices | std::views::transform([](const Vertex &vertex) {
                           auto newvertex = vertex;
                           newvertex.pos[2] = 0.5;
                           return newvertex;
                       }) | std::ranges::to<std::vector<Vertex>>(),
                indices);
        }),
        mesh::gen_array_value<mesh::object_data_map, MAX_FRAMES_IN_FLIGHT>(
            [&] { return mesh::make_object_data_map(world, sizeof(object_data)); }),
        mesh::vertex_raw_data{}, 3, 1,
        mesh::get_topLeftLocal(
            {.vertices = vertices | std::views::transform([](const Vertex &vertex) {
                             auto newvertex = vertex;
                             newvertex.pos[2] = 0.5;
                             return newvertex;
                         }) |
                         std::ranges::to<std::vector<Vertex>>(),
             .indices = indices}),
        model_state{});

    // 随机生成两个纹理索引和采样器索引
    auto [textureIndex1] = autoSpinStore.view_entity<"textureData">(0, autoSpinId);
    static_assert(std::is_reference_v<decltype(textureIndex1)>);

    //diff: [test_dod3] end

    // diff: [test_dod2] end

    class ShaderData
    {
      public:
        mesh::vertex_data *vertex_data;
        mesh::object_data_map *object_data_map;
        uint32_t texture_index;
        uint32_t sampler_index;
    };

    static constexpr auto views_matrix_update = [](world_type &world) {
        const auto &input = world.inputCtx.input;
        auto &camera = world.globalCtx.camera;
        using mcs::vulkan::event::Key;
        const float step = 0.1f; // 移动步长，可根据需要调整

        // 获取当前相机参数
        glm::vec3 eye = camera.refViewsMatrix().eye;
        glm::vec3 center = camera.refViewsMatrix().center;
        glm::vec3 up = camera.refViewsMatrix().up; // 通常为 (0,1,0)

        // 计算视线方向和右向
        glm::vec3 forward = glm::normalize(center - eye);
        glm::vec3 right = glm::normalize(glm::cross(forward, up));

        glm::vec3 delta(0.0f);

        if (input.isKeyPressedOrRepeat(Key::eW))
            delta += forward * step;
        if (input.isKeyPressedOrRepeat(Key::eS))
            delta -= forward * step;
        if (input.isKeyPressedOrRepeat(Key::eA))
            delta -= right * step;
        if (input.isKeyPressedOrRepeat(Key::eD))
            delta += right * step;
        if (input.isKeyPressedOrRepeat(Key::eQ))
            delta += glm::vec3(0.0f, step, 0.0f); // 世界Y轴向上
        if (input.isKeyPressedOrRepeat(Key::eE))
            delta += glm::vec3(0.0f, -step, 0.0f); // 世界Y轴向下

        if (glm::length2(delta) > 0.0f)
        {
            // 同时移动 eye 和 center，保持视线方向不变
            camera.refViewsMatrix().eye += delta;
            camera.refViewsMatrix().center += delta;
        }

        // ========== Ctrl+右键旋转视角 ==========
        {
            static mcs::vulkan::event::position2d_event lastRightPos{};
            static bool isRightDragging = false;
            using mcs::vulkan::event::MouseButtons;

            bool ctrlPressed = input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                               input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL);
            bool curRightPressed =
                input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_RIGHT);

            if (curRightPressed && ctrlPressed)
            {
                auto cur = input.cursorPos();
                if (!isRightDragging)
                {
                    lastRightPos = cur;
                    isRightDragging = true;
                }
                else
                {
                    float dx = static_cast<float>(cur.xpos - lastRightPos.xpos);
                    float dy = static_cast<float>(cur.ypos - lastRightPos.ypos);

                    if (dx != 0.0f || dy != 0.0f)
                    {
                        const float rotSensitivity = 0.005f; // 灵敏度可调

                        glm::vec3 eye = camera.refViewsMatrix().eye;
                        glm::vec3 center = camera.refViewsMatrix().center;
                        glm::vec3 up = camera.refViewsMatrix().up;

                        glm::vec3 forward = glm::normalize(center - eye);
                        glm::vec3 right = glm::normalize(glm::cross(forward, up));

                        // 偏航（绕世界Y轴）和俯仰（绕局部右轴）
                        float yaw = -dx * rotSensitivity;   // 鼠标左移时向左转
                        float pitch = -dy * rotSensitivity; // 鼠标上推时向上看

                        // 限制俯仰角，避免翻转
                        const float maxPitch = glm::radians(89.0f);
                        float currentPitch = asin(glm::clamp(forward.y, -1.0f, 1.0f));
                        float newPitch =
                            glm::clamp(currentPitch + pitch, -maxPitch, maxPitch);
                        pitch = newPitch - currentPitch;

                        // 构建旋转矩阵：先绕世界Y轴旋转 yaw，再绕局部右轴旋转 pitch
                        glm::mat4 rotYaw =
                            glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));
                        glm::mat4 rotPitch = glm::rotate(glm::mat4(1.0f), pitch, right);

                        // 应用旋转到 forward
                        glm::vec4 newForward =
                            rotYaw * rotPitch * glm::vec4(forward, 0.0f);
                        forward = glm::normalize(glm::vec3(newForward));

                        // 更新 center，保持 eye 不变
                        float distance = glm::length(center - eye);
                        camera.refViewsMatrix().center = eye + forward * distance;
                    }
                    lastRightPos = cur;
                }
            }
            else
            {
                isRightDragging = false;
            }
        };
    };

    static constexpr auto views_perspective_update = [](world_type &world) {
        const auto &input = world.inputCtx.input;
        auto &camera = world.globalCtx.camera;
        using mcs::vulkan::event::Key;

        if (input.isKeyPressedOrRepeat(Key::eR))
        {
            constexpr auto step = 00.1;
            update_perspective<perspective_member::eFOVY>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eF))
        {
            constexpr auto step = -00.1;
            update_perspective<perspective_member::eFOVY>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eT))
        {
            constexpr auto step = 00.1;
            update_perspective<perspective_member::eASPECT>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eG))
        {
            constexpr auto step = -00.1;
            update_perspective<perspective_member::eASPECT>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eY))
        {
            constexpr auto step = 00.1;
            update_perspective<perspective_member::eZ_NEAR>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eH))
        {
            constexpr auto step = -00.1;
            update_perspective<perspective_member::eZ_NEAR>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eU))
        {
            constexpr auto step = 00.1;
            update_perspective<perspective_member::eZ_FAR>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eJ))
        {
            constexpr auto step = -00.1;
            update_perspective<perspective_member::eZ_FAR>(camera, step);
        }
    };

    // diff: [test_model_matrix3] 小小调整
    static constexpr auto updateObjectData = [](world_type &world,
                                                uint32_t currentFrame) noexcept {
        auto &soaCtx = world.soaCtx;

        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;

        auto &spinElapsed = soaCtx.autoSpinStoreShareData.spinElapsed;

        // 获取 deltaTime（从 inputCtx 读取）
        const float dt = world.inputCtx.deltaTime;
        // 更新自旋动画的累积时间
        spinElapsed += dt;

        // Buffer is already mapped. You can access its memory.
        uint32_t idx = 0;
        for (auto [objectDataMap] : autoSpinStore.view<"objectDataMap">(currentFrame))
        {
            float phaseOffset = idx * glm::radians(137.5f);
            auto angle = spinElapsed * glm::radians(90.0f) + phaseOffset;
            auto data = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));
            ::memcpy(objectDataMap.objectDataBuffer.mapPtr(), &data, sizeof(data));
            ++idx;
        }
        for (const auto &[objectDataMap, modelState] :
             interactiveStore.view<"objectDataMap", "modelState">(currentFrame))
        {
            auto mat = modelState.model_matrix(); // 得到 glm::mat4
            ::memcpy(objectDataMap.objectDataBuffer.mapPtr(), &mat, sizeof(mat));
        }
    };
    // diff: [test_model_matrix2] end

    static constexpr auto updateVertexData = [](world_type &world,
                                                uint32_t currentFrame) noexcept {
        auto &soaCtx = world.soaCtx;

        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;

        for (auto [data, raw_data] :
             autoSpinStore.view<"vertexData", "vertexUpdateQueue">(currentFrame))
        {
            auto &count = raw_data.count;
            if (count == 0)
                continue;
            --count;

            data = mesh::make_vertex_data(world, raw_data.vertices, raw_data.indices);

            if (count == 0)
                raw_data.clear();
        }

        for (auto [data, raw_data, model, topLeftLocal] :
             interactiveStore.view<"vertexData", "vertexUpdateQueue", "modelState",
                                   "topLeftLocal">(currentFrame))
        {
            auto &count = raw_data.count;
            if (count == 0)
                continue;
            --count;

            // 更新GPU缓冲区（这个帧当前没有被GPU使用）;
            data = mesh::make_vertex_data(world, raw_data.vertices, raw_data.indices);
            topLeftLocal = mesh::get_topLeftLocal(raw_data);

            if (count == 0)
                raw_data.clear();
        }
    };

    static constexpr auto model_update = [](world_type &world) {
        auto &globalCtx = world.globalCtx;
        auto &inputCtx = world.inputCtx;

        auto &soaCtx = world.soaCtx;
        auto &interactiveStore = soaCtx.interactiveStore;
        auto [modelState, topLeftLocal] =
            *(interactiveStore.view<"modelState", "topLeftLocal">().begin());

        const auto &input = inputCtx.input;
        auto &camera = globalCtx.camera;
        auto &swapchain = globalCtx.swapchain;
        auto &frameContext = globalCtx.frameContext;

        auto &[lastPos, isMiddleButtonPressed, isLeftButtonPressed,
               rightButtonPressedLast, lastLeftPos, modelMatrix] = modelState;
        using mcs::vulkan::event::MouseButtons;
        using mcs::vulkan::event::Key;
        using mcs::vulkan::event::scroll_event;
        const auto cur = input.cursorPos();
        const auto windowSize = swapchain.refImageExtent();

        const bool curMiddlePressed =
            input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_MIDDLE);
        const bool curLeftPressed =
            input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_LEFT);
        const bool curRightPressed =
            input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_RIGHT);

        if (scroll_event{} != input.scroll())
        {
            std::println("input.scroll(): {}", input.scroll());
            // 获取修饰键状态
            bool ctrlPressed = input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                               input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL);
            bool altPressed = input.isKeyPressedOrRepeat(Key::eLEFT_ALT) ||
                              input.isKeyPressedOrRepeat(Key::eRIGHT_ALT);
            bool shiftPressed = input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                                input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);
            auto scroll = input.scroll();
            float delta = scroll.yoffset; // +1 向上滚（放大），-1 向下滚（缩小）

            if (delta != 0.0f)
            {
                // 缩放因子：每格 ±10%
                const float factor = (delta > 0) ? 1.1f : (1.0f / 1.1f);

                // 统计按下修饰键的数量
                int modCount =
                    (ctrlPressed ? 1 : 0) + (altPressed ? 1 : 0) + (shiftPressed ? 1 : 0);

                if (modCount == 1)
                {
                    // 仅一个修饰键按下：缩放对应的轴
                    if (ctrlPressed)
                        modelMatrix.scale.x *= factor;
                    else if (altPressed)
                        modelMatrix.scale.y *= factor;
                    else if (shiftPressed)
                        modelMatrix.scale.z *= factor;
                }
                else
                {
                    // 无修饰键或多个修饰键：均匀缩放所有轴
                    modelMatrix.scale *= factor;
                }

                // 可选：限制缩放范围，避免过小或过大
                // modelMatrix_.scale = glm::clamp(modelMatrix_.scale, 0.01f, 100.0f);
            }
        }

        // ========== 右键单击复位物体 ==========
        if (curRightPressed && !rightButtonPressedLast)
        {
            // 按下瞬间：平移归零，旋转归单位四元数
            modelMatrix.translation = glm::vec3(0.0f);
            modelMatrix.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            // scale 保持 (1,1,1) 不变
            modelMatrix.scale = {1, 1, 1};
        }
        rightButtonPressedLast = curRightPressed; // 更新状态供下一帧使用

        // ========== 左键旋转处理（支持修饰键分离轴） ==========
        if (curLeftPressed)
        {
            if (!isLeftButtonPressed)
            {
                lastLeftPos = cur;
                isLeftButtonPressed = true;
            }
            else
            {
                float dx = static_cast<float>(cur.xpos - lastLeftPos.xpos);
                float dy = static_cast<float>(cur.ypos - lastLeftPos.ypos);

                if (dx != 0.0f || dy != 0.0f)
                {
                    glm::mat4 view = camera.viewsMatrix();
                    glm::mat4 viewInv = glm::inverse(view);
                    glm::vec3 worldRight = viewInv[0]; // 屏幕右方向（水平轴）
                    glm::vec3 worldUp = viewInv[1];    // 屏幕上方向（垂直轴）
                    glm::vec3 worldForward = -glm::vec3(viewInv[2]); // 视线方向

                    const float rotSensitivity = 0.01f;

                    bool ctrlPressed = input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                                       input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL);
                    bool altPressed = input.isKeyPressedOrRepeat(Key::eLEFT_ALT) ||
                                      input.isKeyPressedOrRepeat(Key::eRIGHT_ALT);
                    bool shiftPressed = input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                                        input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);

                    glm::quat deltaRot(1.0f, 0.0f, 0.0f, 0.0f);

                    if (ctrlPressed)
                    {
                        // Ctrl + 水平移动：绕屏幕垂直轴（左右旋转）
                        deltaRot = glm::angleAxis(dx * rotSensitivity, worldUp);
                    }
                    else if (altPressed)
                    {
                        // Alt + 垂直移动：绕屏幕水平轴（上下翻转）
                        deltaRot = glm::angleAxis(dy * rotSensitivity, worldRight);
                    }
                    else if (shiftPressed)
                    {
                        // Shift + 水平移动：绕视线方向（滚动）
                        deltaRot = glm::angleAxis(dx * rotSensitivity, worldForward);
                    }
                    else
                    {
                        // 无修饰键：水平绕 worldUp，垂直绕 worldRight
                        glm::quat rotY = glm::angleAxis(dx * rotSensitivity, worldUp);
                        glm::quat rotX = glm::angleAxis(dy * rotSensitivity, worldRight);
                        deltaRot = rotY * rotX;
                    }

                    if (deltaRot != glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
                    {
                        modelMatrix.rotation = deltaRot * modelMatrix.rotation;
                    }
                }
                lastLeftPos = cur;
            }
        }
        else
        {
            isLeftButtonPressed = false;
        }
        if (curMiddlePressed)
        {
            // 检测 Shift 键（独立于左键修饰键）
            bool shiftPressed = input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                                input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);

            glm::mat4 view = camera.viewsMatrix();
            glm::mat4 proj = camera.perspectiveMatrix();
            glm::mat4 viewInv = glm::inverse(view);
            glm::vec4 viewport(0.0f, 0.0f, static_cast<float>(windowSize.width),
                               static_cast<float>(windowSize.height));

            glm::vec3 currentScale = modelMatrix.scale;
            glm::quat currentRot = modelMatrix.rotation;
            glm::vec3 localTopLeft = topLeftLocal;
            glm::vec3 scaledTopLeft = currentScale * localTopLeft;
            glm::vec3 rotatedOffset =
                currentRot * scaledTopLeft; // 从平移位置指向左上角的偏移量
            glm::vec3 currentTopLeftWorld = modelMatrix.translation + rotatedOffset;

            glm::vec3 winNear(static_cast<float>(cur.xpos),
                              static_cast<float>(windowSize.height - cur.ypos), 0.0f);
            glm::vec3 winFar(static_cast<float>(cur.xpos),
                             static_cast<float>(windowSize.height - cur.ypos), 1.0f);
            glm::vec3 nearWorld = glm::unProject(winNear, view, proj, viewport);
            glm::vec3 farWorld = glm::unProject(winFar, view, proj, viewport);
            glm::vec3 dirWorld = glm::normalize(farWorld - nearWorld);

            const float eps = 1e-6f;
            const float maxT = 1000.0f;   // 最大射线距离，根据场景调整
            const float maxDelta = 10.0f; // 单次最大平移变化量（可选）

            glm::vec3 newTranslation = modelMatrix.translation; // 默认不变

            if (shiftPressed)
            {
                // ========== 旧模式：世界 XY 平面平移（固定世界 Z） ==========
                float targetZ = currentTopLeftWorld.z; // 保持当前左上角的世界 Z
                if (std::fabs(dirWorld.z) > eps)
                {
                    float t = (targetZ - nearWorld.z) / dirWorld.z;
                    if (t >= 0.0f && t < maxT)
                    {
                        glm::vec3 worldPoint = nearWorld + t * dirWorld;
                        newTranslation = worldPoint - rotatedOffset;
                    }
                }
            }
            else
            {
                // ========== 新模式：屏幕空间平移（固定相机空间 Z） ==========
                // 将当前左上角转换到相机空间，获取其深度
                glm::vec4 currentTopLeftView =
                    view * glm::vec4(currentTopLeftWorld, 1.0f);
                float depthView = currentTopLeftView.z; // 相机空间 Z

                // 将射线端点转换到相机空间
                glm::vec4 nearView = view * glm::vec4(nearWorld, 1.0f);
                glm::vec4 farView = view * glm::vec4(farWorld, 1.0f);
                glm::vec3 dirView = glm::vec3(farView - nearView); // 相机空间射线方向

                if (std::fabs(dirView.z) > eps)
                {
                    float t = (depthView - nearView.z) / dirView.z;
                    if (t >= 0.0f && t <= 1.0f) // 交点在射线范围内
                    {
                        glm::vec3 pointView = glm::vec3(nearView) + t * dirView;
                        glm::vec4 pointWorld = viewInv * glm::vec4(pointView, 1.0f);
                        glm::vec3 worldPoint = glm::vec3(pointWorld) / pointWorld.w;
                        newTranslation = worldPoint - rotatedOffset;
                    }
                }
            }

            // 可选：限制平移变化量，防止跳变
            glm::vec3 delta = newTranslation - modelMatrix.translation;
            if (glm::length(delta) < maxDelta)
            {
                modelMatrix.translation = newTranslation;
            }
            // 若超过限制，可以忽略本次更新，或按比例缩放

            if (!isMiddleButtonPressed)
                isMiddleButtonPressed = true;
            lastPos = cur;
        }
        else
        {
            isMiddleButtonPressed = false;
        }
    };

    // diff: [test_indirectdraw] start prepareBatch：动态分配合批资源并填充数据 // NOLINTNEXTLINE
    static constexpr auto prepareBatch = [](world_type &world, uint32_t currentFrame) {
        auto &globalCtx = world.globalCtx;
        auto &mainShaderCtx = world.mainShaderCtx;

        auto &device = globalCtx.device;

        auto &descriptorSets = mainShaderCtx.descriptorSets;
        auto &batches = mainShaderCtx.indirectDrawBatches;

        auto &descriptorSet = descriptorSets[currentFrame];
        auto &batch = batches[currentFrame];

        auto &soaCtx = world.soaCtx;
        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;

        auto start = std::chrono::high_resolution_clock::now();

        auto autoSpinObjs =
            autoSpinStore
                .view<"vertexData", "objectDataMap", "textureData", "samplerData">(
                    currentFrame) |
            std::views::transform([](auto &&tuple) {
                auto &[mesh, data, tex, samp] = tuple;
                return ShaderData{&mesh, &data, tex, samp};
            }) |
            std::ranges::to<std::vector<ShaderData>>();
        auto interactiveObjs =
            interactiveStore
                .view<"vertexData", "objectDataMap", "textureData", "samplerData">(
                    currentFrame) |
            std::views::transform([](auto &&tuple) {
                auto &[mesh, data, tex, samp] = tuple;
                return ShaderData{&mesh, &data, tex, samp};
            }) |
            std::ranges::to<std::vector<ShaderData>>();

        std::vector<ShaderData> objects;
        objects.reserve(autoSpinObjs.size() + interactiveObjs.size());
        objects.append_range(std::move(autoSpinObjs));
        objects.append_range(std::move(interactiveObjs));
        auto objectCount = static_cast<uint32_t>(objects.size());

        uint32_t totalIndexCount = 0;
        for (auto &o : objects)
            totalIndexCount += o.vertex_data->index_sequence.size();

        // 1. 保证合并索引缓冲足够大
        VkDeviceSize needIdx = totalIndexCount * sizeof(uint32_t);
        if (batch.mergedIndexCapacity < needIdx)
        {
            batch.mergedIndexBuffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = needIdx,
                     .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                needIdx);
            batch.mergedIndexCapacity = needIdx;
        }

        // 2. 保证 ObjectInfo SSBO 足够大
        VkDeviceSize needObj = objectCount * sizeof(ObjectInfo);
        if (batch.objectInfoCapacity < needObj)
        {

            batch.objectInfoBuffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = needObj,
                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                needObj);
            batch.objectInfoCapacity = needObj;

            // 更新描述符集中的 binding 3 指向新缓冲
            VkDescriptorBufferInfo bufInfo{
                .buffer = batch.objectInfoBuffer.buffer(),
                .offset = 0,
                .range = VK_WHOLE_SIZE,
            };
            VkWriteDescriptorSet write{
                .sType = sType<VkWriteDescriptorSet>(),
                .dstSet = descriptorSet,
                .dstBinding = 3,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &bufInfo,
            };
            device.updateDescriptorSets(1, &write, 0, nullptr);
        }

        // 3. 保证间接命令缓冲足够大
        VkDeviceSize needCmd = objectCount * sizeof(VkDrawIndexedIndirectCommand);
        if (batch.indirectCmdCapacity < needCmd)
        {
            batch.indirectDrawBuffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = needCmd,
                     .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                needCmd);
            batch.indirectCmdCapacity = needCmd;
        }

        // 4. 填充数据
        uint32_t idxOff = 0;
        std::vector<ObjectInfo> infos(objectCount);
        std::vector<VkDrawIndexedIndirectCommand> cmds(objectCount);
        for (uint32_t i = 0; i < objectCount; ++i)
        {
            auto &shaderData = objects[i];
            auto &vertex_data = *shaderData.vertex_data;
            auto &object_data = *shaderData.object_data_map;

            auto &index_sequence = vertex_data.index_sequence;
            using index_value_type = decltype(auto(index_sequence[0]));
            uint32_t indexCount = index_sequence.size();
            //diff: [test_dod2] 要特别小心void*
            // 拷贝索引数据到合并缓冲
            // 错误 ❌: mapPtr() 返回 void*
            // memcpy(batch.mergedIndexBuffer.mapPtr() + idxOff, index_sequence.data(),
            //        indexCount * sizeof(index_value_type));
            memcpy(static_cast<uint32_t *>(batch.mergedIndexBuffer.mapPtr()) + idxOff,
                   index_sequence.data(), indexCount * sizeof(uint32_t));

            cmds[i] = {
                .indexCount = indexCount,
                .instanceCount = 1,
                .firstIndex = idxOff,
                .vertexOffset = 0,
                .firstInstance = i, // 关键：每个物体不同的 firstInstance
            };
            infos[i] = {
                .vertexBufferAddress = vertex_data.vertexBufferAddress,
                .objectDataAddress = object_data.objectDataAddress,
                .textureIndex = shaderData.texture_index,
                .samplerIndex = shaderData.sampler_index,
                .objectId =
                    i, //diff: [test_indirectdraw_no_pick]. 直接用数组索引. 可以从object中拿到自定义
            };
            idxOff += indexCount;
        }
        memcpy(batch.objectInfoBuffer.mapPtr(), infos.data(), needObj);
        memcpy(batch.indirectDrawBuffer.mapPtr(), cmds.data(), needCmd);
        batch.drawCount = objectCount;

        // 3. 记录结束时间点
        auto end = std::chrono::high_resolution_clock::now();

        // 4. 计算时间差，并转换为毫秒
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // NOTE: 函数执行耗时: 2 毫秒. [FPS]  77.1 (avg frame: 12.963 ms) 占用了1/6
        // 5. 打印耗时
        std::cout << "函数执行耗时: " << duration.count() << " 毫秒" << std::endl;
    };

    // diff: [test_indirectdraw] end
    // diff: [test_dod5] start
    static constexpr auto mainPipeline = make_aggregate<
        "mainPipeline", "transitionImageLayout", "beginRendering", "setPipelineState",
        "draw", "endRendering">(
        std::constant_wrapper<[](world_type &world) {
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &recordCtx = world.recordCtx;
            auto &pickCtx = world.pickCtx;

            auto &swapchain = globalCtx.swapchain;
            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            // images
            auto pickingImage = pickCtx.pickResource.image();
            auto resolveImage = pickCtx.resolveResource.image();
            auto &depthResource = mainCtx.depthResource;
            auto &msaaResource = mainCtx.msaaResource;

            VkImage image = swapchain.image(imageIndex);
            VkImage depthImage = depthResource.image();

            VkImage msaaImage = msaaResource.image();

            // Before starting rendering,
            // transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
            my_render::transition_image_layout(
                commandBuffer,
                my_render::image_info{
                    .image = image,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
                my_render::image_info{
                    .image = msaaImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
                my_render::image_info{
                    .image = depthImage,
                    .aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT}},
                //diff: [test_indirectdraw_no_pick] start  转换拾取 MSAA 图像布局
                my_render::image_info{
                    .image = pickingImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
                my_render::image_info{
                    .image = resolveImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}}
                //diff: [test_indirectdraw_no_pick] end
            );
        }>{},
        std::constant_wrapper<[](world_type &world) {
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &recordCtx = world.recordCtx;
            auto &pickCtx = world.pickCtx;

            auto &swapchain = globalCtx.swapchain;
            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            // imageViews
            auto &depthResource = mainCtx.depthResource;
            auto &msaaResource = mainCtx.msaaResource;

            VkImageView msaaImageView = msaaResource.imageView();
            VkImageView imageView = swapchain.imageView(imageIndex);
            auto pickingImageView = pickCtx.pickResource.imageView();
            auto pickingResolveImageView = pickCtx.resolveResource.imageView();
            VkImageView depthImageView = depthResource.imageView();
            auto imageExtent = swapchain.imageExtent();

            VkRenderingAttachmentInfo colorAttachment = {
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = msaaImageView,
                .imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
                .resolveImageView =
                    imageView, //NOTE: 将msaaImageView的输出替换到imageView，这就是关键
                .resolveImageLayout =
                    VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}}};
            //diff: [test_indirectdraw_no_pick] start
            // 拾取附件（location=1）
            VkRenderingAttachmentInfo pickAttachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = pickingImageView,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode =
                    VK_RESOLVE_MODE_SAMPLE_ZERO_BIT, // 整数格式必须用 SAMPLE_ZERO
                .resolveImageView = pickingResolveImageView,
                .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {
                    .color = {.uint32 = {0xFFFFFFFF, 0xFFFFFFFF, 0, 0}}}}; // NOLINT
            std::array attachments = {colorAttachment, pickAttachment};
            //                        ^ 索引 0          ^ 索引 1
            //diff: [test_indirectdraw_no_pick] end

            VkRenderingAttachmentInfo depthAttachment = {
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = depthImageView,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.depthStencil = {.depth = 1.0F}}};

            commandBuffer.beginRendering(
                {.sType = sType<VkRenderingInfo>(),
                 .renderArea = {.offset = {.x = 0, .y = 0}, .extent = imageExtent},
                 .layerCount = 1,
                 //diff: [test_indirectdraw_no_pick] start
                 .colorAttachmentCount = static_cast<uint32_t>(attachments.size()),
                 .pColorAttachments = attachments.data(),
                 //diff: [test_indirectdraw_no_pick] end
                 .pDepthAttachment = &depthAttachment});
        }>{},
        std::constant_wrapper<[](world_type &world) {
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &recordCtx = world.recordCtx;
            auto &mainShaderCtx = world.mainShaderCtx;

            auto &swapchain = globalCtx.swapchain;
            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto &graphicsPipeline = mainCtx.pipeline;
            auto imageExtent = swapchain.imageExtent();
            auto &pipelineLayout = mainCtx.pipelineLayout;

            auto &descriptorSets = mainShaderCtx.descriptorSets;
            auto descriptorSet = descriptorSets[currentFrame];

            commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       *graphicsPipeline);
            commandBuffer.setViewport(
                0, std::array<VkViewport, 1>{
                       VkViewport{.x = 0.0F,
                                  .y = 0.0F,
                                  .width = static_cast<float>(imageExtent.width),
                                  .height = static_cast<float>(imageExtent.height),
                                  .minDepth = 0.0F,
                                  .maxDepth = 1.0F}});

            commandBuffer.setScissor(
                0, std::array<VkRect2D, 1>{
                       VkRect2D{.offset = {.x = 0, .y = 0}, .extent = imageExtent}});
            commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             *pipelineLayout, 0, 1, &(descriptorSet), 0,
                                             nullptr);
        }>{},
        std::constant_wrapper<[](world_type &world) {
            auto &mainShaderCtx = world.mainShaderCtx;
            auto &recordCtx = world.recordCtx;
            auto &globalCtx = world.globalCtx;

            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto &batches = mainShaderCtx.indirectDrawBatches;

            // diff: [test_indirectdraw] start 使用合并索引缓冲和间接绘制
            auto &batch = batches[currentFrame];
            if (batch.drawCount > 0)
            {
                commandBuffer.bindIndexBuffer(batch.mergedIndexBuffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.drawIndexedIndirect(batch.indirectDrawBuffer.buffer(), 0,
                                                  batch.drawCount,
                                                  sizeof(VkDrawIndexedIndirectCommand));
            }
            // diff: [test_indirectdraw] start
        }>{},
        std::constant_wrapper<[](world_type &world) {
            auto &mainShaderCtx = world.mainShaderCtx;
            auto &recordCtx = world.recordCtx;
            auto &globalCtx = world.globalCtx;
            auto &pickCtx = world.pickCtx;

            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto resolveImage = pickCtx.resolveResource.image();

            auto &pickingFrames = pickCtx.frames;
            auto &pickMouse = pickCtx.mouse;

            auto &mouseValid = pickMouse.valid;
            auto &mousePos = pickMouse.pos;

            commandBuffer.endRendering();
            if (mouseValid)
            {
                //diff: [test_indirectdraw_no_pick] start
                // 转换 resolve 目标到 TRANSFER_SRC
                my_render::transition_image_layout(
                    commandBuffer,
                    my_render::image_info{
                        .image = resolveImage,
                        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .src = {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                        .dst = {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_ACCESS_2_TRANSFER_READ_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT}});
                //NOTE: 复制到 pickingFrames，就可以读取了，拿到着色器的输出
                commandBuffer.copyImageToBuffer(
                    resolveImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    pickingFrames[currentFrame].buffer(),
                    std::array<VkBufferImageCopy, 1>{VkBufferImageCopy{
                        .bufferOffset = 0,
                        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .imageOffset = {mousePos.x, mousePos.y, 0},
                        .imageExtent = {1, 1, 1}}});

                //diff: [test_indirectdraw_no_pick] end
            }
        }>{});
    // NOLINTNEXTLINE
    static constexpr auto recordCommandBuffer = [](world_type &world) {
        auto &globalCtx = world.globalCtx;
        auto &recordCtx = world.recordCtx;

        auto &swapchain = globalCtx.swapchain;
        auto &commandBuffers = globalCtx.commandBuffers;

        auto [currentFrame, imageIndex] = recordCtx.info;
        const auto &commandBuffer = commandBuffers[currentFrame];
        VkImage image = swapchain.image(imageIndex);

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        mainPipeline.transitionImageLayout(world);
        mainPipeline.beginRendering(world);
        mainPipeline.setPipelineState(world);
        mainPipeline.draw(world);
        mainPipeline.endRendering(world);

        // After rendering, transition the swapchain image to PRESENT_SRC
        my_render::transition_image_layout(
            commandBuffer,
            my_render::image_info{
                .image = image,
                .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                .src = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                .dst = {.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        .access_mask = VK_ACCESS_2_NONE,
                        .stage_mask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT}});
        commandBuffer.end();
    };
    // diff: [test_dod5] end

    // NOLINTNEXTLINE
    static constexpr auto recreateSwapChain = [](world_type &world) constexpr {
        auto &globalCtx = world.globalCtx;
        auto &pickCtx = world.pickCtx;
        auto &mainCtx = world.mainCtx;

        auto &device = globalCtx.device;
        auto &surface = globalCtx.surface;
        auto &swapchainBuild = globalCtx.swapchainBuild;
        auto &swapchain = globalCtx.swapchain;
        auto &camera = globalCtx.camera;
        auto &frameContext = globalCtx.frameContext;

        auto &msaaResourcesBuild = mainCtx.msaaResourcesBuild;
        auto &depthResource = mainCtx.depthResource;
        auto &depthResourcesBuild = mainCtx.depthResourcesBuild;
        auto &msaaResource = mainCtx.msaaResource;

        auto &pickResourcesBuild = pickCtx.pickResourcesBuild;
        auto &pickResource = pickCtx.pickResource;
        auto &resolveResourcesBuild = pickCtx.resolveResourcesBuild;
        auto &resolveResource = pickCtx.resolveResource;
        auto &pickMouse = pickCtx.mouse;

        surface.waitGoodFramebufferSize();
        device.waitIdle();

        swapchain = swapchainBuild.rebuild();
        auto newExtent = swapchain.refImageExtent();
        VkExtent3D imageExtent = {
            .width = newExtent.width, .height = newExtent.height, .depth = 1};

        msaaResource = msaaResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        depthResource =
            depthResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        camera.refPerspectiveMatrix().aspect =
            newExtent.width / static_cast<float>(newExtent.height);

        pickResource = pickResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        resolveResource =
            resolveResourcesBuild.setCreateInfoExtent(imageExtent).build(device);

        pickMouse.valid = false;

        frameContext.rebuild(swapchain.imagesSize());
    };
    // diff: [test_dod5] start
    static auto s_lastFrameTime = std::chrono::high_resolution_clock::now();
    static constexpr auto limit_frame_rate = [](auto & /*world*/,
                                                float targetFPS = 60.0f) {
        const float targetFrameTime = 1.0f / targetFPS;
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - s_lastFrameTime).count();

        if (elapsed < targetFrameTime)
        {
            auto sleepTime = std::chrono::duration<float>(targetFrameTime - elapsed);
            std::this_thread::sleep_for(
                std::chrono::duration_cast<std::chrono::microseconds>(sleepTime));
            s_lastFrameTime = std::chrono::high_resolution_clock::now();
        }
        else
        {
            s_lastFrameTime = now;
        }
    };
    static constexpr auto update_delta_time = [](auto &world) {
        auto now = std::chrono::high_resolution_clock::now();
        float rawDelta = std::chrono::duration<float>(now - s_lastFrameTime).count();
        world.inputCtx.deltaTime = glm::clamp(rawDelta, 0.001f, 0.1f);
        s_lastFrameTime = now;
    };
    static constexpr auto FrameRateMonitor = [](auto & /*world*/) {
        static auto lastPrintTime = std::chrono::high_resolution_clock::now();
        static int frameCounter = 0;

        ++frameCounter;
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastPrintTime).count();

        if (elapsed >= 1.0f)
        {
            float fps = frameCounter / elapsed;
            float avgFrameTimeMs = elapsed * 1000.0f / frameCounter;
            std::println("[FPS] {:5.1f} (avg frame: {:.3f} ms)", fps, avgFrameTimeMs);
            frameCounter = 0;
            lastPrintTime = now;
        }
    };
    // diff: [test_dod5] end
    static constexpr auto InputController = [](auto &world) {
        auto &inputCtx = world.inputCtx;
        auto &pickCtx = world.pickCtx;
        auto &globalCtx = world.globalCtx;
        auto &soaCtx = world.soaCtx;

        auto &input = inputCtx.input;
        auto &pickMouse = pickCtx.mouse;
        auto &swapchain = globalCtx.swapchain;

        surface::pollEvents();

        auto cur = input.cursorPos();
        auto ext = swapchain.refImageExtent();
        if (cur.xpos >= 0 && cur.xpos < static_cast<int>(ext.width) && cur.ypos >= 0 &&
            cur.ypos < static_cast<int>(ext.height))
        {
            pickMouse.pos = {cur.xpos, cur.ypos};
            pickMouse.valid = true;
        }
        else
        {
            pickMouse.valid = false;
        }

        // diff: [test_dod5] start
        // 每 2 秒添加 10 个小实体，使用网格锚点保证不重叠，并按波次从内向外扩散
        static auto lastUpdate = std::chrono::steady_clock::now();
        // NOTE: 最终实体数量会达到 10000。每个实体 6 个内存分配 → 60000 次 vkAllocateMemory，远超过 4096 的限制。
        /*
[ ERROR ] [create_debugger.hpp:24:static VkBool32 mcs::vulkan::tool::create_debugger::defaultDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*, void*)]: 1318213324 Validation Layer: Error: VUID-vkAllocateMemory-maxMemoryAllocationCount-04101: vkAllocateMemory(): The number of currently valid memory objects (4096) is not less than maxMemoryAllocationCount (4096).
The Vulkan spec states: There must be less than VkPhysicalDeviceLimits::maxMemoryAllocationCount device memory allocations currently allocated on the device (https://docs.vulkan.org/spec/latest/chapters/memory.html#VUID-vkAllocateMemory-maxMemoryAllocationCount-04101)
*/
        //diff: [test_dod7] vma是很重要的，至少目前是这样。 子分配还是很有必要的
        constexpr auto add_count_per_times = 100;
        constexpr int total_batches = 10;
        constexpr int total_entities = add_count_per_times * total_batches;
        static int batch_index = 0; // 当前批次索引 (0~4)

        // 预生成所有锚点（在 main 外部，或作为静态变量只初始化一次）
        static std::vector<glm::vec2> anchorPositions = []() {
            // 网格大小：根据总实体数和预期区域估算
            // 假设每个实体边长 0.3，安全间距取 0.35
            const float spacing = 0.35f;
            const int grid_dim = static_cast<int>(std::ceil(std::sqrt(total_entities)));
            std::vector<glm::vec2> anchors;
            anchors.reserve(grid_dim * grid_dim);
            // 生成中心对称的网格，范围从 -(grid_dim-1)*spacing/2 到 +(grid_dim-1)*spacing/2
            float start = -(grid_dim - 1) * spacing / 2.0f;
            for (int y = 0; y < grid_dim; ++y)
            {
                for (int x = 0; x < grid_dim; ++x)
                {
                    anchors.emplace_back(start + x * spacing, start + y * spacing);
                }
            }
            // 随机打乱锚点顺序，使各批次内部的放置看起来随机
            std::mt19937 rng(std::random_device{}());
            std::shuffle(anchors.begin(), anchors.end(), rng);
            return anchors;
        }();

        // 静态变量，记录下一个可用锚点的索引
        static size_t next_anchor_idx = 0;

        auto now = std::chrono::steady_clock::now();
        if (now - lastUpdate > std::chrono::seconds(2))
        {
            auto &autoSpinStore = soaCtx.autoSpinStore;

            if (batch_index < total_batches) // 还剩批次
            {
                ++batch_index; // 批次计数（注意这里改为递增，方便理解，原 count 递减）
                // 确保 SoA 容器有足够空间
                if (add_count_per_times > autoSpinStore.free_size())
                    autoSpinStore.expansion_size(add_count_per_times -
                                                 autoSpinStore.free_size());

                std::mt19937 rng(std::random_device{}());
                std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
                // 锚点微调范围（远小于网格间距，确保不重叠s）
                std::uniform_real_distribution<float> jitter(-0.03f, 0.03f);
                // 实体大小随机，但需保证最大尺寸不超过网格间距
                std::uniform_real_distribution<float> scaleDist(0.08f, 0.14f);

                for (int i = 0; i < add_count_per_times; ++i)
                {
                    if (next_anchor_idx >= anchorPositions.size())
                    {
                        // 理论上不会发生，因为我们预留了足够锚点
                        break;
                    }
                    glm::vec2 anchor = anchorPositions[next_anchor_idx++];
                    float scale = scaleDist(rng);
                    // 加上随机微调，增加自然感
                    float posX = anchor.x + jitter(rng);
                    float posY = anchor.y + jitter(rng);

                    std::array<Vertex, 4> vertices = {
                        {{{-0.5f * scale + posX, -0.5f * scale + posY, 0.0f},
                          {colorDist(rng), colorDist(rng), colorDist(rng)},
                          {0.0f, 0.0f}},
                         {{0.5f * scale + posX, -0.5f * scale + posY, 0.0f},
                          {colorDist(rng), colorDist(rng), colorDist(rng)},
                          {1.0f, 0.0f}},
                         {{0.5f * scale + posX, 0.5f * scale + posY, 0.0f},
                          {colorDist(rng), colorDist(rng), colorDist(rng)},
                          {1.0f, 1.0f}},
                         {{-0.5f * scale + posX, 0.5f * scale + posY, 0.0f},
                          {colorDist(rng), colorDist(rng), colorDist(rng)},
                          {0.0f, 1.0f}}}};

                    std::array<uint32_t, 6> indices = {0, 1, 2, 0, 2, 3};

                    autoSpinStore.new_entity(
                        mesh::gen_array_value<mesh::vertex_data, MAX_FRAMES_IN_FLIGHT>(
                            [&] {
                                return mesh::make_vertex_data(world, vertices, indices);
                            }),
                        mesh::gen_array_value<mesh::object_data_map,
                                              MAX_FRAMES_IN_FLIGHT>([&] {
                            return mesh::make_object_data_map(world, sizeof(object_data));
                        }),
                        mesh::vertex_raw_data{}, 0, 0);
                }

                lastUpdate = now; // 重置定时器
            }
        }
        // diff: [test_dod5] end
    };

    //NOTE: 下面的内联做的更好，编译期的数据更利于优化
    static constexpr auto vulaknDataTransformer =
        make_aggregate<"vulaknDataTransformer", "ProcessingWorldInput_0",
                       "AfterWaitForFences_0", "BeforeRecordCommandBuffer_0">(
            std::constant_wrapper<[](world_type &world) {
                // limit_frame_rate(world);  // ① 帧率限制（可选）
                update_delta_time(world); // ② 必须在帧率限制之后，保证 delta 正确
                InputController(world);
                views_matrix_update(world);
                views_perspective_update(world);
                model_update(world);
                FrameRateMonitor(world);            // ③ 纯监控
                world.inputCtx.input.scroll() = {}; // 滚轮清零放在最后
            }>{},
            std::constant_wrapper<[](world_type &world) {
                auto &globalCtx = world.globalCtx;
                auto &pickCtx = world.pickCtx;

                auto &frameContext = globalCtx.frameContext;

                auto &pickMouse = pickCtx.mouse;
                auto &pickingFrames = pickCtx.frames;

                auto &currentFrame = frameContext.currentFrame;

                // 等待栅栏后，正式获取图像前
                if (currentFrame > 0 && pickMouse.valid)
                {
                    uint32_t readIdx =
                        (currentFrame - 1 + MAX_FRAMES_IN_FLIGHT) % MAX_FRAMES_IN_FLIGHT;
                    auto *data = static_cast<uint32_t *>(pickingFrames[readIdx].mapPtr());
                    uint32_t objId = data[0];
                    uint32_t primId = data[1];
                    if (objId != 0xFFFFFFFF)
                    {
                        std::cout << "Hover: Object=" << objId << ", Triangle=" << primId
                                  << "\n";
                    }
                }
            }>{},
            std::constant_wrapper<[](world_type &world) {
                auto &globalCtx = world.globalCtx;
                auto &mainShaderCtx = world.mainShaderCtx;

                auto &frameContext = globalCtx.frameContext;
                auto &camera = globalCtx.camera;

                auto &currentFrame = frameContext.currentFrame;

                auto &uniformBuffers = mainShaderCtx.uniformBuffers;

                //diff: [test_dod3] start
                updateObjectData(world, currentFrame);
                updateVertexData(world, currentFrame);
                prepareBatch(world, currentFrame);

                auto uploadUniformBuffers = [&]() {
                    auto *uniformBuffersPtr = uniformBuffers[currentFrame].mapPtr();

                    UniformBufferObject ubo{};
                    ubo.view = camera.viewsMatrix();

                    ubo.proj = camera.perspectiveMatrix();
                    ubo.proj[1][1] *= -1;

                    // 复制数据到Uniform Buffer
                    memcpy(uniformBuffersPtr, &ubo, sizeof(ubo));
                };
                uploadUniformBuffers();
                //diff: [test_dod3] end
            }>{});

    // NOLINTNEXTLINE
    static constexpr auto drawFrame = [](world_type &world) constexpr {
        auto &globalCtx = world.globalCtx;
        auto &pickCtx = world.pickCtx;
        auto &recordCtx = world.recordCtx;

        auto &device = globalCtx.device;

        auto &swapchain = globalCtx.swapchain;
        auto &frameContext = globalCtx.frameContext;
        auto &window = globalCtx.window;
        auto &commandBuffers = globalCtx.commandBuffers;
        const auto &GRAPHICS_AND_PRESENT = globalCtx.queue;

        auto &pickingFrames = pickCtx.frames;
        auto &pickMouse = pickCtx.mouse;

        auto &inFlightFences = frameContext.inFlightFences;
        auto &currentFrame = frameContext.currentFrame;
        auto &presentCompleteSemaphore = frameContext.presentCompleteSemaphore;
        auto &semaphoreIndex = frameContext.semaphoreIndex;
        auto &renderFinishedSemaphore = frameContext.renderFinishedSemaphore;

        while (device.waitForFences(1, inFlightFences[currentFrame], VK_TRUE,
                                    UINT64_MAX) == VK_TIMEOUT)
            ;

        invoke_aggregate_ranges<"AfterWaitForFences_", 0, 0>(vulaknDataTransformer,
                                                             world);

        auto [result, imageIndex] = swapchain.acquireNextImage(
            UINT64_MAX, presentCompleteSemaphore[semaphoreIndex], nullptr);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            recreateSwapChain(world);
            return;
        }
        if (result != VK_SUCCESS)
            throw std::runtime_error("failed to acquire swap chain image!");
        device.resetFences(1, inFlightFences[currentFrame]);

        invoke_aggregate_ranges<"BeforeRecordCommandBuffer_", 0, 0>(vulaknDataTransformer,
                                                                    world);

        const auto &commandBuffer = commandBuffers[currentFrame];
        commandBuffer.reset({});
        recordCtx.info = {.current_frame = currentFrame, .image_index = imageIndex};

        recordCommandBuffer(world);

        // NOLINTNEXTLINE
        VkPipelineStageFlags waitDestinationStageMask[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        GRAPHICS_AND_PRESENT.submit(
            1,
            {.sType = sType<VkSubmitInfo>(),
             .waitSemaphoreCount = 1,
             .pWaitSemaphores = &presentCompleteSemaphore[semaphoreIndex],
             .pWaitDstStageMask = waitDestinationStageMask,
             .commandBufferCount = 1,
             .pCommandBuffers = &*commandBuffer,
             .signalSemaphoreCount = 1,
             .pSignalSemaphores = &renderFinishedSemaphore[imageIndex]},
            inFlightFences[currentFrame]);

        result = GRAPHICS_AND_PRESENT.presentKHR(
            {.sType = sType<VkPresentInfoKHR>(),
             .waitSemaphoreCount = 1,
             .pWaitSemaphores = &renderFinishedSemaphore[imageIndex],
             .swapchainCount = 1,
             .pSwapchains = &(*swapchain),
             .pImageIndices = &imageIndex});
        if (auto &framebufferResized = window.refFramebufferResized();
            result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            framebufferResized)
        {
            framebufferResized = false;
            recreateSwapChain(world);
        }
        semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphore.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    };
    while (globalCtx.window.shouldClose() == 0)
    {
        invoke_aggregate_ranges<"ProcessingWorldInput_", 0, 0>(vulaknDataTransformer,
                                                               world);

        //NOTE: 一般是鼠标选中设置指定的mesh。 暂时不知道如何组织。单个实体.或许需要事件系统？
        // diff: [test_dod5] start
        // 定时器：每 2 秒切换纹理和网格 //NOTE: 400 - 470 左右。更新影响50左右的帧率
        {
            static auto lastUpdate = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now - lastUpdate > std::chrono::seconds(2))
            {
                lastUpdate = now;

                // 1. 选择一个新的纹理槽位（循环使用或找空闲）
                static uint32_t dynamicSlot = 0;
                // 若当前槽已被占用，按序找下一个空闲槽，找不到就复用 0
                while (activeTextureSlots.count(dynamicSlot) > 0 &&
                       dynamicSlot < MAX_TEXTURES)
                    ++dynamicSlot;
                if (dynamicSlot >= MAX_TEXTURES)
                    dynamicSlot = 0; // 简单环绕

                // 2. 创建新纹理（动态 mipmap 切换或固定文件）
                static bool mipmap = false;
                mipmap = !mipmap;
                textureMap[dynamicSlot] = create_texture.templateForImage2d(
                    device, commandPool, GRAPHICS_AND_PRESENT,
                    raw_stbi_image{"textures/viking_room.png", STBI_rgb_alpha}, mipmap);
                activeTextureSlots.insert(dynamicSlot);

                // 3. 更新所有飞行帧的描述符（绑定 1 的指定槽位）
                for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
                {
                    VkDescriptorImageInfo imgInfo{
                        .sampler = nullptr,
                        .imageView = textureMap[dynamicSlot].imageView(),
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    VkWriteDescriptorSet write{.sType = sType<VkWriteDescriptorSet>(),
                                               .dstSet = descriptorSets[frame],
                                               .dstBinding = 1,
                                               .dstArrayElement = dynamicSlot,
                                               .descriptorCount = 1,
                                               .descriptorType =
                                                   VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                               .pImageInfo = &imgInfo};
                    device.updateDescriptorSets(1, &write, 0, nullptr);
                }

                // 4. 修改“自旋”实体的纹理索引（直接通过 SoA 的 view_entity 引用）
                //    autoSpinId 是实体的 ID，我们这里只需拿到第 0 号实体的纹理索引引用
                //    （假设只有一个自旋实体，id == 0）
                auto entityId = 0; // 或你已有的 autoSpinId
                auto [texRef] = autoSpinStore.view_entity<"textureData">(0, entityId);
                texRef = dynamicSlot; // 立即更新实体记录的纹理槽位

                // 5. 动态重建网格（三角形 / 四边形切换）
                static bool useQuad = true;
                if (useQuad)
                {
                    // 生成 NxN 网格（N 在 2~8 之间循环）
                    static int N = 2;
                    N = (N % 7) + 2; // 2,3,4,...,8

                    std::vector<Vertex> newVertices;
                    std::vector<uint32_t> newIndices;
                    newVertices.reserve(N * N);                // 顶点总数
                    newIndices.reserve((N - 1) * (N - 1) * 6); // 三角形索引总数
                    float step = 1.0f / (N - 1);
                    for (int j = 0; j < N; ++j)
                        for (int i = 0; i < N; ++i)
                            newVertices.push_back(
                                {.pos = {-0.5f + i * step, -0.5f + j * step, 0.0f},
                                 .color = {1.0f, 1.0f, 1.0f},
                                 .texCoord = {static_cast<float>(i) / (N - 1),
                                              static_cast<float>(j) / (N - 1)}});
                    for (int j = 0; j < N - 1; ++j)
                        for (int i = 0; i < N - 1; ++i)
                        {
                            uint32_t tl = j * N + i;
                            uint32_t tr = tl + 1;
                            uint32_t bl = (j + 1) * N + i;
                            uint32_t br = bl + 1;
                            newIndices.insert(newIndices.end(), {tl, bl, br, tl, br, tr});
                        }

                    // 排队更新：通过 SoA 的 vertexUpdateQueue 操作
                    std::cout << "网格切换为 " << N << "x" << N
                              << "，三角形数: " << newIndices.size() / 3 << '\n';
                    auto [vertexUpdateQueue] =
                        autoSpinStore.view_entity<"vertexUpdateQueue">(0, entityId);
                    vertexUpdateQueue = mesh::vertex_raw_data{std::move(newVertices),
                                                              std::move(newIndices),
                                                              MAX_FRAMES_IN_FLIGHT};
                }
                else
                {
                    // 回退到原始四边形
                    auto [vertexUpdateQueue] =
                        autoSpinStore.view_entity<"vertexUpdateQueue">(0, entityId);
                    vertexUpdateQueue =
                        mesh::vertex_raw_data{vertices, indices, MAX_FRAMES_IN_FLIGHT};
                    std::cout << "网格切换为原始四边形\n";
                }
                useQuad = !useQuad;
            }
        }
        // diff: [test_dod5] end
        drawFrame(world);
    }
    device.waitIdle();

    // diff: [test_dod2] 不再有 手动 map unmap

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}