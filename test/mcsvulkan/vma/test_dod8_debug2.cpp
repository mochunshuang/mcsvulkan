
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
    glm::vec2 texCoord; // diff: [texture] 添加纹理坐标
};
struct VertexAttribute
{
    glm::vec3 color; // 仅颜色
};

namespace mesh
{
    using buffer_base = mcs::vulkan::memory::buffer_base;
    using auto_map_buffer = mcs::vulkan::memory::auto_map_buffer;
    using mcs::vulkan::memory::create_simple_buffer;
    using mcs::vulkan::memory::create_staging_buffer;

    using index_type = uint32_t;

    using position_3d = glm::vec3;

    static auto get_topLeftLocal(const std::span<const Vertex> &vertices) noexcept
    { // 计算包围盒和左上角坐标
        assert(!vertices.empty());
        glm::vec3 minPos = vertices[0].pos;
        glm::vec3 maxPos = vertices[0].pos;
        for (const auto &v : vertices)
        {
            minPos = glm::min(minPos, v.pos);
            maxPos = glm::max(maxPos, v.pos);
        }
        // 左上角 = (minX, maxY, maxZ)
        return glm::vec3(minPos.x, maxPos.y, maxPos.z);
    }

}; // namespace mesh

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

//diff: [test_dod8] start
struct object_data
{
    glm::mat4 matrix;
};
struct InstanceData
{
    uint32_t objectId;
    uint32_t textureIndex; // 纹理数组索引
    uint32_t samplerIndex; // 采样器数组索引
    object_data objectData;
};
struct CommandConstant
{
    uint32_t perInstanceAttributeCount;
};
struct PushData
{
    uint64_t vertexAddress;           // 全局顶点缓冲区地址
    uint64_t attributeAddress;        // 全局属性池地址
    uint64_t instanceAddress;         // 全局实例缓冲区地址
    uint64_t commandConstantsAddress; // 新增：命令常量缓冲区地址
};

struct Mesh
{
    uint32_t vertexCount;  // 网格的顶点数量
    uint32_t vertexOffset; // 在全局顶点池中的偏移
    uint32_t indexOffset;  // 在全局索引池中的偏移
    uint32_t indexCount;   // 索引数量

    [[nodiscard]] VkDrawIndexedIndirectCommand getDrawCommand(
        uint32_t instanceCount, uint32_t firstInstance = 0) const noexcept
    {
        return {.indexCount = indexCount,
                .instanceCount = instanceCount,
                .firstIndex = indexOffset,
                .vertexOffset = static_cast<int32_t>(vertexOffset),
                .firstInstance = firstInstance};
    }
};

struct PerFrameBatch
{
    mcs::vulkan::memory::auto_map_buffer globalVertexBuffer{};
    VkDeviceSize globalVertexBufferCapacity = 0;
    VkDeviceAddress globalVertexBufferAddress = 0;

    mcs::vulkan::memory::auto_map_buffer globalIndexBuffer{};
    VkDeviceSize globalIndexBufferCapacity = 0;

    mcs::vulkan::memory::auto_map_buffer globalInstanceBuffer{};
    VkDeviceSize globalInstanceBufferCapacity = 0;
    VkDeviceAddress globalInstanceBufferAddress = {};

    mcs::vulkan::memory::auto_map_buffer globalAttributeBuffer{};
    VkDeviceSize globalAttributeBufferCapacity = 0;
    VkDeviceAddress globalAttributeBufferAddress = 0;

    mcs::vulkan::memory::auto_map_buffer indirectDrawBuffer{};
    VkDeviceSize indirectDrawBufferCapacity = 0;

    // 新增：命令常量缓冲区
    mcs::vulkan::memory::auto_map_buffer commandConstantsBuffer{};
    VkDeviceAddress commandConstantsBufferAddress = 0;

    uint32_t drawCount = 0;
};
//diff: [test_dod8] end

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

    //diff: [test_dod8] start
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;
    std::unordered_map<std::string, Mesh> meshMap;
    auto addMesh = [&](const std::string &name, const std::vector<Vertex> &verts,
                       const std::vector<uint32_t> &indices) {
        uint32_t vOff = static_cast<uint32_t>(allVertices.size());
        uint32_t iOff = static_cast<uint32_t>(allIndices.size());
        allVertices.insert(allVertices.end(), verts.begin(), verts.end());
        allIndices.insert(allIndices.end(), indices.begin(), indices.end());
        meshMap[name] = {static_cast<uint32_t>(verts.size()), vOff, iOff,
                         static_cast<uint32_t>(indices.size())};
    };

    // 构建全局几何池

    const std::vector<Vertex> quadVerts = {
        {{-0.5f, -0.5f, 0}, {0.0F, 0.0F}}, // 左下
        {{0.5, -0.5, 0}, {1.0F, 0.0F}},    // 右下
        {{0.5, 0.5, 0}, {1.0F, 1.0F}},     // 右上
        {{-0.5, 0.5, 0}, {0.0F, 1.0F}}     // 左上
    };
    const std::vector<uint32_t> quadIdx = {0, 1, 2, 0, 2, 3};
    addMesh("quad", quadVerts, quadIdx);

    constexpr uint32_t MAX_INSTANCE_COUNT = 1000;
    constexpr uint32_t MAX_DRAW_CALLS = 10;

    std::array<PerFrameBatch, MAX_FRAMES_IN_FLIGHT> batches;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        auto &batche = batches[i];
        {
            auto &buffer = batche.globalVertexBuffer;
            auto &capacity = batche.globalVertexBufferCapacity;
            auto &address = batche.globalVertexBufferAddress;
            capacity = allVertices.size() * sizeof(Vertex);
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = capacity,
                     .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                capacity);
            address = device.getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(), .buffer = buffer.buffer()});
            memcpy(buffer.mapPtr(), allVertices.data(), capacity);
        }
        {
            auto &buffer = batche.globalIndexBuffer;
            auto &capacity = batche.globalIndexBufferCapacity;
            capacity = allIndices.size() * sizeof(uint32_t);
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = capacity,
                     .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                capacity);
            memcpy(buffer.mapPtr(), allIndices.data(), capacity);
        }
        {
            constexpr VkDeviceSize MAX_INSTANCE_DATA_SIZE =
                MAX_DRAW_CALLS * MAX_INSTANCE_COUNT * sizeof(InstanceData);

            auto &buffer = batche.globalInstanceBuffer;
            auto &capacity = batche.globalInstanceBufferCapacity;
            auto &address = batche.globalInstanceBufferAddress;
            capacity = MAX_INSTANCE_DATA_SIZE;
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = capacity,
                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                capacity);
            address = device.getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(), .buffer = buffer.buffer()});

            // NOTE: 绘制的时候，每帧填充
        }
        {

            constexpr VkDeviceSize MAX_Attribute_SIZE = 1 * 1024 * 1024;

            auto &buffer = batche.globalAttributeBuffer;
            auto &capacity = batche.globalAttributeBufferCapacity;
            auto &address = batche.globalAttributeBufferAddress;
            capacity = MAX_Attribute_SIZE;
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = capacity,
                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                capacity);
            address = device.getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(), .buffer = buffer.buffer()});

            // NOTE: 绘制的时候，每帧填充
        }
        {

            constexpr VkDeviceSize MAX_IndirectDraw_SIZE = 1 * 1024 * 1024;

            auto &buffer = batche.indirectDrawBuffer;
            auto &capacity = batche.indirectDrawBufferCapacity;
            capacity = MAX_IndirectDraw_SIZE;
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = capacity,
                     .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                capacity);
            // NOTE: 绘制的时候，每帧填充
        }
        {
            // 新增 command constants buffer
            constexpr VkDeviceSize MAX_CMD_CONST_SIZE =
                MAX_DRAW_CALLS * sizeof(CommandConstant);
            batche.commandConstantsBuffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = MAX_CMD_CONST_SIZE,
                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                MAX_CMD_CONST_SIZE);
            batche.commandConstantsBufferAddress = device.getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(),
                 .buffer = batche.commandConstantsBuffer.buffer()});
        }
    }

    // diff: [test_dod8] end

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
            .setCreateInfo(
                {.setLayouts = {*descriptorSetLayout},
                 // diff: [test_dod8] 推送常量传说地址更快，没有绑定的开销？
                 .pushConstantRanges = {{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                         .offset = 0,
                                         .size = sizeof(PushData)}}})
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

    // diff: [test_dod2] end

    //diff: [test_dod8.cpp] start
    using auto_spin_data_type =
        gen_soa_aggregate<{"instanceData", ^^InstanceData},
                          {"vertexAttribute", ^^std::array<VertexAttribute, 4>}>;
    auto_spin_data_type autoSpinStore{1};

    using interactive_type =
        gen_soa_aggregate<{"instanceData", ^^InstanceData},
                          {"vertexAttribute", ^^std::array<VertexAttribute, 4>},
                          {"topLeftLocal", ^^glm::vec3}, {"modelState", ^^model_state}>;
    interactive_type interactiveStore{1};

    // NOTE: spinElapsed 全部mesh共享？
    auto autoSpinStoreShareData =
        make_aggregate<"autoSpinStoreShareData", "spinElapsed">(float{});
    auto soaCtx = make_aggregate_ref<"soaCtx", "autoSpinStore", "interactiveStore",
                                     "autoSpinStoreShareData">(
        autoSpinStore, interactiveStore, autoSpinStoreShareData);

    //diff: [test_dod8.cpp] end

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
                           "commandBuffers", "queue", "meshMap">(
            device, window, surface, swapchainBuild, swapchain, camera, frameContext,
            commandPool, commandBuffers, GRAPHICS_AND_PRESENT, meshMap);

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

    //diff: [test_dod8] start
    auto global_id = 0;
    auto autoSpinId =
        autoSpinStore.new_entity(InstanceData{global_id++, 0, 0, object_data{}},
                                 std::array<VertexAttribute, 4>{
                                     VertexAttribute{glm::vec3{1.0f, 0.0f, 0.0f}}, // 红
                                     VertexAttribute{glm::vec3{1.0f, 0.0f, 0.0f}}, // 红
                                     VertexAttribute{glm::vec3{0.0f, 1.0f, 0.0f}}, // 绿
                                     VertexAttribute{glm::vec3{0.0f, 1.0f, 0.0f}}, // 绿
                                 });
    auto interactiveId = interactiveStore.new_entity(
        InstanceData{global_id++, 3, 1, object_data{}},
        std::array<VertexAttribute, 4>{
            VertexAttribute{{1.0f, 0.0f, 1.0f}}, // 品红
            VertexAttribute{{0.0f, 1.0f, 1.0f}}, // 青
            VertexAttribute{{1.0f, 0.5f, 0.0f}}, // 橙
            VertexAttribute{{0.5f, 0.0f, 0.5f}}, // 紫
        },
        mesh::get_topLeftLocal(std::span{quadVerts}), model_state{});

    // 随机生成两个纹理索引和采样器索引

    //diff: [test_dod3] end

    // diff: [test_dod2] end

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

        //diff: [test_dod8] start
        auto start = std::chrono::high_resolution_clock::now();
        uint32_t idx = 0;
        {
            // Buffer is already mapped. You can access its memory.
            for (auto [instanceData] : autoSpinStore.view<"instanceData">(currentFrame))
            {
                static_assert(std::is_reference_v<decltype(instanceData)>);
                auto &mat = instanceData.objectData.matrix;
                if (idx == 0) [[unlikely]]
                {
                    float phaseOffset = idx * glm::radians(137.5f);
                    auto angle = spinElapsed * glm::radians(90.0f) + phaseOffset;
                    mat =
                        glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));
                    mat = glm::mat4(1.0f);
                }
                else [[likely]]
                {
                    // 提取现有的平移和缩放
                    glm::vec3 translation = glm::vec3(mat[3]);       // 第4列是平移
                    glm::vec3 scale(glm::length(glm::vec3(mat[0])),  // 第1列长度 = X缩放
                                    glm::length(glm::vec3(mat[1])),  // 第2列长度 = Y缩放
                                    glm::length(glm::vec3(mat[2]))); // 第3列长度 = Z缩放

                    float phaseOffset = idx * glm::radians(137.5f);
                    float angle = spinElapsed * glm::radians(90.0f) + phaseOffset;
                    glm::mat4 rot =
                        glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));

                    // 重构：缩放 → 旋转 → 平移（与创建时的顺序保持一致）
                    mat = glm::translate(glm::mat4(1.0f), translation) * rot *
                          glm::scale(glm::mat4(1.0f), scale);
                }

                ++idx;
            }
            for (const auto &[instanceData, modelState] :
                 interactiveStore.view<"instanceData", "modelState">(currentFrame))
            {
                //diff: [test_dod8] 构造时的偏移迁移到这里
                auto &objectData = instanceData.objectData;
                modelState.model_matrix.translation.z = 0.5f;
                objectData = object_data{modelState.model_matrix()};
            }
        }
        // 3. 记录结束时间点
        auto end = std::chrono::high_resolution_clock::now();

        // 4. 计算时间差，并转换为毫秒
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // 5. 打印耗时
        if (auto count = duration.count(); count > 0)
            std::cout << "updateObjectData耗时: " << duration.count()
                      << " 毫秒,数据量: " << idx << std::endl;
        //diff: [test_dod8] end
    };
    // diff: [test_model_matrix2] end

    static constexpr auto updateVertexData = [](world_type &world,
                                                uint32_t currentFrame) noexcept {
        auto &soaCtx = world.soaCtx;

        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;
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
        auto &meshMap = globalCtx.meshMap;

        auto &descriptorSets = mainShaderCtx.descriptorSets;
        auto &batches = mainShaderCtx.indirectDrawBatches;

        auto &descriptorSet = descriptorSets[currentFrame];
        auto &batch = batches[currentFrame];

        auto &soaCtx = world.soaCtx;
        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;

        auto start = std::chrono::high_resolution_clock::now();
        //diff: [test_dod8] start
        {
            static std::vector<VkDrawIndexedIndirectCommand> g_cmds;
            static std::vector<InstanceData> g_allInstances;
            static std::vector<VertexAttribute> g_allAttributes;

            // 临时命令和常量数组
            std::vector<VkDrawIndexedIndirectCommand> cmds;
            std::vector<CommandConstant> cmdConsts;

            // 用于写入 buffer 的偏移
            VkDeviceSize instanceOffset = 0;
            VkDeviceSize attributeOffset = 0;

            // 处理一个 store（它绑定到一个 mesh，mesh 提供顶点数）
            auto processStore = [&](auto &store, const Mesh &mesh, uint32_t frame) {
                uint32_t count = store.size(); // 存活实体数
                if (count == 0)
                    return;
                // ★ 计算正确的 firstInstance
                uint32_t firstInstance =
                    static_cast<uint32_t>(instanceOffset / sizeof(InstanceData));

                // 1. 整体拷贝实例数据（每个实体一个 InstanceData）
                // 假设 store 中有 "instanceData" 字段，且类型为 InstanceData
                InstanceData *instSrc =
                    store.template raw_field<"instanceData">(frame).data();
                VkDeviceSize instSize = count * sizeof(InstanceData);
                memcpy((char *)batch.globalInstanceBuffer.mapPtr() + instanceOffset,
                       instSrc, instSize);

                // 2. 整体拷贝属性数据（每个实体一个 std::array<VertexAttribute, 4>）
                std::array<VertexAttribute, 4> *attrSrc =
                    store.template raw_field<"vertexAttribute">(frame).data();
                VkDeviceSize attrSize = count * sizeof(std::array<VertexAttribute, 4>);
                memcpy((char *)batch.globalAttributeBuffer.mapPtr() + attributeOffset,
                       attrSrc, attrSize);

                // 3. 生成间接命令
                cmds.push_back(mesh.getDrawCommand(count, firstInstance));

                // 4. 生成命令常量
                cmdConsts.push_back(CommandConstant{mesh.vertexCount});

                instanceOffset += instSize;
                attributeOffset += attrSize;

                // std::cout << "写入了实体: " << count << std::endl;
            };

            const Mesh &quadMesh = meshMap["quad"];
            processStore(autoSpinStore, quadMesh, currentFrame);
            // processStore(interactiveStore, quadMesh, currentFrame);

            uint32_t drawCount = static_cast<uint32_t>(cmds.size());
            batch.drawCount = drawCount;

            // 上传命令和常量
            memcpy(batch.indirectDrawBuffer.mapPtr(), cmds.data(),
                   drawCount * sizeof(VkDrawIndexedIndirectCommand));
            memcpy(batch.commandConstantsBuffer.mapPtr(), cmdConsts.data(),
                   drawCount * sizeof(CommandConstant));
        }
        //diff: [test_dod8] end
        // 3. 记录结束时间点
        auto end = std::chrono::high_resolution_clock::now();

        // 4. 计算时间差，并转换为毫秒
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // NOTE: 函数执行耗时: 2 毫秒. [FPS]  77.1 (avg frame: 12.963 ms) 占用了1/6
        // 5. 打印耗时
        if (auto count = duration.count(); count > 0)
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
            auto &mainCtx = world.mainCtx;

            auto &commandBuffers = globalCtx.commandBuffers;
            auto &pipelineLayout = mainCtx.pipelineLayout;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto &batches = mainShaderCtx.indirectDrawBatches;

            // diff: [test_dod8] start 使用合并索引缓冲和间接绘制
            auto &batch = batches[currentFrame];
            commandBuffer.bindIndexBuffer(batch.globalIndexBuffer.buffer(), 0,
                                          VK_INDEX_TYPE_UINT32);
            PushData pushConstants = {
                .vertexAddress = batch.globalVertexBufferAddress,
                .attributeAddress = batch.globalAttributeBufferAddress,
                .instanceAddress = batch.globalInstanceBufferAddress,
                .commandConstantsAddress = batch.commandConstantsBufferAddress};
            commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(PushData), &pushConstants);
            commandBuffer.drawIndexedIndirect(batch.indirectDrawBuffer.buffer(), 0,
                                              batch.drawCount,
                                              sizeof(VkDrawIndexedIndirectCommand));
            // diff: [test_dod8] start
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
                    // ubo.proj[1][1] *= -1; //diff: [test_dod8_debug2] 让 Y 翻转的操作。 BUG的来源

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

        drawFrame(world);

        // FPS 统计
        {
            static auto lastTime = std::chrono::high_resolution_clock::now();
            static int frameCount = 0;
            static int totalFrames = 0;
            static float accumulatedFPS = 0.0f;
            static int avgSamples = 0;

            frameCount++;
            totalFrames++;

            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(now - lastTime).count();
            if (elapsed >= 1.0F)
            {
                float fps = frameCount / elapsed;
                accumulatedFPS += fps;
                avgSamples++;
                float avgFPS = accumulatedFPS / avgSamples;
                float frameTimeMs = (elapsed / frameCount) * 1000.0f;
                std::println(
                    "FPS: {:.1f} (avg: {:.1f}) | FrameTime: {:.2f}ms | Total: {}", fps,
                    avgFPS, frameTimeMs, totalFrames);
                frameCount = 0;
                lastTime = now;
            }
        }
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