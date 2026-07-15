
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
#include <memory>
#include <optional>
#include <print>
#include <chrono>
#include <random>
#include <span>
#include <stdexcept>
#include <stdint.h>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "../head.hpp"

using mcs::vulkan::Instance;
using mcs::vulkan::tool::create_instance;
using mcs::vulkan::tool::create_debugger;
using mcs::vulkan::tool::create_physical_device_selector;
using mcs::vulkan::vkMakeVersion;
using mcs::vulkan::vkApiVersion;

using mcs::vulkan::tool::enable_intance_build;
using mcs::vulkan::tool::structure_chain;
using mcs::vulkan::tool::create_queue_family_index_selector;
using mcs::vulkan::tool::create_logical_device;
using mcs::vulkan::tool::create_swapchain;
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
using mcs::vulkan::Debugger;
using mcs::vulkan::surface_impl;
using mcs::vulkan::wsi::glfw::Window;
using mcs::vulkan::Queue;

using mcs::vulkan::LogicalDevice;

using mcs::vulkan::tool::make_pNext;

using mcs::vulkan::CommandPool;
using mcs::vulkan::CommandBuffers;
using mcs::vulkan::CommandBuffer;
using mcs::vulkan::CommandBufferView;
using mcs::vulkan::Fence;

using mcs::vulkan::DescriptorSetLayout;
using mcs::vulkan::DescriptorPool;
using mcs::vulkan::DescriptorSets;
using mcs::vulkan::DescriptorSetLayout;

using raii_vma = mcs::vulkan::raii_vma;

using mcs::vulkan::tool::simple_copy_buffer;

using mcs::vulkan::meta::make_aggregate_ref;
using mcs::vulkan::meta::make_aggregate;

using mcs::vulkan::ecs::gen_soa_aggregate;
using mcs::vulkan::ecs::gen_soa_struct;

using mcs::vulkan::task::make_task;
using mcs::vulkan::task::init_task;
using mcs::vulkan::task::schedulable_task;

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
    using mcs::vulkan::memory::buffer_base;
    using mcs::vulkan::memory::auto_map_buffer;
    using mcs::vulkan::memory::create_simple_buffer;
    using mcs::vulkan::memory::create_staging_buffer;

    using index_type = uint32_t;

    using position_3d = glm::vec3;

}; // namespace mesh

namespace font
{
    // 2. Library Initialization
    using freetype_loader = mcs::vulkan::font::freetype::loader;

    using mcs::vulkan::font::FontType;

    using mcs::vulkan::font::texture_info;
    using mcs::vulkan::font::FontInfo;
    using mcs::vulkan::font::font_register;

    using mcs::vulkan::font::font_registration;

    using mcs::vulkan::font::GenFontContext;
    using mcs::vulkan::font::GenFontFactory;
    using mcs::vulkan::font::GenFontSelector;
    using mcs::vulkan::font::make_font_factory;

}; // namespace font

// diff: [test_dod14] start: 让3D和UI各自一份。推送常量切换
struct CameraInfo
{
    glm::mat4 view;
    glm::mat4 proj;
};
struct UniformBufferObject
{
    CameraInfo cameraInfo[2]; // 0: 3D, 1: UI
};
// diff: [test_dod14] end

// diff: [texture] start

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

// diff: [camera_perspective] end

// diff: [camera_model] start

// diff: [camera_model] end

//diff: [test_dod8] start
struct object_data
{
    glm::mat4 matrix;
};
struct VertexTransform
{
    glm::mat4 matrix = glm::mat4(1.0f);
};
struct UvTransform
{
    glm::vec2 scale{1.0f, 1.0f};
    glm::vec2 offset{0.0f, 0.0f};

    // 默认：完整纹理
    constexpr static UvTransform identity() noexcept
    {
        return {};
    }

    // 从目标矩形的四个纹理坐标构造
    static UvTransform from_target_verts(std::span<const glm::vec2> target)
    {
        // 对轴对齐矩形直接取边界
        float l = std::min({target[0].x, target[1].x, target[2].x, target[3].x});
        float r = std::max({target[0].x, target[1].x, target[2].x, target[3].x});
        float b = std::min({target[0].y, target[1].y, target[2].y, target[3].y});
        float t = std::max({target[0].y, target[1].y, target[2].y, target[3].y});
        return {.scale = {r - l, t - b}, .offset = {l, b}};
    }

    // 也可以接受 origin（默认为标准 [0,1]²）和目标，用于完整仿射
    static UvTransform from_verts(std::span<const glm::vec2> origin,
                                  std::span<const glm::vec2> target)
    {
        // 与 from_target_verts 类似，但考虑 origin 缩放
        float ol = std::min({origin[0].x, origin[1].x, origin[2].x, origin[3].x});
        float or_ = std::max({origin[0].x, origin[1].x, origin[2].x, origin[3].x});
        float ob = std::min({origin[0].y, origin[1].y, origin[2].y, origin[3].y});
        float ot = std::max({origin[0].y, origin[1].y, origin[2].y, origin[3].y});

        float tl = std::min({target[0].x, target[1].x, target[2].x, target[3].x});
        float tr = std::max({target[0].x, target[1].x, target[2].x, target[3].x});
        float tb = std::min({target[0].y, target[1].y, target[2].y, target[3].y});
        float tt = std::max({target[0].y, target[1].y, target[2].y, target[3].y});

        glm::vec2 originSize{or_ - ol, ot - ob};
        glm::vec2 targetSize{tr - tl, tt - tb};
        glm::vec2 originBase{ol, ob};
        glm::vec2 targetBase{tl, tb};

        glm::vec2 scale = targetSize / originSize;
        glm::vec2 offset = targetBase - originBase * scale;

        return {scale, offset};
    }
};

//diff: [test_dod16] start
struct object_key
{
    uint32_t object_type : 8;   // 假设高 8 位
    uint32_t entity_index : 24; // 低 24 位

    static consteval uint32_t max_type_value(int bit_with) noexcept
    {
        return (1u << bit_with) - 1; // 255
    }

    constexpr object_key(uint32_t type, uint32_t index) noexcept
        : object_type{type}, entity_index{index}
    {
        assert(type <= max_type_value(8));
        assert(type <= max_type_value(24));
    }
    constexpr object_key() : object_key(0, 0) {}

    // 显式组合位，不依赖内存布局
    constexpr operator uint32_t() const noexcept
    {
        return (static_cast<uint32_t>(object_type) << 24) | (entity_index & 0xFFFFFFu);
    }
};
static_assert(sizeof(object_key) == sizeof(uint32_t));
struct picking_result
{
    object_key key;
    uint32_t primitive_id;
};

struct InstanceData
{
    object_key objectId;
    uint32_t textureIndex; // 纹理数组索引
    uint32_t samplerIndex; // 采样器数组索引
    object_data objectData;
    VertexTransform vertexTransform; //diff: [test_dod13]
    UvTransform uvTransform;

    //diff: [test_dod12] start
    uint32_t fontType = static_cast<uint32_t>(font::FontType::eNONE);
    float pxRange;         //msdf 需要
    uint32_t modulateFlag; // 混合需要
    //diff: [test_dod12] end
};
//diff: [test_dod16] end

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
    uint32_t cameraIndex;
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

namespace camera
{
    using mcs::vulkan::camera::composeTRS;
    using mcs::vulkan::camera::extractTranslationScale;
    using mcs::vulkan::camera::VulkanNDCConfig;
    using mcs::vulkan::camera::transform;
    using mcs::vulkan::camera::computeAnchorOffset;
    using mcs::vulkan::camera::transformPointToWorld;
    using mcs::vulkan::camera::RightHandedView;
    using mcs::vulkan::camera::LeftHandedView;
    using mcs::vulkan::camera::VulkanPerspectiveProjection;
    using mcs::vulkan::camera::VulkanOrthographicProjection;
    using mcs::vulkan::camera::VulkanUIOrthographicProjection;
    using mcs::vulkan::camera::GenCamera;
}; // namespace camera

using model_matrix = camera::transform;
using mcs::vulkan::match;

struct FrameClock
{
    using Clock = std::chrono::steady_clock;    // 单调时钟，适合测时间间隔
    Clock::time_point startTime = Clock::now(); // 程序启动时自动记录
    Clock::time_point lastTime = startTime;
    float deltaTime = 0.016f;

    // 返回从 startTime 到现在的秒数（float）
    float getElapsed() const noexcept
    {
        return std::chrono::duration<float>(Clock::now() - startTime).count();
    }
};
constexpr auto init()
{
    //NOTE: make_unique 保证地址稳定
    auto ctx = std::make_unique<raii_vulkan>();
    auto window = std::make_unique<Window>();
    window->setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT

    auto enables = enable_intance_build{}
                       .enableDebugExtension()
                       .enableValidationLayer()
                       .enableSurfaceExtension<Window>();

    constexpr auto APIVERSION = VK_API_VERSION_1_4;
    enables.check();
    enables.print();
    auto instance = std::make_unique<Instance>(
        create_instance{}
            .setCreateInfo(
                {.applicationInfo = {.pApplicationName = "Hello Triangle",
                                     .applicationVersion = vkMakeVersion(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = vkMakeVersion(1, 0, 0),
                                     .apiVersion = APIVERSION},
                 .enabledLayers = enables.enabledLayers(),
                 .enabledExtensions = enables.enabledExtensions()})
            .build());
    auto debuger = std::make_unique<Debugger>(
        create_debugger{}
            .setCreateInfo(create_debugger::defaultCreateInfo())
            .build(*instance.get()));
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
        create_physical_device_selector{}
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
            .select(*instance.get())[0];
    auto physical = std::make_unique<PhysicalDevice>(std::move(physical_device));
    auto surface = std::make_unique<surface_impl<Window>>(*physical.get(), *window.get());
    const uint32_t GRAPHICS_QUEUE_FAMILY_IDX =
        create_queue_family_index_selector{}
            .requiredQueueFamily([&](const VkQueueFamilyProperties &qfp,
                                     uint32_t queueFamilyIndex) -> bool {
                return (qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                       physical->getSurfaceSupportKHR(queueFamilyIndex, **surface);
            })
            .select(*physical.get())[0];
    auto device = std::make_unique<LogicalDevice>(
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
            .build(*physical.get()));
    auto GRAPHICS_AND_PRESENT = std::make_unique<Queue>(
        Queue{*device.get(),
              {.queue_family_index = GRAPHICS_QUEUE_FAMILY_IDX, .queue_index = 0}});
    auto commandPool = std::make_unique<CommandPool>(
        create_command_pool{}
            .setCreateInfo({.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                            .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX})
            .build(*device.get()));
    auto commandBuffers =
        std::make_unique<CommandBuffers>(commandPool->allocateCommandBuffers(
            {.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
             .commandBufferCount = MAX_FRAMES_IN_FLIGHT}));

    auto swapchainBuild =
        create_swapchain{}
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
    auto swapchain = swapchainBuild.build(*device.get(), *surface.get());
    frame_context<MAX_FRAMES_IN_FLIGHT> frameContext{*device.get(),
                                                     swapchain.imagesSize()};
    // hardware
    return make_aggregate<"hardwareCtx", "vulkan", "window", "instance", "debuger",
                          "physicalDevice", "surface", "device", "queue", "commandPool",
                          "commandBuffers", "swapchainBuild", "swapchain",
                          "frameContext">(
        std::move(ctx), std::move(window), std::move(instance), std::move(debuger),
        std::move(physical), std::move(surface), std::move(device),
        std::move(GRAPHICS_AND_PRESENT), std::move(commandPool),
        std::move(commandBuffers), std::move(swapchainBuild), std::move(swapchain),
        std::move(frameContext));
}

template <class resource_type, uint32_t MAX_TEXTURES>
struct resource_manager
{
    using slot_type = uint32_t;
    static constexpr slot_type MARK_FREE = 0;
    static constexpr slot_type MARK_USED = 1;
    static constexpr auto invalid_index = (std::numeric_limits<slot_type>::max)();

    // 状态标记：0 空闲，1 已占用
    std::array<slot_type, MAX_TEXTURES> slot_status{};
    // 纹理资源数组
    std::array<resource_type, MAX_TEXTURES> resources;
    std::array<std::string, MAX_TEXTURES> resources_key;

    constexpr void use_slot(slot_type index, resource_type resource,
                            std::string key) noexcept
    {
        mark_used(index);
        resources[index] = std::move(resource);
        resources_key[index] = std::move(key);
    }
    constexpr void free_slot(slot_type index) noexcept
    {
        mark_free(index);
        resources[index] = {};
        resources_key[index] = {};
    }
    constexpr std::optional<slot_type> find_slot_by_name(std::string key) const noexcept
    {
        for (slot_type i = 0; i < resources_key.size(); ++i)
        {
            if (resources_key[i] == key)
                return i;
        }
        return std::nullopt;
    }

    constexpr auto free_count() const noexcept
    {
        return std::ranges::count(slot_status, MARK_FREE);
    }
    constexpr auto used_count() const noexcept
    {
        return std::ranges::count(slot_status, MARK_USED);
    }
    constexpr bool is_free(slot_type index) const noexcept
    {
        return slot_status[index] == MARK_FREE;
    }
    constexpr bool is_used(slot_type index) const noexcept
    {
        return slot_status[index] == MARK_USED;
    }
    constexpr void mark_free(slot_type index) noexcept
    {
        assert(is_used(index) && "该槽已经是空闲的(Double free)");
        slot_status[index] = MARK_FREE;
    }
    constexpr void mark_used(slot_type index) noexcept
    {
        assert(is_free(index) && "该槽已经是空闲的(Double used)");
        slot_status[index] = MARK_USED;
    }
    constexpr auto view_free_indexes() const noexcept
    {
        return slot_status | std::views::enumerate |
               std::views::filter([](auto tup) noexcept {
                   auto [index, status] = tup;
                   return status == MARK_FREE;
               }) |
               std::views::transform([](auto tup) noexcept -> slot_type {
                   auto [index, status] = tup;
                   return index;
               });
    }
    constexpr auto view_used_indexes() const noexcept
    {
        return slot_status | std::views::enumerate |
               std::views::filter([](auto tup) noexcept {
                   auto [index, status] = tup;
                   return status == MARK_USED;
               }) |
               std::views::transform([](auto tup) noexcept -> slot_type {
                   auto [index, status] = tup;
                   return index;
               });
    }
    constexpr auto view_used_textures() const noexcept
    {
        return view_used_indexes() |
               std::views::transform([&](slot_type i) noexcept -> auto {
                   return std::make_pair(i, &resources[i]);
               });
    }
    struct auto_free_slot_type
    {
      private:
        resource_manager *manager{nullptr};
        slot_type index{invalid_index};

      public:
        constexpr auto valid() const noexcept
        {
            return manager != nullptr;
        }
        constexpr operator bool() const noexcept
        {
            return valid();
        }
        auto_free_slot_type() = default;
        constexpr auto_free_slot_type(resource_manager &mgr, slot_type idx) noexcept
            : manager(&mgr), index(idx)
        {
        }
        constexpr ~auto_free_slot_type() noexcept
        {
            if (manager)
            {
                manager->free_slot(index);
                manager = nullptr;
                index = invalid_index;
            }
        }
        auto_free_slot_type(const auto_free_slot_type &) = delete;
        auto_free_slot_type &operator=(const auto_free_slot_type &) = delete;
        constexpr auto_free_slot_type(auto_free_slot_type &&other) noexcept
            : manager(std::exchange(other.manager, nullptr)),
              index(std::exchange(other.index, invalid_index))
        {
        }
        constexpr auto_free_slot_type &operator=(auto_free_slot_type &&other) noexcept
        {
            if (this != &other)
            {
                if (manager)
                    manager->free_slot(index);
                manager = std::exchange(other.manager, nullptr);
                index = std::exchange(other.index, std::numeric_limits<slot_type>::max());
            }
            return *this;
        }
    };
};

constexpr static auto font_linear_sampler_key = "font_linear_sampler";
constexpr static auto font_nearest_neighbor_sampler_key = "font_nearest_neighbor_sampler";
constexpr static auto texture_ui_key = "white";
constexpr auto descriptorInit(auto &hardwareCtx)
{
    auto &physical_device = *hardwareCtx.physicalDevice.get();
    auto &surface = *hardwareCtx.surface.get();
    auto &device = *hardwareCtx.device.get();
    auto &GRAPHICS_AND_PRESENT = *hardwareCtx.queue.get();
    auto &commandPool = *hardwareCtx.commandPool.get();

    constexpr int MAX_TEXTURES = 64; // 预分配最大纹理槽位数（足够大）
    constexpr int SAMPLER_COUNT = 4; // 创建2个不同的采样器

    std::array<VkDescriptorBindingFlags, 4> bindingFlags = {
        // 绑定0：Uniform Buffer - 通常不需要绑定后更新
        0,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, //允许不连续占用纹理槽（如 1、3、5、33）
        0,
        0, // 对应 binding 3（Storage Buffer）
    };

    auto descriptorSetLayoutPtr = std::make_unique<DescriptorSetLayout>(
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
            .build(device));
    auto &descriptorSetLayout = *descriptorSetLayoutPtr.get();

    auto descriptorPoolPtr = std::make_unique<DescriptorPool>(
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
            .build(device));
    using descriptorSetsType = decltype(descriptorPoolPtr->allocateDescriptorSets(
        {.descriptorSets = std::vector<VkDescriptorSetLayout>{MAX_FRAMES_IN_FLIGHT,
                                                              *descriptorSetLayout}}));
    auto descriptorSetsPtr =
        std::make_unique<descriptorSetsType>(descriptorPoolPtr->allocateDescriptorSets(
            {.descriptorSets = std::vector<VkDescriptorSetLayout>{
                 MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout}}));
    auto &descriptorSets = *descriptorSetsPtr.get();

    // diff: [test_dod7] start: 不再是 vma 的内存
    auto uniformBuffersPtr = std::make_unique<
        std::array<mcs::vulkan::memory::auto_map_buffer, MAX_FRAMES_IN_FLIGHT>>();
    auto &uniformBuffers = *uniformBuffersPtr.get();
    {
        constexpr VkDeviceSize BUFFER_SIZE = sizeof(UniformBufferObject);
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
    auto textureManagerPtr =
        std::make_unique<resource_manager<mcs::vulkan::memory::resource, MAX_TEXTURES>>();
    auto &textureManager = *textureManagerPtr.get();
    {

        std::vector<mcs::vulkan::memory::resource> upload;
        std::vector<std::string> upload_keys;
        upload.reserve(4);
        upload.push_back(create_texture.templateForImage2d(
            device, commandPool, GRAPHICS_AND_PRESENT,
            raw_stbi_image{"textures/texture.jpg", STBI_rgb_alpha}, true));
        upload_keys.emplace_back("texture");
        uint32_t white = 0xFFFFFFFF;
        upload.push_back(
            create_texture.build(device, commandPool, GRAPHICS_AND_PRESENT,
                                 mcs::vulkan::memory::create_texture::image_info{
                                     .extent = {.width = 1, .height = 1},
                                     .pixels = std::span<const uint8_t>(
                                         reinterpret_cast<const uint8_t *>(&white), 4),
                                     .mipLevels = 1})); //创建纯白纹理
        upload_keys.emplace_back("white");

        upload.push_back(generateGradientTexture(device, commandPool,
                                                 GRAPHICS_AND_PRESENT, 2)); // 渐变
        upload_keys.emplace_back("gradient");
        upload.push_back(generateGradientTexture(device, commandPool,
                                                 GRAPHICS_AND_PRESENT, 1)); // 棋盘
        upload_keys.emplace_back("chessboard");

        for (auto [resource, index, key] :
             std::views::zip(upload, textureManager.view_free_indexes(), upload_keys))
        {
            textureManager.use_slot(index, std::move(resource), std::move(key));
        }
    }
    auto samplerManagerPtr = std::make_unique<resource_manager<Sampler, SAMPLER_COUNT>>();
    auto &samplerManager = *samplerManagerPtr.get();
    {
        std::vector<Sampler> samplers;
        std::vector<std::string> upload_keys;

        samplers.reserve(SAMPLER_COUNT);
        samplers.emplace_back(
            create_sampler{}
                .setCreateInfo(create_sampler::templateLinear())
                .enableAnisotropy(
                    device.physicalDevice()->getProperties().limits.maxSamplerAnisotropy)
                .updateMaxLodByMipmap(create_texture_mipLevels)
                .build(device)); // 线性采样器
        upload_keys.emplace_back("linear_sampler");
        samplers.emplace_back(create_sampler{}
                                  .setCreateInfo(create_sampler::templateNearest())
                                  .build(device)); // 最近邻采样器
        upload_keys.emplace_back("nearest_neighbor_sampler");

        // 采样器类型2：线性过滤，重复寻址，各向异性
        samplers.emplace_back(
            create_sampler{}
                .setCreateInfo(
                    {.pNext = {},
                     .flags = {},
                     .magFilter = VK_FILTER_LINEAR,
                     .minFilter = VK_FILTER_LINEAR,
                     .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                     .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                     .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                     .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                     .mipLodBias = 0,
                     .anisotropyEnable = VK_TRUE, // NOTE: 各向异性器件特性启用
                     .maxAnisotropy =
                         physical_device.getProperties().limits.maxSamplerAnisotropy,
                     .compareEnable = VK_FALSE,
                     .compareOp = VK_COMPARE_OP_ALWAYS,
                     .minLod = 0,
                     .maxLod =
                         VK_LOD_CLAMP_NONE, // NOTE: vulkan 内置最大值，就能适配一切mip
                     .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                     .unnormalizedCoordinates = VK_FALSE})
                .build(device));
        upload_keys.emplace_back(font_linear_sampler_key);

        // 采样器类型1：最近邻过滤，钳位到边缘
        samplers.emplace_back(
            create_sampler{}
                .setCreateInfo({.pNext = {},
                                .flags = {},
                                .magFilter = VK_FILTER_NEAREST,
                                .minFilter = VK_FILTER_NEAREST,
                                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                .mipLodBias = 0,
                                .anisotropyEnable = VK_FALSE, // NOTE: 无各向异性器
                                .maxAnisotropy = 1,
                                .compareEnable = VK_FALSE,
                                .compareOp = VK_COMPARE_OP_ALWAYS,
                                .minLod = 0,
                                .maxLod = VK_LOD_CLAMP_NONE,
                                .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                                .unnormalizedCoordinates = VK_FALSE})
                .build(device));
        upload_keys.emplace_back(font_nearest_neighbor_sampler_key);

        for (auto [resource, index, key] :
             std::views::zip(samplers, samplerManager.view_free_indexes(), upload_keys))
        {
            samplerManager.use_slot(index, std::move(resource), std::move(key));
        }
    }
    auto descriptorSetManager = make_aggregate<"descriptorSetManager",
                                               "update_uniform_buffer", "update_texture",
                                               "update_sampler">(
        [&device, &uniformBuffers, &descriptorSets]() {
            constexpr auto dstBinding = 0;
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                VkDescriptorBufferInfo uniformInfo{.buffer = uniformBuffers[i].buffer(),
                                                   .offset = 0,
                                                   .range = sizeof(UniformBufferObject)};
                VkWriteDescriptorSet writes{
                    .sType = sType<VkWriteDescriptorSet>(),
                    .dstSet = descriptorSets[i],
                    .dstBinding = dstBinding,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &uniformInfo,
                };
                device.updateDescriptorSets(1, &writes, 0, nullptr);
            }
        },
        [&device, &textureManager, &descriptorSets](auto used_indexes) {
            constexpr auto dstBinding = 1;
            std::vector<VkDescriptorImageInfo> imageInfos(MAX_TEXTURES);
            for (auto index : used_indexes)
            {
                auto &image = textureManager.resources[index];
                imageInfos[index] = VkDescriptorImageInfo{
                    .sampler = nullptr,
                    .imageView = image.imageView(),
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            }
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                std::vector<VkWriteDescriptorSet> writes;
                for (uint32_t slot : used_indexes)
                {
                    writes.push_back({
                        .sType = sType<VkWriteDescriptorSet>(),
                        .dstSet = descriptorSets[i],
                        .dstBinding = dstBinding,
                        .dstArrayElement = slot,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                        .pImageInfo =
                            &imageInfos[slot], // 安全：imageInfos 生命周期远长于此处
                    });
                }
                device.updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                            writes.data(), 0, nullptr);
            }
        },
        [&device, &samplerManager, &descriptorSets](auto used_indexes) {
            constexpr auto dstBinding = 2;
            std::vector<VkDescriptorImageInfo> samplerInfos(SAMPLER_COUNT);
            for (auto index : used_indexes)
            {
                auto &sampler = samplerManager.resources[index];
                samplerInfos[index] =
                    VkDescriptorImageInfo{.sampler = sampler.data(),
                                          .imageView = nullptr,
                                          .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
            }
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                std::vector<VkWriteDescriptorSet> writes;
                for (uint32_t slot : used_indexes)
                {
                    writes.push_back({
                        .sType = sType<VkWriteDescriptorSet>(),
                        .dstSet = descriptorSets[i],
                        .dstBinding = dstBinding,
                        .dstArrayElement = static_cast<uint32_t>(slot),
                        // NOTE: 上面是 shader的信息。下面是传输的信息
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                        .pImageInfo =
                            &samplerInfos[slot], // 安全：imageInfos 生命周期远长于此处
                    });
                }
                device.updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                            writes.data(), 0, nullptr);
            }
        });

    return make_aggregate<"descriptorCtx", "descriptorSetLayout", "descriptorPool",
                          "descriptorSets", "uniformBuffers", "textureManager",
                          "samplerManager", "descriptorSetManager">(
        std::move(descriptorSetLayoutPtr), std::move(descriptorPoolPtr),
        std::move(descriptorSetsPtr), std::move(uniformBuffersPtr),
        std::move(textureManagerPtr), std::move(samplerManagerPtr),
        std::move(descriptorSetManager));
}
constexpr auto initFont(auto &hardwareCtx, auto &descriptorCtx)
{
    auto &device = *hardwareCtx.device.get();
    auto &GRAPHICS_AND_PRESENT = *hardwareCtx.queue.get();
    auto &commandPool = *hardwareCtx.commandPool.get();

    auto &textureManager = *descriptorCtx.textureManager.get();
    auto &samplerManager = *descriptorCtx.samplerManager.get();

    //diff: [test_dod12] start: 加载字符纹理
    auto pre = std::string{MSDF_OUTPUT_DIR};
    const std::string TEXTURE_PATH_0 = pre + "/english_atlas.png";
    const std::string JSON_PATH_0 = pre + "/english_atlas.json";
    const std::string FONT_PATH_0 = pre + "/english_atlas.ttf";

    const std::string TEXTURE_PATH_1 = pre + "/msyh_chinese.png";
    const std::string JSON_PATH_1 = pre + "/msyh_chinese.json";
    const std::string FONT_PATH_1 = pre + "/msyh_chinese.ttc";

    const std::string TEXTURE_PATH_2 = pre + "/emoji.png";
    const std::string JSON_PATH_2 = pre + "/emoji.json";
    const std::string FONT_PATH_2 = pre + "/emoji.ttf";

    const std::string TEXTURE_PATH_3 = pre + "/arial_all.png";
    const std::string JSON_PATH_3 = pre + "/arial_all.json";
    const std::string FONT_PATH_3 = pre + "/arial_all.ttf";

    const std::string TEXTURE_PATH_4 = pre + "/missing_char.png";
    const std::string JSON_PATH_4 = pre + "/missing_char.json";
    const std::string FONT_PATH_4 = pre + "/missing_char.ttf";

    // 添加字体
    auto loaderPtr = std::make_unique<font::freetype_loader>();
    auto &loader = *loaderPtr.get();
    auto factory = font::make_font_factory(
        [&device, &commandPool, &GRAPHICS_AND_PRESENT, &textureManager,
         &samplerManager](font::FontInfo &info) {
            mcs::vulkan::memory::create_texture create_font_texture{
                [](mcs::vulkan::memory::create_texture::image_info imageInfo)
                    -> mcs::vulkan::memory::create_texture::create_info {
                    return {
                        .imageType = VK_IMAGE_TYPE_2D,
                        .format =
                            VK_FORMAT_R8G8B8A8_UNORM, //diff: [test_dod14] msdf是距离场。这个格式更好
                        .extent = {.width = imageInfo.extent.width,
                                   .height = imageInfo.extent.height,
                                   .depth = 1},
                        .mipLevels = imageInfo.mipLevels,
                        .arrayLayers = 1,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .tiling = VK_IMAGE_TILING_OPTIMAL,
                        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                 VK_IMAGE_USAGE_SAMPLED_BIT,
                        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
                },
                mcs::vulkan::memory::gen_memory_allocate_info(
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
                [](VkImageCreateInfo imageCreateInfo, VkImage image)
                    -> mcs::vulkan::memory::create_texture::view_create_info {
                    return {.image = image,
                            .viewType = VK_IMAGE_VIEW_TYPE_2D,
                            .format = imageCreateInfo.format,
                            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                 .baseMipLevel = 0,
                                                 .levelCount = imageCreateInfo.mipLevels,
                                                 .baseArrayLayer = 0,
                                                 .layerCount = 1}};
                }};

            using stbi_image_type = mcs::vulkan::font::texture_info::stbi_image_type;
            using texture_bind_sampler = mcs::vulkan::font::texture_bind_sampler;
            const auto &registration = info.registration;
            if (std::holds_alternative<stbi_image_type>(
                    registration.texture_info.image_variant))
            {
                auto sampler_index = samplerManager.find_slot_by_name(
                    info.registration.type == font::FontType::eMSDF
                        ? font_linear_sampler_key
                        : font_nearest_neighbor_sampler_key);
                if (not sampler_index)
                    throw std::logic_error{"couldn't find a suitable sampler_index"};

                using image_type = stbi_image_type::type;
                const auto &imageInfo =
                    std::get<stbi_image_type>(registration.texture_info.image_variant);
                auto image =
                    image_type{imageInfo.image_path.data(), imageInfo.image_format};
                auto texWidth = image.width();
                auto texHeight = image.height();
                auto imageSize = image.size();

                for (auto texture_index :
                     textureManager.view_free_indexes() | std::views::take(1))
                {
                    // NOTE: update: bind
                    info.registration.texture_info.bind = texture_bind_sampler{
                        .texture_index = static_cast<uint32_t>(texture_index),
                        .sampler_index = static_cast<uint32_t>(*sampler_index)};

                    textureManager.use_slot(
                        texture_index,
                        create_font_texture.build(
                            device, commandPool, GRAPHICS_AND_PRESENT,
                            mcs::vulkan::memory::create_texture::image_info{
                                .extent = {.width = static_cast<uint32_t>(texWidth),
                                           .height = static_cast<uint32_t>(texHeight)},
                                .pixels =
                                    std::span<const uint8_t>{
                                        image.data(), static_cast<uint64_t>(imageSize)},
                                .mipLevels = 1}),
                        imageInfo.image_path);
                    using ManagerType = std::remove_cvref_t<decltype(textureManager)>;
                    using auto_free_slot_type = typename ManagerType::auto_free_slot_type;
                    return auto_free_slot_type{textureManager, texture_index};
                }
                throw std::logic_error{"couldn't find a suitable texture_index"};
            }
            throw std::logic_error{"check image_variant"};
        },
        *loader);

    using factoryType = decltype(factory);
    auto font_factoryPtr = std::make_unique<factoryType>(std::move(factory));
    auto &fontFactory = *font_factoryPtr.get();
    auto fontSelect = font::GenFontSelector{&fontFactory, "zh-CN"}.load(
        font::font_register::makeFontInfos(
            loader, //
            {
                font::font_registration{
                    .font_path = FONT_PATH_0,
                    .json_path = JSON_PATH_0,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {}, //NOTE: lazy_bind
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_0}}},
                font::font_registration{
                    .font_path = FONT_PATH_1,
                    .json_path = JSON_PATH_1,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_1}}},
                font::font_registration{
                    .font_path = FONT_PATH_2,
                    .json_path = JSON_PATH_2,
                    .type = font::FontType::eBITMAP,
                    .texture_info = {.bind = {},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_2}}},
                font::font_registration{
                    .font_path = FONT_PATH_3,
                    .json_path = JSON_PATH_3,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_3}}},
                font::font_registration{
                    .font_path = FONT_PATH_4,
                    .json_path = JSON_PATH_4,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_4}}},
            }));
    fontSelect.initNotdefFont();
    assert(fontSelect.notdefFont() != nullptr);

    return make_aggregate<"fontCtx", "loader", "fontFactory", "fontSelect">(
        std::move(loaderPtr), std::move(font_factoryPtr), std::move(fontSelect));
}
constexpr auto inputInit(auto &swapchain)
{
    auto camera = [&]() {
        using namespace camera; // 你的 camera 命名空间
        // 视图：从 eye/center/up 构建 ViewMatrixObject
        glm::vec3 eye(0.0f, 0.0f, 2.0f);
        glm::vec3 center(0.0f, 0.0f, 0.0f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 forward = glm::normalize(center - eye);
        RightHandedView view;
        view.setPosition(eye).setOrientation(glm::quatLookAt(forward, up)); // 右手系

        // 投影：注意原 fovy 是弧度，这里要转成度数，因为构造函数接收度数
        float fovDeg = glm::degrees(glm::radians(45.0f)); // 就是 45.0f
        float aspect = swapchain.refImageExtent().width /
                       static_cast<float>(swapchain.refImageExtent().height);
        VulkanPerspectiveProjection proj(fovDeg, aspect, 0.1f, 10.0f);
        return GenCamera(RightHandedView::lookAt(glm::vec3(0, 0, 2), glm::vec3(0, 0, 0),
                                                 glm::vec3(0, 1, 0)),
                         std::move(proj));
    }();
    // 在创建 camera 之后，创建 uiCamera.与i开始是单位矩阵
    auto uiCamera = []() {
        using namespace camera;
        // 视图：相机位于原点，无旋转 → 视图矩阵 = I
        auto uiView = camera::RightHandedView{
            glm::vec3(0.0f, 0.0f, 0.0f), // 位置为原点
            glm::identity<glm::quat>()   // 无旋转
        };
        // 投影：正交范围 [-1,1] 且 near=0, far=1 → 投影矩阵 = I
        auto uiProj =
            camera::VulkanUIOrthographicProjection{-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f};
        return camera::GenCamera(uiView, uiProj);
    }();
    {
        auto v = uiCamera.getViewMatrix();
        auto p = uiCamera.getProjMatrix();

        std::println("=== UI Camera matrices ===");
        for (int i = 0; i < 4; ++i)
            std::println("V[{}]: {:8.6f} {:8.6f} {:8.6f} {:8.6f}", i, v[i][0], v[i][1],
                         v[i][2], v[i][3]);
        std::println("");
        for (int i = 0; i < 4; ++i)
            std::println("P[{}]: {:8.6f} {:8.6f} {:8.6f} {:8.6f}", i, p[i][0], p[i][1],
                         p[i][2], p[i][3]);
    }
    assert(uiCamera.getViewMatrix() == glm::mat4(1) &&
           uiCamera.getProjMatrix() == glm::mat4(1));
    auto input = std::make_unique<glfw_input>();
    return make_aggregate<"inputDataCtx", "input", "camera", "uiCamera", "clock">(
        std::move(input), std::move(camera), std::move(uiCamera), FrameClock{});
}
struct mesh_data
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
struct mesh_manager
{
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;
    std::unordered_map<std::string, mesh_data> meshMap;

    constexpr void addMesh(const std::string &name, const std::span<const Vertex> &verts,
                           const std::span<const uint32_t> &indices)
    {
        assert(not name.empty());
        assert(not verts.empty());
        assert(not indices.empty());
        uint32_t vOff = static_cast<uint32_t>(allVertices.size());
        uint32_t iOff = static_cast<uint32_t>(allIndices.size());
        allVertices.insert(allVertices.end(), verts.begin(), verts.end());
        allIndices.insert(allIndices.end(), indices.begin(), indices.end());
        meshMap[name] = {static_cast<uint32_t>(verts.size()), vOff, iOff,
                         static_cast<uint32_t>(indices.size())};
    }
};

// NOTE: 考虑放到一个命名空间或等区域统一处理
constexpr std::array<Vertex, 4> quadVerts = {
    Vertex{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}}, // 左上
    Vertex{{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},  // 右上
    Vertex{{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},   // 右下
    Vertex{{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}   // 左下
};
constexpr auto quadIdx = std::array<uint32_t, 6>{0, 1, 2, 0, 2, 3};
constexpr auto initMeshManager()
{
    mesh_manager m;
    m.addMesh("quad", std::span{quadVerts}, std::span{quadIdx});
    return m;
}

struct BufferResource
{
    mcs::vulkan::memory::auto_map_buffer buffer{};
    VkDeviceSize size{};
    constexpr auto write(size_t offset, const void *src, size_t size) noexcept
    {
        return ::memcpy(static_cast<char *>(buffer.mapPtr()) + offset, src, size);
    }
    BufferResource() = default;
    constexpr BufferResource(const LogicalDevice &device, VkDeviceSize capacity,
                             VkBufferUsageFlags usage, VkSharingMode sharingMode,
                             VkMemoryPropertyFlags properties)
        : buffer{mcs::vulkan::memory::auto_map_buffer(
              mcs::vulkan::memory::create_simple_buffer(
                  device, {.size = capacity, .usage = usage, .sharingMode = sharingMode},
                  properties),
              capacity)},
          size{capacity}
    {
    }
};
struct BufferResourceWithAddress
{
    mcs::vulkan::memory::auto_map_buffer buffer{};
    VkDeviceSize size{};
    VkDeviceAddress address{};

    constexpr auto write(size_t offset, const void *src, size_t size) noexcept
    {
        return ::memcpy(static_cast<char *>(buffer.mapPtr()) + offset, src, size);
    }

    BufferResourceWithAddress() = default;
    constexpr BufferResourceWithAddress(const LogicalDevice &device,
                                        VkDeviceSize capacity, VkBufferUsageFlags usage,
                                        VkSharingMode sharingMode,
                                        VkMemoryPropertyFlags properties)
        : buffer{mcs::vulkan::memory::auto_map_buffer(
              mcs::vulkan::memory::create_simple_buffer(
                  device, {.size = capacity, .usage = usage, .sharingMode = sharingMode},
                  properties),
              capacity)},
          size{capacity},
          address{device.getBufferDeviceAddress(
              {.sType = sType<VkBufferDeviceAddressInfo>(), .buffer = buffer.buffer()})}
    {
    }
};
struct FrameResources
{
    BufferResourceWithAddress globalVertexBuffer{};

    BufferResource globalIndexBuffer{};

    BufferResourceWithAddress globalInstanceBuffer{};

    BufferResourceWithAddress globalAttributeBuffer{};

    BufferResource indirectDrawBuffer{};

    // 新增：命令常量缓冲区
    BufferResourceWithAddress commandConstantsBuffer{};

    uint32_t drawCount3D = 0;
    uint32_t drawCountUI = 0;
};

constexpr uint32_t MAX_INSTANCE_COUNT = 1000;
constexpr uint32_t MAX_DRAW_CALLS = 10;
constexpr auto initFrameResources(const LogicalDevice &device, const mesh_manager &m)
{
    auto &[allVertices, allIndices, meshMap] = m;
    std::array<FrameResources, MAX_FRAMES_IN_FLIGHT> frameResources;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        auto &batche = frameResources[i];
        {
            auto capacity = allVertices.size() * sizeof(Vertex);
            batche.globalVertexBuffer =
                BufferResourceWithAddress{device, capacity,
                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                          VK_SHARING_MODE_EXCLUSIVE,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
            batche.globalVertexBuffer.write(0, allVertices.data(), capacity);
        }
        {
            auto capacity = allIndices.size() * sizeof(uint32_t);
            batche.globalIndexBuffer =
                BufferResource{device, capacity, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               VK_SHARING_MODE_EXCLUSIVE,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
            batche.globalIndexBuffer.write(0, allIndices.data(), capacity);
        }
        {
            constexpr VkDeviceSize MAX_INSTANCE_DATA_SIZE =
                MAX_DRAW_CALLS * MAX_INSTANCE_COUNT * sizeof(InstanceData);
            batche.globalInstanceBuffer =
                BufferResourceWithAddress{device, MAX_INSTANCE_DATA_SIZE,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                          VK_SHARING_MODE_EXCLUSIVE,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

            // NOTE: 绘制的时候，每帧填充
        }
        {
            constexpr VkDeviceSize MAX_Attribute_SIZE = 1 * 1024 * 1024;
            batche.globalAttributeBuffer =
                BufferResourceWithAddress{device, MAX_Attribute_SIZE,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                          VK_SHARING_MODE_EXCLUSIVE,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        }
        {

            constexpr VkDeviceSize MAX_IndirectDraw_SIZE = 1 * 1024 * 1024;
            batche.indirectDrawBuffer = BufferResource{
                device, MAX_IndirectDraw_SIZE, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        }
        {
            // 新增 command constants buffer
            constexpr VkDeviceSize MAX_CMD_CONST_SIZE =
                MAX_DRAW_CALLS * sizeof(CommandConstant);
            batche.commandConstantsBuffer =
                BufferResourceWithAddress{device, MAX_CMD_CONST_SIZE,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                          VK_SHARING_MODE_EXCLUSIVE,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        }
    }
    return frameResources;
}
constexpr auto run_text_pipeline(auto &fontSelect, const char8_t *rawText,
                                 std::string_view langBcp47, bool ltr = true)
{
    constexpr auto ltr_value = 0; // NOLINT
    constexpr auto rtl_value = 1; // NOLINT
    namespace font_ns = mcs::vulkan::font;

    auto norm = font_ns::utf8proc::normalize(rawText);
    const std::vector<uint32_t> &codepoints = norm.codepoints;
    font_ns::utf8proc::print_normalized(norm, rawText);

    auto analyze_result = font_ns::bidi::analyze(codepoints, ltr ? ltr_value : rtl_value);
    font_ns::bidi::print_bidi_result(codepoints, analyze_result);

    auto test_text_runs = font_ns::assign_fonts(analyze_result, fontSelect);
    font_ns::print_text_runs(test_text_runs);

    auto test_shape_result = font_ns::harfbuzz::shape(
        analyze_result.mirrored_codepoints, test_text_runs, fontSelect.notdefFont());
    font_ns::harfbuzz::print_shape_result(analyze_result.mirrored_codepoints,
                                          test_shape_result);

    auto break_result = font_ns::libunibreak::analyze_line_breaks(
        analyze_result.mirrored_codepoints, langBcp47);
    font_ns::libunibreak::print_break_result(break_result,
                                             analyze_result.mirrored_codepoints);
    return make_aggregate<"text_pipeline_result", "codepoints", "shape_result",
                          "break_result">(std::move(analyze_result.mirrored_codepoints),
                                          std::move(test_shape_result),
                                          std::move(break_result));
}
constexpr auto initPipeline(auto &hardwareCtx, auto &descriptorCtx)
{
    auto &physical_device = *hardwareCtx.physicalDevice.get();
    auto &device = *hardwareCtx.device.get();

    auto &swapchainBuild = hardwareCtx.swapchainBuild;
    auto &swapchain = hardwareCtx.swapchain;

    auto &descriptorSetLayout = *descriptorCtx.descriptorSetLayout.get();

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
    auto swapchainAttachments =
        make_aggregate<"swapchainAttachments", "depthResourcesBuild", "depthResource",
                       "msaaResourcesBuild", "msaaResource">(
            std::move(depthResourcesBuild), std::move(depthResource),
            std::move(msaaResourcesBuild), std::move(msaaResource));

    // ========== 拾取资源 =========
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
    std::array<mcs::vulkan::memory::auto_map_buffer, MAX_FRAMES_IN_FLIGHT> pickingFrames;
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
    struct pick_mouse
    {
        glm::ivec2 pos{0, 0};
        bool valid = false;
    };
    pick_mouse pickMouse;
    auto pickingAttachments =
        make_aggregate<"pickingAttachments", "pickResourcesBuild", "pickResource",
                       "resolveResourcesBuild", "resolveResource", "pickingFrames",
                       "pickMouse">(
            std::move(pickResourcesBuild), std::move(pickResource),
            std::move(resolveResourcesBuild), std::move(resolveResource),
            std::move(pickingFrames), std::move(pickMouse));

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
    // 定义两个颜色附件格式
    std::array<VkFormat, 2> mainColorFormats = {
        swapchainBuild.refImageFormat(), // location 0 (swapchain)
        VK_FORMAT_R32G32_UINT            // location 1 (picking)
    };

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
                 .colorBlendState =
                     {.logicOpEnable = VK_FALSE,
                      .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                      .attachments =
                          {
                              {
                                  //diff: [test_dod12] start: 附件 0 – 开启混合，支持 MSDF 与普通纹理混合
                                  .blendEnable = VK_TRUE,
                                  .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                                  .dstColorBlendFactor =
                                      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                  .colorBlendOp = VK_BLEND_OP_ADD,
                                  .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                  .dstAlphaBlendFactor =
                                      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                  .alphaBlendOp = VK_BLEND_OP_ADD,
                                  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                    VK_COLOR_COMPONENT_G_BIT |
                                                    VK_COLOR_COMPONENT_B_BIT |
                                                    VK_COLOR_COMPONENT_A_BIT,
                                  //diff: [test_dod12] end
                              },
                              //diff: [test_indirectdraw_no_pick] start
                              {
                                  // location 1 (R32G32_UINT，不能混合)
                                  .blendEnable = VK_FALSE,
                                  .colorWriteMask =
                                      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT,
                              },
                              //diff: [test_indirectdraw_no_pick] end
                          }},
                 .dynamicState =
                     {.dynamicStates =
                          {
                              VK_DYNAMIC_STATE_VIEWPORT,
                              VK_DYNAMIC_STATE_SCISSOR,
                              // diff: [test_dod10] start: UI的 z 是一样的哦。 所以前面的会被丢弃。我们不能让UI的z相同就丢弃
                              VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                              VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                              // diff: [test_dod10] end
                          }},
                 .layout = *pipelineLayout})
            .build(device);
    return make_aggregate<"mainPipelineCtx", "pipelineLayout", "graphicsPipeline",
                          "swapchainAttachments", "pickingAttachments">(
        std::move(pipelineLayout), std::move(graphicsPipeline),
        std::move(swapchainAttachments), std::move(pickingAttachments));
}

constexpr auto autoSpinStore_type_id = 0;
constexpr auto interactive_type_id = 1;
constexpr auto uiStore_type_id = 2;

struct triat_for_autoSpin
{
    static constexpr auto inputController(auto &world, auto &inputCtx, auto &soaCtx)
    {

        // 每1秒添加一批实体，直到总数达到1000
        {
            auto &autoSpinStore = soaCtx.autoSpinStore;
            constexpr int TARGET_ENTITIES = 1000;
            constexpr int BATCH_SIZE = 100;

            if (autoSpinStore.size() < TARGET_ENTITIES)
            {
                static auto lastAddTime = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                          lastAddTime)
                        .count() >= 1000)
                {
                    lastAddTime = now;
                    int toAdd =
                        std::min(BATCH_SIZE, TARGET_ENTITIES -
                                                 static_cast<int>(autoSpinStore.size()));

                    if (toAdd > autoSpinStore.free_size())
                        autoSpinStore.expansion_size(toAdd - autoSpinStore.free_size());

                    std::mt19937 rng(std::random_device{}());
                    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
                    std::uniform_real_distribution<float> posDist(-1.5f,
                                                                  1.5f); // 散开范围
                    std::uniform_real_distribution<float> scaleDist(0.01f,
                                                                    0.05f); // 粒子极小
                    // 可选：随机使用不同纹理和采样器
                    std::uniform_int_distribution<int> texDist(0, 1);
                    std::uniform_int_distribution<int> sampDist(0, 1);

                    for (uint32_t i = 0; i < toAdd; ++i)
                    {
                        float scale = scaleDist(rng);
                        glm::vec3 pos(posDist(rng), posDist(rng), 0.0f);

                        // 构建模型矩阵：先缩放，后平移（重要！否则位置会被缩放影响）
                        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) *
                                          glm::scale(glm::mat4(1.0f), glm::vec3(scale));
                        // 若需要粒子旋转（不关心朝向，可加随机旋转）
                        // model = model * glm::toMat4(glm::angleAxis(posDist(rng), glm::vec3(0,0,1)));

                        uint32_t texIdx = texDist(rng) ? 3 : 0;   // 纹理0或3（棋盘/渐变）
                        uint32_t sampIdx = sampDist(rng) ? 1 : 0; // 采样器0或1

                        InstanceData instData{.objectId = {autoSpinStore_type_id,
                                                           autoSpinStore.nextEntityId()},
                                              .textureIndex = texIdx,
                                              .samplerIndex = sampIdx,
                                              .objectData = object_data{model}};

                        std::array<VertexAttribute, 4> attrs;
                        for (auto &attr : attrs)
                            attr.color =
                                glm::vec3(colorDist(rng), colorDist(rng), colorDist(rng));

                        autoSpinStore.new_entity(instData, attrs);
                    }
                }
            }
        }
    }

    static constexpr auto on_hover(auto &world, auto &inputCtx, auto &soaCtx,
                                   picking_result result)
    {

        uint32_t type = result.key.object_type;
        uint32_t idx = result.key.entity_index;
        assert(type == autoSpinStore_type_id);
        auto &store = soaCtx.autoSpinStore;
        if (store.alive(idx))
        {
            auto [inst] = store.template view_entity<"instanceData">(0, idx);
            std::cout << "[AutoSpin] entity " << idx
                      << " textureIndex=" << inst.textureIndex
                      << " samplerIndex=" << inst.samplerIndex << "\n";
        }
        else
        {
            std::cout << "[AutoSpin] entity " << idx << " is dead\n";
        }
    }
};
struct triat_for_interactive
{
    static constexpr auto on_hover(auto &world, auto &inputCtx, auto &soaCtx,
                                   picking_result result)
    {
        uint32_t type = result.key.object_type;
        uint32_t idx = result.key.entity_index;
        assert(type == interactive_type_id);
        auto &store = soaCtx.interactiveStore;
        if (store.alive(idx))
        {
            auto [inst, state] =
                store.template view_entity<"instanceData", "modelState">(0, idx);
            std::cout << "[Interactive] entity " << idx << " pos=("
                      << state.model_matrix.translation.x << ","
                      << state.model_matrix.translation.y << ","
                      << state.model_matrix.translation.z << ")\n";
        }
    }
};
struct triat_for_ui
{
    static constexpr auto on_hover(auto &world, auto &inputCtx, auto &soaCtx,
                                   picking_result result)
    {
        uint32_t type = result.key.object_type;
        uint32_t idx = result.key.entity_index;
        assert(type == uiStore_type_id);
        auto &store = soaCtx.uiStore;
        if (store.alive(idx))
        {
            auto [inst] = store.template view_entity<"instanceData">(0, idx);
            std::cout << "[UI] entity " << idx << " fontType=" << inst.fontType << "\n";
        }
    }
};

using auto_spin_data_type =
    gen_soa_struct<triat_for_autoSpin, {"instanceData", ^^InstanceData},
                   {"vertexAttribute", ^^std::array<VertexAttribute, 4>}>;
struct model_state
{
    mcs::vulkan::event::position2d_event last_pos{};
    bool is_middle_button_pressed = false;
    bool is_left_button_pressed = false;
    bool is_right_button_pressed_last = false; // 新增：记录上一帧右键状态
    mcs::vulkan::event::position2d_event last_left_pos{};
    model_matrix model_matrix{};
};
using interactive_type =
    gen_soa_struct<triat_for_interactive, {"instanceData", ^^InstanceData},
                   {"vertexAttribute", ^^std::array<VertexAttribute, 4>},
                   {"modelState", ^^model_state}>;
using ui_data_type =
    gen_soa_struct<triat_for_ui, {"instanceData", ^^InstanceData},
                   {"vertexAttribute", ^^std::array<VertexAttribute, 4>}>;

static constexpr auto on_hover(auto &world, auto &inputCtx, auto &soaCtx,
                               picking_result result)
{
    constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
    template for (constexpr auto e : std::ranges::views::indices(members.size()))
    {
        if constexpr (requires {
                          typename std::remove_cvref_t<
                              decltype(soaCtx.[:members[e]:])>::trait_type;
                          std::remove_cvref_t<decltype(soaCtx.[:members[e]:])>::
                              trait_type::template on_hover(world, inputCtx, soaCtx,
                                                            result);
                      })
            if (result.key.object_type == e)
            {
                using trait_type =
                    std::remove_cvref_t<decltype(soaCtx.[:members[e]:])>::trait_type;
                trait_type::template on_hover(world, inputCtx, soaCtx, result);
            }
    }
}
static constexpr auto inputController(auto &world, auto &inputCtx, auto &soaCtx)
{
    constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
    template for (constexpr auto e : std::ranges::views::indices(members.size()))
    {
        if constexpr (requires {
                          typename std::remove_cvref_t<
                              decltype(soaCtx.[:members[e]:])>::trait_type;
                          std::remove_cvref_t<decltype(soaCtx.[:members[e]:])>::
                              trait_type::template inputController(world, inputCtx,
                                                                   soaCtx);
                      })
        {
            using trait_type =
                std::remove_cvref_t<decltype(soaCtx.[:members[e]:])>::trait_type;
            trait_type::template inputController(world, inputCtx, soaCtx);
        }
    }
}

int main()
try
{
#ifdef VERT_SHADER_PATH
    std::cout << "VERT_SHADER_PATH: " << VERT_SHADER_PATH << '\n';
#endif
#ifdef FRAG_SHADER_PATH
    std::cout << "FRAG_SHADER_PATH: " << FRAG_SHADER_PATH << '\n';
#endif
    auto hardwareCtx = init();
    auto &window = *hardwareCtx.window.get();
    auto &physical_device = *hardwareCtx.physicalDevice.get();
    auto &surface = *hardwareCtx.surface.get();
    auto &device = *hardwareCtx.device.get();
    auto &GRAPHICS_AND_PRESENT = *hardwareCtx.queue.get();
    auto &commandPool = *hardwareCtx.commandPool.get();
    auto &commandBuffers = *hardwareCtx.commandBuffers.get();

    auto &swapchainBuild = hardwareCtx.swapchainBuild;
    auto &swapchain = hardwareCtx.swapchain;
    auto &frameContext = hardwareCtx.frameContext;

    auto descriptorCtx = descriptorInit(hardwareCtx);
    auto &descriptorSetLayout = *descriptorCtx.descriptorSetLayout.get();
    auto &descriptorPool = *descriptorCtx.descriptorPool.get();
    auto &descriptorSets = *descriptorCtx.descriptorSets.get();
    auto &uniformBuffers = *descriptorCtx.uniformBuffers.get();
    auto &textureManager = *descriptorCtx.textureManager.get();
    auto &samplerManager = *descriptorCtx.samplerManager.get();

    auto &descriptorSetManager = descriptorCtx.descriptorSetManager;

    static uint32_t UITextureIndex =
        textureManager.find_slot_by_name(texture_ui_key).value();

    auto fontCtx = initFont(hardwareCtx, descriptorCtx);
    auto &fontFactory = *fontCtx.fontFactory.get();
    auto &fontSelect = fontCtx.fontSelect;
    using FontContext = std::remove_cvref_t<decltype(fontFactory)>::font_context_type;

    descriptorSetManager.update_uniform_buffer();
    descriptorSetManager.update_texture(textureManager.view_used_indexes());
    descriptorSetManager.update_sampler(samplerManager.view_used_indexes());

    auto inputDataCtx = inputInit(swapchain);
    auto &input = *inputDataCtx.input.get();
    auto &camera = inputDataCtx.camera;
    auto &uiCamera = inputDataCtx.uiCamera;
    auto &clock = inputDataCtx.clock;

    //diff: [test_dod12] end

    auto meshManager = initMeshManager();
    auto &[allVertices, allIndices, meshMap] = meshManager;
    auto frameResources = initFrameResources(device, meshManager);

    auto mainPipelineCtx = initPipeline(hardwareCtx, descriptorCtx);
    auto &[pipelineLayout, graphicsPipeline, swapchainAttachments, pickingAttachments] =
        mainPipelineCtx;
    auto &[depthResourcesBuild, depthResource, msaaResourcesBuild, msaaResource] =
        swapchainAttachments;
    auto &[pickResourcesBuild, pickResource, resolveResourcesBuild, resolveResource,
           pickingFrames, pickMouse] = pickingAttachments;

    // diff: [test_dod2] start 定义全部数据

    // diff: [test_dod2] end

    //diff: [test_dod8.cpp] start

    auto_spin_data_type autoSpinStore{1};

    interactive_type interactiveStore{1};

    //diff: [test_dod10] start

    // 新增 UI store（结构与其他 store 完全一致）
    ui_data_type uiStore{100};

    using namespace mcs::vulkan::yoga::literals;
    using namespace mcs::vulkan::yoga;
    Node screen{
        "screen",
        Config{YGConfigNew()}.setUseWebDefaults(false).setPointScaleFactor(
            window.getContentScale().xscale),
        Style{.width = {Pixels{WIDTH}}, .height = {Pixels{HEIGHT}}},
        Node{
            "root",
            Style{
                .padding = {10_px},
                .width = {200_px},
                .height = {200_px},
            },
            Node{"child0", Style{.margin = {5_px}, .width = {100_px}, .height = {100_px}},
                 Node{"child0-0",
                      Style{.margin = {5_px}, .width = {50_px}, .height = {50_px}}}},
            Node{"1", Style{.margin = {5_px}, .width = {100_px}, .height = {20_px}}},
        },
        Node{
            "root2",
            Style{
                .padding = {10_px},
                .width = {200_px},
                .height = {200_px},
            },
            Node{"1", Style{.margin = {5_px}, .width = {100_px}, .height = {20_px}}},
        },
        Node{"text_display", Style{
                                 .padding = {10_px},
                                 .width = {200_px},
                                 .height = {200_px},
                             }}};
    window.enableContentScaleCallback([&](void *self, float xscale, float yscale) {
        MCSLOG_INFO("glfw change ContentScaleCallback");
        screen.refConfig().setPointScaleFactor(xscale);
    });

    Layout layout{screen};
    auto on_resize = [&](Layout &layout, layout_size size) {
        layout.root()->refStyle().width = Pixels{size.available_width};
        layout.root()->refStyle().height = Pixels{size.available_height};
        auto &child0 = screen.childrens()[0];
        auto &child1 = screen.childrens()[1];

        constexpr auto test_insert = true;
        if constexpr (not test_insert)
        {
            if (!child0->childrens().empty())
                child1->emplace_back(child0->removeChild(0));
            else
                child0->emplace_back(child1->removeChild(0));
        }
        else
        {
            if (child0->childrens().size() == 2)
                child1->insert(child0->removeChild(0), 0);
            else
                child0->insert(child1->removeChild(0), 0);
        }
        layout.update();
    };
    layout.calculate({.available_width = WIDTH, .available_height = HEIGHT});
    std::println("layout.print [start]: ContentScale: [{},{}]",
                 window.getContentScale().xscale, window.getContentScale().yscale);
    layout.print();
    std::println("layout.print [end]");

    // ============================================================
    // 1. 纯几何数据结构（从 Yoga/Node 提取出来的唯一产物）
    // ============================================================
    struct NodeGeometry
    {
        float x, y, w, h;
        float pad_left, pad_right, pad_top, pad_bottom;
        std::string node_id;
        uint32_t traversal_id; // 遍历顺序编号，用于颜色等
    };

    // ============================================================
    // 2. 提取函数 —— 唯一接触 Node / Yoga 的地方
    // ============================================================
    static constexpr auto extract_geometries = [](const Layout &layout) {
        constexpr auto count_nodes = [](const Node *root) noexcept {
            size_t cnt = 0;
            auto dfs = [&](this auto &self, const Node *node) {
                if (!node)
                    return;
                ++cnt;
                for (const auto &child : node->childrens())
                    self(child.get());
            };
            dfs(root);
            return cnt;
        };

        std::vector<NodeGeometry> geos;
        geos.reserve(count_nodes(layout.root()));
        uint32_t id = 0;
        auto dfs = [&](this auto &self, const Node *node, float px, float py) {
            if (!node)
                return;
            YGNodeRef yg = **node;
            float x = px + YGNodeLayoutGetLeft(yg);
            float y = py + YGNodeLayoutGetTop(yg);
            float w = YGNodeLayoutGetWidth(yg);
            float h = YGNodeLayoutGetHeight(yg);
            if (w > 0 && h > 0)
            {
                NodeGeometry geo;
                geo.x = x;
                geo.y = y;
                geo.w = w;
                geo.h = h;
                auto getPad = [&](YGEdge e) {
                    auto v = YGNodeStyleGetPadding(yg, e);
                    return v.unit != YGUnitUndefined ? v.value : 0.0f;
                };
                geo.pad_left = getPad(YGEdgeLeft);
                geo.pad_right = getPad(YGEdgeRight);
                geo.pad_top = getPad(YGEdgeTop);
                geo.pad_bottom = getPad(YGEdgeBottom);
                geo.node_id = node->id();
                geo.traversal_id = id++;
                geos.push_back(geo);
            }
            for (const auto &child : node->childrens())
                self(child.get(), x, y);
        };
        dfs(layout.root(), 0.0f, 0.0f);
        return geos;
    };

    // ============================================================
    // 3. 文本实例生成（纯函数，只依赖 NodeGeometry + 文本数据）
    // ============================================================
    static constexpr auto generate_text_instances =
        [](float w, float h, ui_data_type &uiStore, const NodeGeometry &geo,
           const std::vector<uint32_t> &codepoints, const auto &shape_results,
           const auto &break_types, uint32_t modulateFlag = 1) {
            // -------- 1. 排版参数 --------
            float x = geo.x + geo.pad_left;
            float y = geo.y + geo.pad_top;
            float contentWidth = geo.w - geo.pad_left - geo.pad_right;
            float contentHeight = geo.h - geo.pad_top - geo.pad_bottom;

            const double FONT_SIZE_PX = std::min<double>(contentHeight, 12.0);
            const double LINE_HEIGHT_PX = FONT_SIZE_PX * 1.5;
            const double ORIGIN_X = static_cast<double>(x);
            const double ORIGIN_Y = static_cast<double>(y) + FONT_SIZE_PX;
            const double MAX_LINE_WIDTH = static_cast<double>(contentWidth);

            // -------- 2. 收集字形与 advance --------
            using GlyphType = std::remove_cvref_t<decltype(shape_results[0][0])>;
            std::vector<const GlyphType *> visual_glyphs;
            visual_glyphs.reserve([&] noexcept {
                size_t total_glyphs = 0;
                for (const auto &run : shape_results)
                    total_glyphs += run.size();
                return total_glyphs;
            }());
            std::vector<double> char_advance(codepoints.size(), 0.0);

            for (const auto &run : shape_results)
            {
                for (const auto &glyph : run)
                {
                    visual_glyphs.push_back(&glyph);
                    char_advance[glyph.logical_idx] += glyph.advance_x * FONT_SIZE_PX;
                }
            }

            // -------- 3. 分行 --------
            std::vector<int> line_of_logical(codepoints.size(), -1);
            int current_line = 0;
            double line_width = 0.0;
            bool line_has_content = false;

            for (size_t i = 0; i < break_types.size(); ++i)
            {
                if (break_types[i] == LINEBREAK_MUSTBREAK)
                {
                    if (line_has_content)
                    {
                        ++current_line;
                        line_width = 0.0;
                        line_has_content = false;
                    }
                    continue;
                }
                double adv = char_advance[i];
                if (adv == 0.0)
                    continue;

                if (line_has_content && (line_width + adv) > MAX_LINE_WIDTH)
                {
                    ++current_line;
                    line_width = 0.0;
                    line_has_content = false;
                }
                line_of_logical[i] = current_line;
                line_width += adv;
                line_has_content = true;
            }

            // -------- 4. 生成字形实例 --------
            const float ndc_scale_x = 2.0f / w;
            const float ndc_scale_y = 2.0f / h;

            double currentY = ORIGIN_Y;
            double cursorX = ORIGIN_X;
            int active_line = 0;

            for (const auto *g : visual_glyphs)
            {
                int target_line = line_of_logical[g->logical_idx];
                if (target_line < 0)
                    continue;

                if (target_line != active_line)
                {
                    currentY = ORIGIN_Y + LINE_HEIGHT_PX * target_line;
                    cursorX = ORIGIN_X;
                    active_line = target_line;
                }

                using bound_type = decltype(g->plane_bounds);
                if (g->plane_bounds == bound_type{})
                {
                    cursorX += g->advance_x * FONT_SIZE_PX;
                    continue;
                }

                double baselineY = currentY + g->offset_y * FONT_SIZE_PX;
                float left =
                    static_cast<float>(cursorX + g->plane_bounds.left * FONT_SIZE_PX);
                float bottom =
                    static_cast<float>(baselineY + g->plane_bounds.bottom * FONT_SIZE_PX);
                float right =
                    static_cast<float>(cursorX + g->plane_bounds.right * FONT_SIZE_PX);
                float top =
                    static_cast<float>(baselineY + g->plane_bounds.top * FONT_SIZE_PX);

                glm::vec2 p0(left * ndc_scale_x - 1.0f, top * ndc_scale_y - 1.0f);
                glm::vec2 p2(right * ndc_scale_x - 1.0f, bottom * ndc_scale_y - 1.0f);
                glm::mat4 M = camera::VulkanNDCConfig::rectTransform(p0, p2, 0.0f);

                UvTransform uv = UvTransform::from_target_verts(
                    std::array<glm::vec2, 4>{{{static_cast<float>(g->uv_bounds.left),
                                               static_cast<float>(g->uv_bounds.bottom)},
                                              {static_cast<float>(g->uv_bounds.right),
                                               static_cast<float>(g->uv_bounds.bottom)},
                                              {static_cast<float>(g->uv_bounds.right),
                                               static_cast<float>(g->uv_bounds.top)},
                                              {static_cast<float>(g->uv_bounds.left),
                                               static_cast<float>(g->uv_bounds.top)}}});

                InstanceData inst{
                    .objectId = {uiStore_type_id, uiStore.nextEntityId()},
                    .textureIndex = g->font_ctx->bind.texture_index,
                    .samplerIndex = g->font_ctx->bind.sampler_index,
                    .objectData = object_data{glm::mat4(1.0f)},
                    .vertexTransform = VertexTransform{M},
                    .uvTransform = uv,
                    .fontType = static_cast<uint32_t>(g->font_ctx->type),
                    .pxRange = static_cast<float>(
                        g->font_ctx->font.atlas.distanceRange.value_or(0.0)),
                    .modulateFlag = modulateFlag};

                std::array<VertexAttribute, 4> attrs = {
                    VertexAttribute{{1.0f, 1.0f, 1.0f}},
                    VertexAttribute{{1.0f, 0.0f, 0.0f}},
                    VertexAttribute{{0.0f, 1.0f, 0.0f}},
                    VertexAttribute{{0.0f, 0.0f, 1.0f}}};

                uiStore.new_entity(inst, attrs);
                cursorX += g->advance_x * FONT_SIZE_PX;
            }
        };
    // ============================================================
    // 4. 矩形实例生成（单个节点）
    // ============================================================
    static constexpr auto generate_rect_instance = [](float w, float h,
                                                      ui_data_type &uiStore,
                                                      const NodeGeometry &geo) {
        glm::vec2 p0 = camera::VulkanNDCConfig::screenToNDC(geo.x, geo.y, w, h);
        glm::vec2 p2 =
            camera::VulkanNDCConfig::screenToNDC(geo.x + geo.w, geo.y + geo.h, w, h);
        glm::mat4 M = camera::VulkanNDCConfig::rectTransform(p0, p2, 0.0f);
        InstanceData inst{.objectId = {uiStore_type_id, uiStore.nextEntityId()},
                          .textureIndex = UITextureIndex,
                          .samplerIndex = 0,
                          .objectData = object_data{glm::mat4(1.0f)},
                          .vertexTransform = VertexTransform{M},
                          .uvTransform = UvTransform::identity()};

        // 8 组颜色表（与原始代码完全一致）
        static const std::array<std::array<glm::vec3, 4>, 8> instanceColors = {
            {{{{1.0f, 0.0f, 0.0f},
               {1.0f, 0.5f, 0.0f},
               {1.0f, 0.0f, 0.5f},
               {1.0f, 0.5f, 0.2f}}},
             {{{0.0f, 1.0f, 0.0f},
               {0.5f, 1.0f, 0.0f},
               {0.0f, 1.0f, 0.5f},
               {0.2f, 1.0f, 0.2f}}},
             {{{0.0f, 0.0f, 1.0f},
               {0.0f, 0.5f, 1.0f},
               {0.5f, 0.0f, 1.0f},
               {0.2f, 0.5f, 1.0f}}},
             {{{1.0f, 1.0f, 0.0f},
               {1.0f, 0.8f, 0.0f},
               {0.8f, 1.0f, 0.0f},
               {1.0f, 1.0f, 0.3f}}},
             {{{1.0f, 0.0f, 1.0f},
               {1.0f, 0.5f, 0.5f},
               {0.5f, 0.0f, 1.0f},
               {0.8f, 0.2f, 0.8f}}},
             {{{0.0f, 1.0f, 1.0f},
               {0.0f, 0.8f, 0.8f},
               {0.5f, 1.0f, 1.0f},
               {0.2f, 0.8f, 0.8f}}},
             {{{1.0f, 0.5f, 0.0f},
               {1.0f, 0.3f, 0.0f},
               {0.8f, 0.4f, 0.0f},
               {1.0f, 0.6f, 0.2f}}},
             {{{0.5f, 0.0f, 0.5f},
               {0.6f, 0.2f, 0.6f},
               {0.3f, 0.0f, 0.5f},
               {0.7f, 0.3f, 0.7f}}}}};

        size_t colorIdx = geo.traversal_id % 8;
        std::array<VertexAttribute, 4> attrs{
            {VertexAttribute{instanceColors[colorIdx][0]},
             VertexAttribute{instanceColors[colorIdx][1]},
             VertexAttribute{instanceColors[colorIdx][2]},
             VertexAttribute{instanceColors[colorIdx][3]}}};

        // 保留调试打印（与原始逻辑相同）
        auto cur_id = inst.objectId.entity_index;
        std::println("uiStore add id: {}", cur_id);
        if (cur_id == 0)
        {
            auto printMat4 = [](const glm::mat4 &m) {
                std::println("[{}, {}, {}, {}]", m[0][0], m[0][1], m[0][2], m[0][3]);
                std::println("[{}, {}, {}, {}]", m[1][0], m[1][1], m[1][2], m[1][3]);
                std::println("[{}, {}, {}, {}]", m[2][0], m[2][1], m[2][2], m[2][3]);
                std::println("[{}, {}, {}, {}]", m[3][0], m[3][1], m[3][2], m[3][3]);
            };
            std::println("screen: w={}, h={}", w, h);
            std::println(
                "p0=({},{}), p1=({},{}), p2=({},{}), p3=({},{})", p0.x, p0.y,
                camera::VulkanNDCConfig::screenToNDC(geo.x + geo.w, geo.y, w, h).x,
                camera::VulkanNDCConfig::screenToNDC(geo.x + geo.w, geo.y, w, h).y, p2.x,
                p2.y, camera::VulkanNDCConfig::screenToNDC(geo.x, geo.y + geo.h, w, h).x,
                camera::VulkanNDCConfig::screenToNDC(geo.x, geo.y + geo.h, w, h).y);
            std::println("matrix:");
            printMat4(M);
        }

        uiStore.new_entity(inst, attrs);
    };

    // ============================================================
    // 5. 重构后的 generateUIInstances
    // ============================================================
    auto generateUIInstances = [&swapchain, &uiStore, &fontSelect](Layout &layout) {
        float w = static_cast<float>(swapchain.refImageExtent().width);
        float h = static_cast<float>(swapchain.refImageExtent().height);

        // 注意：原始代码只在遇到 text_display 节点时才会调用 run_text_pipeline，
        // 但我们可以提前准备，如果不存在文本节点则不调用。为简单起见，这里保持准备，
        // 但不会造成副作用（run_text_pipeline 可能有副作用？假设它只是计算）。

        // 核心：提取纯几何数据
        auto geos = extract_geometries(layout);

        for (const auto &geo : geos)
        {
            // 1) 每个有尺寸节点都生成矩形实例（与原始行为一致，包括文本节点）
            generate_rect_instance(w, h, uiStore, geo);

            // 2) 如果是文本节点，额外生成文本实例
            if (geo.node_id == "text_display")
            {
                // 生成文本实例
                constexpr auto rawText =
                    u8"我你好世界你好世界你好世界🤣\nW3C (World)👪 ﷲ é \nמעביר את "
                    u8"שירותי- ERCIM."; // NOTE: BUG
                auto [codepoints, shape_result, break_result] =
                    run_text_pipeline(fontSelect, rawText, "zh-CN");
                std::println("=== text_display node detected ===");
                std::println("  position: left={}, top={}, width={}, height={}", geo.x,
                             geo.y, geo.w, geo.h);
                std::println("  padding: left={}, right={}, top={}, bottom={}",
                             geo.pad_left, geo.pad_right, geo.pad_top, geo.pad_bottom);

                generate_text_instances(w, h, uiStore, geo, codepoints, shape_result,
                                        break_result.types);
            }
        }
    };
    auto screen_resize = [&](VkExtent2D newSize) {
        static VkExtent2D lastSize = {0, 0};
        if (newSize.width == lastSize.width && newSize.height == lastSize.height)
            return;
        lastSize = newSize;
        uiStore.clear();

        on_resize(layout, {.available_width = static_cast<float>(newSize.width),
                           .available_height = static_cast<float>(newSize.height)});
        layout.calculate({.available_width = static_cast<float>(newSize.width),
                          .available_height = static_cast<float>(newSize.height)});
        generateUIInstances(layout);
    };
    // 初始调用
    generateUIInstances(layout);

    // uiStore
    std::println("uiStore.size(): {}", uiStore.size());
    //diff: [test_dod10] end

    //diff: [test_dod14] 不再需要  topLeftLocal 字段，因为我们使用的公共的顶点我们是知道的
    auto soaCtx = make_aggregate_ref<"soaCtx", "autoSpinStore", "interactiveStore",
                                     "uiStore", "screen_resize">(
        autoSpinStore, interactiveStore, uiStore, screen_resize);

    //diff: [test_dod8.cpp] end

    struct record_info
    {
        uint32_t current_frame;
        uint32_t image_index;
    };

    auto pickCtx =
        make_aggregate_ref<"pickCtx", "pickResourcesBuild", "pickResource",
                           "resolveResourcesBuild", "resolveResource", "frames", "mouse">(
            pickResourcesBuild, pickResource, resolveResourcesBuild, resolveResource,
            pickingFrames, pickMouse);
    auto globalCtx =
        make_aggregate_ref<"globalCtx", "device", "window", "surface", "swapchainBuild",
                           "swapchain", "frameContext", "commandPool", "commandBuffers",
                           "queue", "meshMap">(
            device, window, surface, swapchainBuild, swapchain, frameContext, commandPool,
            commandBuffers, GRAPHICS_AND_PRESENT, meshMap);

    auto mainCtx =
        make_aggregate_ref<"mainCtx", "pipeline", "pipelineLayout", "depthResourcesBuild",
                           "depthResource", "msaaResourcesBuild", "msaaResource">(
            graphicsPipeline, pipelineLayout, depthResourcesBuild, depthResource,
            msaaResourcesBuild, msaaResource);

    auto mainShaderCtx = make_aggregate_ref<"mainShaderCtx", "indirectDrawBatches",
                                            "uniformBuffers", "descriptorSets">(
        frameResources, uniformBuffers, descriptorSets);
    auto inputCtx =
        make_aggregate_ref<"inputCtx", "input", "camera", "uiCamera", "clock">(
            input, camera, uiCamera, clock);

    // record_info
    auto recordCtx = make_aggregate<"recordCtx", "info">(record_info{});
    auto world = make_aggregate_ref<"world", "globalCtx", "mainCtx", "mainShaderCtx",
                                    "pickCtx", "recordCtx">(
        globalCtx, mainCtx, mainShaderCtx, pickCtx, recordCtx);
    using world_type = decltype(world);
    using input_type = decltype(inputCtx);
    using data_type = decltype(soaCtx);

    // diff: [test_dod2] start:  world之后 才能调用 数据API

    // init data

    //diff: [test_dod8] start
    // 从模型矩阵生成 model_state（其他交互字段保持默认）
    constexpr auto make_model_state_from_matrix = [](const glm::mat4 &M) {
        glm::vec3 trans, scale;
        glm::quat rot;
        glm::vec3 skew;
        glm::vec4 persp;
        glm::decompose(M, scale, rot, trans, skew, persp);

        model_state state;
        state.model_matrix.translation = trans;
        state.model_matrix.rotation = rot;
        state.model_matrix.scale = scale;
        // last_pos, is_middle_button_pressed 等保持默认即可
        return state;
    };

    auto autoSpinId = autoSpinStore.new_entity(
        InstanceData{{autoSpinStore_type_id, autoSpinStore.nextEntityId()},
                     0,
                     0,
                     object_data{camera::VulkanNDCConfig::rectTransform(
                         glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, 0.5f))}},
        std::array<VertexAttribute, 4>{
            VertexAttribute{glm::vec3{1.0f, 0.0f, 0.0f}}, // 红
            VertexAttribute{glm::vec3{1.0f, 0.0f, 0.0f}}, // 红
            VertexAttribute{glm::vec3{0.0f, 1.0f, 0.0f}}, // 绿
            VertexAttribute{glm::vec3{0.0f, 1.0f, 0.0f}}, // 绿
        });
    auto interactiveId = interactiveStore.new_entity(
        InstanceData{
            {interactive_type_id, interactiveStore.nextEntityId()}, 3, 1, object_data{}},
        std::array<VertexAttribute, 4>{
            VertexAttribute{{1.0f, 0.0f, 1.0f}}, // 品红
            VertexAttribute{{0.0f, 1.0f, 1.0f}}, // 青
            VertexAttribute{{1.0f, 0.5f, 0.0f}}, // 橙
            VertexAttribute{{0.5f, 0.0f, 0.5f}}, // 紫
        },
        model_state{make_model_state_from_matrix(camera::VulkanNDCConfig::rectTransform(
            glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, 0.5f), 0.2f))});

    // 随机生成两个纹理索引和采样器索引

    //diff: [test_dod3] end

    // diff: [test_dod2] end

    static constexpr auto views_matrix_update = [](world_type &world,
                                                   input_type &inputCtx,
                                                   data_type &soaCtx) {
        const auto &input = inputCtx.input;
        auto &camera = inputCtx.camera; // 类型现在是 GenCamera<...>

        using mcs::vulkan::event::Key;
        const float step = 0.1f;

        // 移动方向由 ViewMatrixObject 的 moveForward/moveRight 等自动处理手系
        if (input.isKeyPressedOrRepeat(Key::eW))
            camera.refView().moveForward(step);
        if (input.isKeyPressedOrRepeat(Key::eS))
            camera.refView().moveForward(-step);
        if (input.isKeyPressedOrRepeat(Key::eA))
            camera.refView().moveRight(-step);
        if (input.isKeyPressedOrRepeat(Key::eD))
            camera.refView().moveRight(step);
        if (input.isKeyPressedOrRepeat(Key::eQ))
            camera.refView().moveUp(-step);
        if (input.isKeyPressedOrRepeat(Key::eE))
            camera.refView().moveUp(step);

        // ========== Ctrl+右键旋转视角 ==========
        {
            static mcs::vulkan::event::position2d_event lastRightPos{};
            static bool isRightDragging = false;
            using mcs::vulkan::event::MouseButtons;

            bool ctrlPressed = input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                               input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL);
            bool curRightPressed =
                input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_RIGHT);

            // clang-format off
            struct DefaultMode {};
            struct PitchOnlyMode {};
            struct YawOnlyMode {};
            struct RollMode {};
            // clang-format on
            using CameraRotateMode =
                std::variant<DefaultMode, PitchOnlyMode, YawOnlyMode, RollMode>;
            constexpr auto getCameraRotateMode =
                [](bool alt, bool shift) noexcept -> CameraRotateMode {
                if (alt && shift)
                    return RollMode{};
                if (alt)
                    return PitchOnlyMode{};
                if (shift)
                    return YawOnlyMode{};
                return DefaultMode{};
            };

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
                        const float sens = 0.005f;
                        auto [rawYaw, rawPitch] =
                            camera::VulkanNDCConfig::ScreenDragToCameraYawPitch(dx, dy,
                                                                                sens);

                        auto &viewObj = camera.refView();
                        glm::vec3 forward = viewObj.getForward();
                        constexpr float maxPitch = glm::radians(89.0f);
                        float curPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));

                        bool altPressed = input.isKeyPressedOrRepeat(Key::eLEFT_ALT) ||
                                          input.isKeyPressedOrRepeat(Key::eRIGHT_ALT);
                        bool shiftPressed =
                            input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                            input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);

                        glm::quat deltaRot = match(
                            getCameraRotateMode(altPressed, shiftPressed),
                            [&](DefaultMode) noexcept {
                                float pitch =
                                    glm::clamp(curPitch + rawPitch, -maxPitch, maxPitch) -
                                    curPitch;
                                return glm::angleAxis(rawYaw, glm::vec3(0, 1, 0)) *
                                       glm::angleAxis(pitch, viewObj.getRight());
                            },
                            [&](PitchOnlyMode) noexcept {
                                float pitch =
                                    glm::clamp(curPitch + rawPitch, -maxPitch, maxPitch) -
                                    curPitch;
                                return glm::angleAxis(pitch, viewObj.getRight());
                            },
                            [&](YawOnlyMode) noexcept {
                                return glm::angleAxis(rawYaw, glm::vec3(0, 1, 0));
                            },
                            [&](RollMode) noexcept {
                                float rollAngle = rawYaw;
                                return glm::angleAxis(rollAngle, forward);
                            });
                        viewObj.rotateWorld(deltaRot);
                    }
                    lastRightPos = cur;
                }
            }
            else
            {
                isRightDragging = false;
            }
        }
    };

    static constexpr auto views_perspective_update =
        [](world_type &world, input_type &inputCtx, data_type &soaCtx) {
            const auto &input = inputCtx.input;
            auto &camera = inputCtx.camera;
            using mcs::vulkan::event::Key;

            constexpr float stepDeg = 1.0f; // 度数步长
            constexpr float aspectStep = 0.1f;
            auto &proj = camera.refProjection();

            // FOV 增大（限制不超过 179°）
            if (input.isKeyPressedOrRepeat(Key::eR))
                proj.adjustFovSafe(stepDeg);

            // FOV 减小（必须大于 0°）
            if (input.isKeyPressedOrRepeat(Key::eF))
                proj.adjustFovSafe(-stepDeg);

            // 宽高比增大（无上限，无需保护）
            if (input.isKeyPressedOrRepeat(Key::eT))
                proj.adjustAspect(aspectStep);
            // 宽高比减小（必须大于 0）
            if (input.isKeyPressedOrRepeat(Key::eG))
                proj.adjustAspectSafe(-aspectStep);

            // 近平面减小（必须大于 0）
            if (input.isKeyPressedOrRepeat(Key::eH))
                proj.adjustNearSafe(-stepDeg);
            // 近平面增大（无上限，无需保护）
            if (input.isKeyPressedOrRepeat(Key::eY))
                proj.adjustNearSafe(stepDeg);
            // 远平面增大（无上限，无需保护）
            if (input.isKeyPressedOrRepeat(Key::eU))
                proj.adjustFarSafe(stepDeg);
            // 远平面减小（必须大于 near）
            if (input.isKeyPressedOrRepeat(Key::eJ))
                proj.adjustFarSafe(-stepDeg);

            // 打印当前值
            // std::print("fov: {} deg, aspect: {}, near: {}, far: {}\n", proj.getFov(),
            //            proj.getAspect(), proj.getNear(), proj.getFar());
        };

    // diff: [test_model_matrix3] 小小调整

    static constexpr auto views_ui_camera_update = [](world_type &world,
                                                      input_type &inputCtx,
                                                      data_type &soaCtx) noexcept {
        const auto &input = inputCtx.input;
        auto &uiCam = inputCtx.uiCamera;
        using mcs::vulkan::event::Key;
        using mcs::vulkan::event::MouseButtons;

        // 窗口尺寸，用于拖拽坐标转换
        auto &swapchain = world.globalCtx.swapchain;
        auto extent = swapchain.refImageExtent();
        float w = static_cast<float>(extent.width);
        float h = static_cast<float>(extent.height);

        // ---------- Alt 修饰键 ----------
        bool alt = input.isKeyPressedOrRepeat(Key::eLEFT_ALT) ||
                   input.isKeyPressedOrRepeat(Key::eRIGHT_ALT);
        if (!alt)
            return;

        // clang-format off
            struct UiTranslateKey { float dx, dy; };
            struct UiDrag { float ndcDx, ndcDy; };
            struct UiZoom { float factor; };
            struct UiRotate { float angle; };
            struct UiReset {};
            using UiAction = std::variant<UiTranslateKey, UiDrag, UiZoom, UiRotate, UiReset>;
        // clang-format on

        // 统一的执行器（每次直接 match）
        auto exec = [&](const UiAction &action) noexcept {
            match(
                action,
                [&](const UiTranslateKey &t) noexcept {
                    uiCam.refView().moveRight(t.dx);
                    uiCam.refView().moveUp(t.dy);
                },
                [&](const UiDrag &d) noexcept {
                    uiCam.refView().moveRight(d.ndcDx);
                    uiCam.refView().moveUp(d.ndcDy);
                },
                [&](const UiZoom &z) noexcept {
                    uiCam.refProjection().scaleOrthoView(z.factor);
                },
                [&](const UiRotate &r) noexcept {
                    uiCam.refView().rotateWorld(
                        glm::angleAxis(r.angle, glm::vec3(0, 0, 1)));
                },
                [&](const UiReset &) noexcept {
                    uiCam.refView().setPosition(glm::vec3(0.0f));
                    uiCam.refView().setOrientation(glm::identity<glm::quat>());
                    uiCam.refProjection().setOrthoBounds(-1.0f, 1.0f, -1.0f, 1.0f);
                });
        };

        // ---------- 按键平移（Alt + WASD） ----------
        constexpr float keyStep = 0.02f;
        if (input.isKeyPressedOrRepeat(Key::eW))
            exec(UiTranslateKey{0.0f, keyStep}); // 向上
        if (input.isKeyPressedOrRepeat(Key::eS))
            exec(UiTranslateKey{0.0f, -keyStep}); // 向下
        if (input.isKeyPressedOrRepeat(Key::eA))
            exec(UiTranslateKey{-keyStep, 0.0f}); // 向左
        if (input.isKeyPressedOrRepeat(Key::eD))
            exec(UiTranslateKey{keyStep, 0.0f}); // 向右

        // ---------- 缩放（Alt + E/Q） ----------
        constexpr float zoomFactor = 1.05f;
        if (input.isKeyPressedOrRepeat(Key::eE))
            exec(UiZoom{1.0f / zoomFactor}); // 放大
        if (input.isKeyPressedOrRepeat(Key::eQ))
            exec(UiZoom{zoomFactor}); // 缩小

        // ---------- 旋转（Alt + R/F） ----------
        constexpr float rotAngle = glm::radians(2.0f);
        if (input.isKeyPressedOrRepeat(Key::eR))
            exec(UiRotate{rotAngle}); // 顺时针
        if (input.isKeyPressedOrRepeat(Key::eF))
            exec(UiRotate{-rotAngle}); // 逆时针

        // ---------- 拖拽平移（Alt + 左键） ----------
        {
            static bool wasDragging = false;
            static glm::dvec2 lastCursor{0.0, 0.0};
            bool leftPressed =
                input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_LEFT);

            if (leftPressed)
            {
                auto cur = input.cursorPos();
                if (!wasDragging)
                {
                    lastCursor = {cur.xpos, cur.ypos};
                    wasDragging = true;
                }
                else
                {
                    float dx = static_cast<float>(cur.xpos - lastCursor.x);
                    float dy = static_cast<float>(cur.ypos - lastCursor.y);
                    float ndcDx = (dx / w) * 2.0f;
                    float ndcDy = (dy / h) * 2.0f;
                    // 注意：拖拽鼠标向上（dy<0） => UI 整体上移 => 相机需下移
                    // 相机需要反向移动才能让 UI 跟随鼠标
                    exec(UiDrag{-ndcDx, -ndcDy}); // 之前只有 y 取负，现已修正 x 也取负
                    lastCursor = {cur.xpos, cur.ypos};
                }
            }
            else
            {
                wasDragging = false;
            }
        }

        // ---------- 重置（Alt + Z） ----------
        if (input.isKeyPressedOrRepeat(Key::eZ))
            exec(UiReset{});
    };

    static constexpr auto updateObjectData = [](world_type &world, input_type &inputCtx,
                                                data_type &soaCtx,
                                                uint32_t currentFrame) noexcept {
        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;

        float elapsed = inputCtx.clock.getElapsed(); // 直接获取当前累积时间

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
                    float angle = elapsed * glm::radians(90.0f) + phaseOffset;
                    // 从现有矩阵提取原始平移和缩放
                    auto [trans, scale] = camera::extractTranslationScale(mat);
                    glm::quat rot = glm::angleAxis(angle, glm::vec3(0, 0, 1));
                    mat = camera::composeTRS(trans, rot, scale);

                    //diff: [test_dod9] start
                    // ---- 动态 UV 变换 ----
                    auto &uv = instanceData.uvTransform;
                    // 示例1：纹理随时间向右滚动（重复模式）
                    uv.offset.x = fmodf(elapsed * 0.2f, 1.0f);
                    // 示例2：纹理在 0.5 倍到 1.5 倍之间周期性缩放
                    float s = 1.0f + 0.5f * sinf(elapsed * 2.0f);
                    uv.scale = glm::vec2(s, s);
                    //diff: [test_dod9] end
                }
                else [[likely]]
                {
                    // 提取现有的平移和缩放

                    float phaseOffset = idx * glm::radians(137.5f);
                    float angle = elapsed * glm::radians(90.0f) + phaseOffset;
                    glm::quat rot = glm::angleAxis(angle, glm::vec3(0, 0, 1));
                    auto [trans, scale] = camera::extractTranslationScale(mat);
                    // 重构：缩放 → 旋转 → 平移（与创建时的顺序保持一致）
                    mat = camera::composeTRS(trans, rot, scale);
                }

                ++idx;
            }
            for (const auto &[instanceData, modelState] :
                 interactiveStore.view<"instanceData", "modelState">(currentFrame))
            {
                //diff: [test_dod8] 构造时的偏移迁移到这里
                auto &objectData = instanceData.objectData;
                // modelState.model_matrix.translation.z = 0.5f; //diff: [test_dod9] 不需要补丁了。初始化的时候就填写好了
                objectData = object_data{modelState.model_matrix()};
            }
        }
        // diff: [test_dod11] start
        // UI 的矩阵操作. 各自UI组件的中心旋转
        if (0)
        {
            auto &uiStore = soaCtx.uiStore;

            int idx = 0;
            for (auto [instanceData] : uiStore.view<"instanceData">(currentFrame))
            {
                float angle = elapsed * 0.5f + idx * 0.2f;
                float s = 1.0f + 0.3f * sinf(elapsed * 2.0f + idx);
                glm::mat4 dynamicMat =
                    glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 0, 1)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(s, s, 1.0f));
                instanceData.objectData.matrix = dynamicMat;
                ++idx;
            }
        }
        else if (0) // 整体 UI 组动画
        {
            auto &uiStore = soaCtx.uiStore;

            float angle = elapsed * 0.5f;
            float s = 1.0f + 0.3f * sinf(elapsed * 2.0f);
            glm::mat4 dynamicTransform =
                glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 0, 1)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(s, s, 1.0f));

            for (auto [instanceData] : uiStore.view<"instanceData">(currentFrame))
            {
                instanceData.objectData.matrix = dynamicTransform;
            }
        }
        // diff: [test_dod11] end
        // 3. 记录结束时间点
        auto end = std::chrono::high_resolution_clock::now();

        // 4. 计算时间差，并转换为毫秒
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // 5. 打印耗时
        // if (auto count = duration.count(); count > 0)
        //     std::cout << "updateObjectData耗时: " << duration.count()
        //               << " 毫秒,数据量: " << idx << std::endl;
        //diff: [test_dod8] end
    };
    // diff: [test_model_matrix2] end

    static constexpr auto updateVertexData = [](world_type &world, input_type &inputCtx,
                                                data_type &soaCtx,
                                                uint32_t currentFrame) noexcept {
        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;
    };

    static constexpr auto model_update = [](world_type &world, input_type &inputCtx,
                                            data_type &soaCtx) {
        auto &globalCtx = world.globalCtx;
        auto &interactiveStore = soaCtx.interactiveStore;
        auto [modelState, instanceData] =
            *(interactiveStore.view<"modelState", "instanceData">().begin());

        const auto &input = inputCtx.input;
        auto &camera = inputCtx.camera;
        auto &swapchain = globalCtx.swapchain;

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

        // ================= 滚轮缩放 =================
        if (scroll_event{} != input.scroll())
        {
            std::println("input.scroll(): {}", input.scroll());
            bool ctrlPressed = input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                               input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL);
            bool altPressed = input.isKeyPressedOrRepeat(Key::eLEFT_ALT) ||
                              input.isKeyPressedOrRepeat(Key::eRIGHT_ALT);
            bool shiftPressed = input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                                input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);
            float delta = input.scroll().yoffset;
            if (delta != 0.0f)
            {
                const float factor = (delta > 0) ? 1.1f : (1.0f / 1.1f);
                int modCount =
                    (ctrlPressed ? 1 : 0) + (altPressed ? 1 : 0) + (shiftPressed ? 1 : 0);
                if (modCount == 1)
                {
                    if (ctrlPressed)
                        modelMatrix.scale.x *= factor;
                    else if (altPressed)
                        modelMatrix.scale.y *= factor;
                    else if (shiftPressed)
                        modelMatrix.scale.z *= factor;
                }
                else
                {
                    modelMatrix.scale *= factor;
                }
            }
        }

        // ================= 右键复位 =================
        if (curRightPressed && !rightButtonPressedLast)
        {
            // modelMatrix.translation = glm::vec3(0.0f); //NOTE: 暴露平移
            modelMatrix.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            modelMatrix.scale = {1.0f, 1.0f, 1.0f};
        }
        rightButtonPressedLast = curRightPressed;

        // ================= 左键旋转 =================
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
                    auto &viewObj = camera.refView();
                    glm::vec3 worldRight = viewObj.getRight();
                    glm::vec3 worldUp = viewObj.getUp();
                    glm::vec3 worldForward = viewObj.getForward();

                    const float sens = 0.01f;
                    auto [yaw, pitch] =
                        camera::VulkanNDCConfig::ScreenDragToYawPitch(dx, dy, sens);

                    bool ctrl = input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                                input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL);
                    bool alt = input.isKeyPressedOrRepeat(Key::eLEFT_ALT) ||
                               input.isKeyPressedOrRepeat(Key::eRIGHT_ALT);
                    bool shift = input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                                 input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);

                    // clang-format off
                    // 旋转约束模式（模型旋转用）
                    struct DefaultRotMode {};   // 绕世界 Up 和 Right 旋转
                    struct PitchOnlyRotMode {}; // 仅绕世界 Right（俯仰）
                    struct YawOnlyRotMode {};   // 仅绕世界 Up（偏航）
                    struct RollRotMode {};      // 绕世界 Forward（滚转）
                    using ModelRotMode = std::variant<DefaultRotMode, PitchOnlyRotMode,
                                                    YawOnlyRotMode, RollRotMode>;
                    // clang-format on
                    // 确定旋转模式
                    ModelRotMode rotMode = [&]() -> ModelRotMode {
                        if (ctrl)
                            return YawOnlyRotMode{};
                        if (alt)
                            return PitchOnlyRotMode{};
                        if (shift)
                            return RollRotMode{};
                        return DefaultRotMode{};
                    }();

                    glm::quat delta = match(
                        rotMode,
                        [&](DefaultRotMode) {
                            glm::quat rotY = glm::angleAxis(yaw, worldUp);
                            glm::quat rotX = glm::angleAxis(pitch, worldRight);
                            return rotY * rotX;
                        },
                        [&](PitchOnlyRotMode) {
                            return glm::angleAxis(pitch, worldRight);
                        },
                        [&](YawOnlyRotMode) { return glm::angleAxis(yaw, worldUp); },
                        [&](RollRotMode) { return glm::angleAxis(yaw, worldForward); });

                    if (delta != glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
                        modelMatrix.rotation = delta * modelMatrix.rotation;
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
            bool shiftPressed = input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                                input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);

            auto [rayOrigin, rayDir] = camera::VulkanNDCConfig::ScreenToWorldRay(
                glm::ivec2(cur.xpos, cur.ypos),
                glm::ivec2(windowSize.width, windowSize.height), camera.getViewMatrix(),
                camera.getProjMatrix());

            constexpr auto topLeftLocal = quadVerts[0].pos; //diff: [test_dod14] 固有属性
            glm::vec3 currentTopLeftWorld = camera::transformPointToWorld(
                topLeftLocal, instanceData.objectData.matrix,
                instanceData.vertexTransform.matrix);

            // clang-format off
            struct WorldDepthMode {};   // 保持世界空间 Z 不变
            struct CameraDepthMode {};  // 保持相机空间深度不变
            using DepthMode = std::variant<WorldDepthMode, CameraDepthMode>;
            // clang-format on
            // 选择深度模式
            DepthMode depthMode =
                shiftPressed ? DepthMode{WorldDepthMode{}} : DepthMode{CameraDepthMode{}};

            glm::vec3 hit = match(
                depthMode,
                [&](WorldDepthMode) noexcept {
                    float targetZ = currentTopLeftWorld.z;
                    float t = (targetZ - rayOrigin.z) / rayDir.z;
                    return (t > 0) ? (rayOrigin + t * rayDir) : currentTopLeftWorld;
                },
                [&](CameraDepthMode) noexcept {
                    glm::mat4 view = camera.getViewMatrix();
                    glm::vec4 curView = view * glm::vec4(currentTopLeftWorld, 1.0f);
                    glm::vec3 viewOrigin = view * glm::vec4(rayOrigin, 1.0f);
                    glm::vec3 viewDir = view * glm::vec4(rayDir, 0.0f);
                    float t = (curView.z - viewOrigin.z) / viewDir.z;
                    return (t > 0) ? glm::vec3(glm::inverse(view) *
                                               glm::vec4(viewOrigin + t * viewDir, 1.0f))
                                   : currentTopLeftWorld;
                });

            glm::vec3 offset = camera::computeAnchorOffset(
                topLeftLocal, modelMatrix, instanceData.vertexTransform.matrix);
            modelMatrix.translation = hit - offset;

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
    static constexpr auto prepareBatch = [](world_type &world, input_type &inputCtx,
                                            data_type &soaCtx, uint32_t currentFrame) {
        auto &globalCtx = world.globalCtx;
        auto &mainShaderCtx = world.mainShaderCtx;

        auto &device = globalCtx.device;
        auto &meshMap = globalCtx.meshMap;

        auto &descriptorSets = mainShaderCtx.descriptorSets;
        auto &frameResources = mainShaderCtx.indirectDrawBatches;

        auto &descriptorSet = descriptorSets[currentFrame];
        auto &batch = frameResources[currentFrame];

        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;
        auto &uiStore = soaCtx.uiStore;

        auto start = std::chrono::high_resolution_clock::now();
        //diff: [test_dod11] start
        {
            // 临时命令和常量数组
            std::vector<VkDrawIndexedIndirectCommand> cmds;
            std::vector<CommandConstant> cmdConsts;

            // 用于写入 buffer 的偏移
            VkDeviceSize instanceOffset = 0;
            VkDeviceSize attributeOffset = 0;
            batch.drawCount3D = 0;
            batch.drawCountUI = 0;

            // 处理一个 store（它绑定到一个 mesh，mesh 提供顶点数）
            auto processStore = [&](auto &store, const mesh_data &mesh, uint32_t frame) {
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
                batch.globalInstanceBuffer.write(instanceOffset, instSrc, instSize);

                // 2. 整体拷贝属性数据（每个实体一个 std::array<VertexAttribute, 4>）
                std::array<VertexAttribute, 4> *attrSrc =
                    store.template raw_field<"vertexAttribute">(frame).data();
                VkDeviceSize attrSize = count * sizeof(std::array<VertexAttribute, 4>);
                batch.globalAttributeBuffer.write(attributeOffset, attrSrc, attrSize);

                // 3. 生成间接命令
                cmds.push_back(mesh.getDrawCommand(count, firstInstance));

                // 4. 生成命令常量
                cmdConsts.push_back(CommandConstant{mesh.vertexCount});

                instanceOffset += instSize;
                attributeOffset += attrSize;

                // std::cout << "写入了实体: " << count << std::endl;
            };

            const mesh_data &quadMesh = meshMap["quad"];
            // 3D
            {
                processStore(autoSpinStore, quadMesh, currentFrame);
                processStore(interactiveStore, quadMesh, currentFrame);
            }
            uint32_t drawCount3D = static_cast<uint32_t>(cmds.size());
            batch.drawCount3D = drawCount3D;

            // UI
            if (1)
            {
                // ========== UI 周期显示控制 ==========
                constexpr float UI_ON_DURATION = 2.0f;  // 显示 2 秒
                constexpr float UI_OFF_DURATION = 3.0f; // 隐藏 3 秒
                constexpr float UI_PERIOD = UI_ON_DURATION + UI_OFF_DURATION;

                static auto s_uiCycleStart = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                float elapsed =
                    std::chrono::duration<float>(now - s_uiCycleStart).count();
                if (elapsed >= UI_PERIOD)
                {
                    s_uiCycleStart = now;
                    elapsed = 0.0f;
                }
                bool showUI = (elapsed < UI_ON_DURATION);

                if (showUI)
                {
                    processStore(uiStore, quadMesh, currentFrame);
                }
            }
            batch.drawCountUI = cmds.size() - drawCount3D;

            // 上传命令和常量
            // 所有命令（3D+UI）一次性拷贝
            size_t totalDrawCount = cmds.size();
            batch.indirectDrawBuffer.write(
                0, cmds.data(), totalDrawCount * sizeof(VkDrawIndexedIndirectCommand));
            batch.commandConstantsBuffer.write(0, cmdConsts.data(),
                                               totalDrawCount * sizeof(CommandConstant));
        }
        //diff: [test_dod11] end
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
        std::constant_wrapper<[](world_type &world, input_type &inputCtx,
                                 data_type &soaCtx) {
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
        std::constant_wrapper<[](world_type &world, input_type &inputCtx,
                                 data_type &soaCtx) {
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
        std::constant_wrapper<[](world_type &world, input_type &inputCtx,
                                 data_type &soaCtx) {
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
        std::constant_wrapper<[](world_type &world, input_type &inputCtx,
                                 data_type &soaCtx) {
            auto &mainShaderCtx = world.mainShaderCtx;
            auto &recordCtx = world.recordCtx;
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;

            auto &commandBuffers = globalCtx.commandBuffers;
            auto &pipelineLayout = mainCtx.pipelineLayout;

            auto &uniformBuffers = mainShaderCtx.uniformBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto &frameResources = mainShaderCtx.indirectDrawBatches;

            // diff: [test_dod8] start 使用合并索引缓冲和间接绘制
            auto &batch = frameResources[currentFrame];
            commandBuffer.bindIndexBuffer(batch.globalIndexBuffer.buffer.buffer(), 0,
                                          VK_INDEX_TYPE_UINT32);

            // diff: [test_dod11] start

            auto uploadUniformBuffers = [&]() {
                UniformBufferObject ubo;
                // 3D 矩阵从 camera 获取
                ubo.cameraInfo[0].view = inputCtx.camera.getViewMatrix();
                ubo.cameraInfo[0].proj = inputCtx.camera.getProjMatrix();
                // UI 矩阵从 uiCamera 获取
                ubo.cameraInfo[1].view = inputCtx.uiCamera.getViewMatrix();
                ubo.cameraInfo[1].proj = inputCtx.uiCamera.getProjMatrix();

                memcpy(uniformBuffers[currentFrame].mapPtr(), &ubo, sizeof(ubo));
            };
            uploadUniformBuffers();

            if (batch.drawCount3D > 0)
            {
                PushData push = {.vertexAddress = batch.globalVertexBuffer.address,
                                 .attributeAddress = batch.globalAttributeBuffer.address,
                                 .instanceAddress = batch.globalInstanceBuffer.address,
                                 .commandConstantsAddress =
                                     batch.commandConstantsBuffer.address,
                                 .cameraIndex = 0};
                commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                            0, sizeof(PushData), &push);
                commandBuffer.setDepthWriteEnable(true);
                commandBuffer.setDepthTestEnable(true);
                commandBuffer.drawIndexedIndirect(
                    batch.indirectDrawBuffer.buffer.buffer(), 0, batch.drawCount3D,
                    sizeof(VkDrawIndexedIndirectCommand));
            }
            if (batch.drawCountUI)
            {
                PushData push = {.vertexAddress = batch.globalVertexBuffer.address,
                                 .attributeAddress = batch.globalAttributeBuffer.address,
                                 .instanceAddress = batch.globalInstanceBuffer.address,
                                 .commandConstantsAddress =
                                     batch.commandConstantsBuffer.address,
                                 .cameraIndex = 1};
                commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                            0, sizeof(PushData), &push);
                commandBuffer.setDepthWriteEnable(false);
                commandBuffer.setDepthTestEnable(false);
                VkDeviceSize offset =
                    batch.drawCount3D * sizeof(VkDrawIndexedIndirectCommand);
                commandBuffer.drawIndexedIndirect(
                    batch.indirectDrawBuffer.buffer.buffer(), offset, batch.drawCountUI,
                    sizeof(VkDrawIndexedIndirectCommand));
            }
            // diff: [test_dod11] end
            // diff: [test_dod8] start
        }>{},
        std::constant_wrapper<[](world_type &world, input_type &inputCtx,
                                 data_type &soaCtx) {
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
    static constexpr auto recordCommandBuffer = [](world_type &world,
                                                   input_type &inputCtx,
                                                   data_type &soaCtx) {
        auto &globalCtx = world.globalCtx;
        auto &recordCtx = world.recordCtx;

        auto &swapchain = globalCtx.swapchain;
        auto &commandBuffers = globalCtx.commandBuffers;

        auto [currentFrame, imageIndex] = recordCtx.info;
        const auto &commandBuffer = commandBuffers[currentFrame];
        VkImage image = swapchain.image(imageIndex);

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        mainPipeline.transitionImageLayout(world, inputCtx, soaCtx);
        mainPipeline.beginRendering(world, inputCtx, soaCtx);
        mainPipeline.setPipelineState(world, inputCtx, soaCtx);
        mainPipeline.draw(world, inputCtx, soaCtx);
        mainPipeline.endRendering(world, inputCtx, soaCtx);

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
    static constexpr auto recreateSwapChain = [](world_type &world, input_type &inputCtx,
                                                 data_type &soaCtx) constexpr {
        auto &globalCtx = world.globalCtx;
        auto &pickCtx = world.pickCtx;
        auto &mainCtx = world.mainCtx;

        auto &device = globalCtx.device;
        auto &surface = globalCtx.surface;
        auto &swapchainBuild = globalCtx.swapchainBuild;
        auto &swapchain = globalCtx.swapchain;
        auto &camera = inputCtx.camera;
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

        swapchain = swapchainBuild.rebuild(device, surface);
        auto newExtent = swapchain.refImageExtent();
        VkExtent3D imageExtent = {
            .width = newExtent.width, .height = newExtent.height, .depth = 1};

        msaaResource = msaaResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        depthResource =
            depthResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        camera.refProjection().setAspect(newExtent.width /
                                         static_cast<float>(newExtent.height));

        pickResource = pickResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        resolveResource =
            resolveResourcesBuild.setCreateInfoExtent(imageExtent).build(device);

        pickMouse.valid = false;

        // diff: [test_dod12.cpp] start
        soaCtx.screen_resize(newExtent);
        // diff: [test_dod12.cpp] end

        frameContext.rebuild(swapchain.imagesSize());
    };
    // diff: [test_dod5] start

    static constexpr auto limit_frame_rate = [](input_type &inputCtx,
                                                float targetFPS) noexcept {
        auto &clk = inputCtx.clock;
        using namespace std::chrono;
        using Clock = FrameClock::Clock;
        using FloatSeconds = duration<float>;

        const auto targetFrameDuration = FloatSeconds(1.0f / targetFPS);
        auto now = Clock::now();

        // 计算下一帧的理想开始时间（基于上一帧结束时间）
        auto nextFrameTime =
            clk.lastTime + duration_cast<Clock::duration>(targetFrameDuration);

        // 如果当前时间已经晚于理想时间，说明上一帧超时，不等待
        if (now < nextFrameTime)
        {
            std::this_thread::sleep_until(nextFrameTime);
            // sleep 后重新获取精确时间，更新 lastTime，避免累积误差
            clk.lastTime = Clock::now();
        }
        else
        {
            // 已经超时，直接更新 lastTime 为当前时间
            clk.lastTime = now;
        }
    };
    static constexpr auto tickClock = [](input_type &inputCtx) noexcept {
        auto &clk = inputCtx.clock;
        auto now = FrameClock::Clock::now();
        if (clk.lastTime.time_since_epoch().count() != 0)
        {
            float raw = std::chrono::duration<float>(now - clk.lastTime).count();
            clk.deltaTime = glm::clamp(raw, 0.001f, 0.1f);
        }
        clk.lastTime = now;
    };
    static constexpr auto FrameRateMonitor = [](world_type &world, input_type &inputCtx,
                                                data_type &soaCtx) {
        auto &clk = inputCtx.clock;
        static auto lastPrint =
            FrameClock::Clock::now(); // 这里的 static 只用于打印间隔，无伤大雅
        static int frames = 0;
        ++frames;
        auto now = FrameClock::Clock::now();
        float elapsed = std::chrono::duration<float>(now - lastPrint).count();
        if (elapsed >= 1.0f)
        {
            float fps = frames / elapsed;
            std::println("[FPS] {:5.1f} (avg frame: {:.3f} ms)", fps, 1000.0f / fps);
            frames = 0;
            lastPrint = now;
        }
    };
    // diff: [test_dod5] end
    static constexpr auto InputController = [](world_type &world, input_type &inputCtx,
                                               data_type &soaCtx) {
        auto &pickCtx = world.pickCtx;
        auto &globalCtx = world.globalCtx;

        auto &input = inputCtx.input;
        auto &pickMouse = pickCtx.mouse;
        auto &swapchain = globalCtx.swapchain;

        Window::pollEvents();

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

        // NOTE: 从 soaCtx 的成员找这个”autoSpinStore“的成员就好了
        inputController(world, inputCtx, soaCtx);
    };

    //NOTE: 下面的内联做的更好，编译期的数据更利于优化
    constexpr auto marking_function = ^^decltype([](auto &&...) noexcept {});
    static constexpr auto task_graph = make_task<
        init_task<
            {.name = "input_start", .function = marking_function},
            {.name = "input_end", .function = marking_function},
            {.name = "after_waitForfences_start", .function = marking_function},
            {.name = "after_waitForfences_end", .function = marking_function},
            {.name = "before_recordCommandBuffer_start", .function = marking_function},
            {.name = "before_recordCommandBuffer_end", .function = marking_function}>,
        [] {
            return schedulable_task{
                .task = {.name = "input_handle",
                         .function =
                             ^^decltype([](world_type &world, input_type &inputCtx,
                                           data_type &soaCtx) {
                                 tickClock(inputCtx); // ① 唯一的时间推进
                                 limit_frame_rate(inputCtx,
                                                  60.0f); // ② 帧率限制（可选）
                                 InputController(world, inputCtx, soaCtx);
                                 views_matrix_update(world, inputCtx, soaCtx);
                                 views_perspective_update(world, inputCtx, soaCtx);
                                 // diff: [test_dod14] 新增：UI 相机更新（Alt + 按键）
                                 views_ui_camera_update(world, inputCtx, soaCtx);
                                 model_update(world, inputCtx, soaCtx);
                                 FrameRateMonitor(world, inputCtx,
                                                  soaCtx);     // ③ 纯监控
                                 inputCtx.input.scroll() = {}; // 滚轮清零放在最后
                             })},
                .befores = {"input_start"},
                .afters = {"input_end"}};
        },
        [] {
            return schedulable_task{
                .task = {.name = "waitforfences_post_processing",
                         .function =
                             ^^decltype([](world_type &world, input_type &inputCtx,
                                           data_type &soaCtx) {
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
                                         (currentFrame - 1 + MAX_FRAMES_IN_FLIGHT) %
                                         MAX_FRAMES_IN_FLIGHT;
                                     //diff: [test_dod16] start
                                     //  NOTE: 应该提取出来的
                                     auto *data = static_cast<picking_result *>(
                                         pickingFrames[readIdx].mapPtr());
                                     if (data->key != 0xFFFFFFFF)
                                     {
                                         on_hover(world, inputCtx, soaCtx, *data);
                                     }
                                     //diff: [test_dod16] end
                                 }
                             })},
                .befores = {"after_waitForfences_start"},
                .afters = {"after_waitForfences_end"},
            };
        },
        [] {
            return schedulable_task{
                .task = {.name = "update_upload_data",
                         .function =
                             ^^decltype([](world_type &world, input_type &inputCtx,
                                           data_type &soaCtx) {
                                 auto &globalCtx = world.globalCtx;
                                 auto &mainShaderCtx = world.mainShaderCtx;

                                 auto &frameContext = globalCtx.frameContext;
                                 auto &camera = inputCtx.camera;

                                 auto &currentFrame = frameContext.currentFrame;

                                 auto &uniformBuffers = mainShaderCtx.uniformBuffers;

                                 //diff: [test_dod3] start
                                 updateObjectData(world, inputCtx, soaCtx, currentFrame);
                                 updateVertexData(world, inputCtx, soaCtx, currentFrame);
                                 prepareBatch(world, inputCtx, soaCtx, currentFrame);
                                 //diff: [test_dod3] end
                             })},
                .befores = {"before_recordCommandBuffer_start"},
                .afters = {"before_recordCommandBuffer_end"}};
        }>{};
    std::cout << "task_graph: [begin]\n"
              << task_graph.task_sequence_string_detail() << "\ntask_graph: [end]\n";

    // NOLINTNEXTLINE
    static constexpr auto drawFrame = [](world_type &world, input_type &inputCtx,
                                         data_type &soaCtx) constexpr {
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
        task_graph.invoke_ranges<"after_waitForfences_start", "after_waitForfences_end">(
            world, inputCtx, soaCtx);

        auto [result, imageIndex] = swapchain.acquireNextImage(
            UINT64_MAX, presentCompleteSemaphore[semaphoreIndex], nullptr);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            recreateSwapChain(world, inputCtx, soaCtx);
            return;
        }
        if (result != VK_SUCCESS)
            throw std::runtime_error("failed to acquire swap chain image!");
        device.resetFences(1, inFlightFences[currentFrame]);

        task_graph.invoke_ranges<"before_recordCommandBuffer_start",
                                 "before_recordCommandBuffer_end">(world, inputCtx,
                                                                   soaCtx);

        const auto &commandBuffer = commandBuffers[currentFrame];
        commandBuffer.reset({});
        recordCtx.info = {.current_frame = currentFrame, .image_index = imageIndex};
        recordCommandBuffer(world, inputCtx, soaCtx);

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
            recreateSwapChain(world, inputCtx, soaCtx);
        }
        semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphore.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    };
    while (globalCtx.window.shouldClose() == 0)
    {
        task_graph.invoke_ranges<"input_start", "input_end">(world, inputCtx, soaCtx);

        // ========== 新增：每 2 秒切换 autoSpinStore 第一个实体的纹理索引 ==========
        {
            // auto &autoSpinStore = soaCtx.autoSpinStore;
            // if (autoSpinStore.size() > 0) // 确保至少有一个实体
            // {
            //     static auto lastTexSwitch = std::chrono::steady_clock::now();
            //     auto now = std::chrono::steady_clock::now();
            //     if (std::chrono::duration_cast<std::chrono::seconds>(now - lastTexSwitch)
            //             .count() >= 2)
            //     {
            //         lastTexSwitch = now;

            //         // 直接通过 view_entity 获取第一个实体的 textureIndex 引用并修改
            //         // 注意：实体 ID 是 0（第一个创建的实体）
            //         auto [texRef] = autoSpinStore.view_entity<"instanceData">(0, 0);
            //         // 在 0 和 3 之间切换（这两个槽位已经绑定了纹理，确保有效）
            //         texRef.textureIndex = (texRef.textureIndex == 0) ? 3 : 0;
            //     }
            // }
        }
        // ======================================================================
        drawFrame(world, inputCtx, soaCtx);
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