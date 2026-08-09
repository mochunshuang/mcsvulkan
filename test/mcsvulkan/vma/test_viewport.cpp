#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>

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

using mcs::vulkan::load::raw_stbi_image;
using Sampler = mcs::vulkan::Sampler;
using create_sampler = mcs::vulkan::tool::create_sampler;

using mcs::vulkan::MCS_ASSERT;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_viewport";
// ============================================================
// 视口效果测试（最小实现原理）
//   每个 view = {面板, 内容, scissor, 相机}
//   setScissor(视图区域) → pushConstants(该视图命令常量基址 + 相机)
//   → drawIndexedIndirect(该视图命令)：内容被裁剪在视图内，
//   溢出部分（首行/末行半截）不会画出视图边界。
//   初始化直接复制自 test_sdf.cpp，核心在数据上传与处理。
// ============================================================
static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;

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

// NOLINTBEGIN
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
    // Polygon Mode: https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/primitive_topology.html#:~:text=)%20out%3B-,Polygon%20Mode,-Once%20you%20have
    requiredDeviceExtension.emplace_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceVulkan12Features, VkPhysicalDeviceVulkan11Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        enablefeatureChain = {
            {.features =
                 {
                     .independentBlend = VK_TRUE,  // NOTE: 两个frag 附件
                     .multiDrawIndirect = VK_TRUE, // 多实例绘制
                     .wideLines = VK_TRUE,         // 管线切换演示：setLineWidth > 1
                     .largePoints = VK_TRUE,       // 管线切换演示：gl_PointSize > 1
                     .samplerAnisotropy = VK_TRUE, //各向异性过滤
                     .shaderInt64 = VK_TRUE,       // uint64支持
                 }},
            {
                .shaderDemoteToHelperInvocation = VK_TRUE, // discard 关键字需要
                .synchronization2 = VK_TRUE,
                .dynamicRendering = VK_TRUE,
            },
            {
                .descriptorIndexing = VK_TRUE,                        //描述符索引
                .shaderSampledImageArrayNonUniformIndexing = VK_TRUE, //动态索引访问描述符
                .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE, //更新采样图像
                .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE, //更新存储映像
                .descriptorBindingPartiallyBound = VK_TRUE,          //允许无效的描述符
                .descriptorBindingVariableDescriptorCount = VK_TRUE, //描述符大小可变

                .runtimeDescriptorArray = VK_TRUE, // [] 声明描述符

                .scalarBlockLayout = VK_TRUE,   //紧凑内存布局
                .bufferDeviceAddress = VK_TRUE, //bufffer地址

            },
            {
                .shaderDrawParameters = VK_TRUE // 着色器绘制上下文参数
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
                       features2.features.largePoints &&       // 管线切换演示需要
                       features2.features.wideLines &&         // 管线切换演示需要
                       features2.features
                           .independentBlend && //diff: [test_indirectdraw_no_pick]
                       query_vulkan13_features.dynamicRendering &&
                       query_vulkan13_features.synchronization2 &&
                       query_vulkan13_features.shaderDemoteToHelperInvocation &&
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

struct FrameClock
{
    using Clock = std::chrono::steady_clock;    // 单调时钟，适合测时间间隔
    Clock::time_point startTime = Clock::now(); // 程序启动时自动记录
    Clock::time_point lastTime = startTime;
    float deltaTime = 0.016f;

    // 返回从 startTime 到现在的秒数（float）
    [[nodiscard]] float getElapsed() const noexcept
    {
        return std::chrono::duration<float>(Clock::now() - startTime).count();
    }
};
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
        // 投影：正交范围 [-1,1] 且 near=0, far=1 → 投影矩阵 = I //NOTE: -10.0f, 10.0f 避免绕 Y X 被裁剪
        auto uiProj = camera::VulkanUIOrthographicProjection{-1.0f, 1.0f,   -1.0f,
                                                             1.0f,  -10.0f, 10.0f};
        return camera::GenCamera(uiView, uiProj);
    }();
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
struct Vertex // NOLINT
{
    glm::vec3 pos;
    glm::vec2 texCoord; // diff: [texture] 添加纹理坐标
};
// 逐顶点属性（type 7 用，独立属性池，同 test_dod18；其它类型无开销）
struct VertexAttribute // NOLINT
{
    glm::vec3 color;
};
struct mesh_manager
{
    std::vector<Vertex> allVertices; // 公共顶点池（20B/顶点）
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

constexpr auto initMeshManager()
{
    // NOTE: 考虑放到一个命名空间或等区域统一处理
    constexpr std::array<Vertex, 4> quadVerts = {
        Vertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}}, // 左上
        Vertex{{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},  // 右上
        Vertex{{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},   // 右下
        Vertex{{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}   // 左下
    };
    constexpr auto quadIdx = std::array<uint32_t, 6>{0, 1, 2, 0, 2, 3};

    // 三角形网格：固定 3 个顶点（颜色由实例上传，不占网格数据）
    constexpr std::array<Vertex, 3> triVerts = {
        Vertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
        Vertex{{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
        Vertex{{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f}}};
    constexpr auto triIdx = std::array<uint32_t, 3>{0, 1, 2};
    // 线框循环网格：与三角形同顶点，LINE_LIST 用 6 索引画三条边（TRIANGLE_LIST 下是退化三角形）
    constexpr std::array<Vertex, 3> triLoopVerts = {
        Vertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
        Vertex{{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
        Vertex{{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f}}};
    constexpr auto triLoopIdx = std::array<uint32_t, 6>{0, 1, 1, 2, 2, 0};

    mesh_manager m;
    m.addMesh("quad", std::span{quadVerts}, std::span{quadIdx});
    m.addMesh("triangle", std::span{triVerts}, std::span{triIdx});
    m.addMesh("triLoop", std::span{triLoopVerts}, std::span{triLoopIdx});
    return m;
}

// ============================================================
// CPU 路径细分器（type 6 UIMesh，无第三方库）
//   - 贝塞尔：de Casteljau 均匀采样（quad→8 段，cubic→16 段）
//   - 三角化：耳切（ear clipping，支持凹多边形，单轮廓无孔）
// ============================================================
struct PathTessellator
{
    std::vector<glm::vec2> pts;
    bool started = false;

    void begin()
    {
        pts.clear();
        started = false;
    }
    void moveTo(glm::vec2 p)
    {
        pts.clear();
        pts.push_back(p);
        started = true;
    }
    void lineTo(glm::vec2 p)
    {
        if (!started)
        {
            moveTo(p);
            return;
        }
        pts.push_back(p);
    }
    void quadTo(glm::vec2 c, glm::vec2 p)
    {
        constexpr int N = 8;
        const glm::vec2 a = pts.back();
        for (int i = 1; i <= N; i++)
        {
            const float t = float(i) / float(N);
            const float u = 1.0f - t;
            lineTo(u * u * a + 2.0f * u * t * c + t * t * p);
        }
    }
    void cubicTo(glm::vec2 c1, glm::vec2 c2, glm::vec2 p)
    {
        constexpr int N = 16;
        const glm::vec2 a = pts.back();
        for (int i = 1; i <= N; i++)
        {
            const float t = float(i) / float(N);
            const float u = 1.0f - t;
            lineTo(u * u * u * a + 3.0f * u * u * t * c1 + 3.0f * u * t * t * c2 +
                   t * t * t * p);
        }
    }
    // 闭合轮廓：去掉首尾重复点
    std::vector<glm::vec2> close()
    {
        std::vector<glm::vec2> poly = std::move(pts);
        if (poly.size() > 1 && glm::length(poly.front() - poly.back()) < 1e-5f)
        {
            poly.pop_back();
        }
        pts.clear();
        started = false;
        return poly;
    }
};

// 点在三角形内（含边界）
static bool pointInTriangle(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c)
{
    auto cross = [](glm::vec2 o, glm::vec2 x, glm::vec2 y) {
        return (x.x - o.x) * (y.y - o.y) - (x.y - o.y) * (y.x - o.x);
    };
    const float d1 = cross(a, b, p);
    const float d2 = cross(b, c, p);
    const float d3 = cross(c, a, p);
    const bool neg = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
    const bool pos = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
    return !(neg && pos);
}

// 耳切三角化：任意方向单轮廓（凸/凹均可，无孔）
static void earClipPolygon(const std::vector<glm::vec2> &poly, std::vector<uint32_t> &out)
{
    const size_t n = poly.size();
    if (n < 3)
        return;
    double area2 = 0.0;
    for (size_t i = 0; i < n; i++)
    {
        const auto &a = poly[i];
        const auto &b = poly[(i + 1) % n];
        area2 += double(a.x) * b.y - double(b.x) * a.y;
    }
    const bool ccw = area2 > 0.0;
    std::vector<uint32_t> idx(n);
    for (size_t i = 0; i < n; i++)
        idx[i] = static_cast<uint32_t>(i);
    const auto cross = [&](size_t i0, size_t i1, size_t i2) {
        const auto &a = poly[idx[i0]];
        const auto &b = poly[idx[i1]];
        const auto &c = poly[idx[i2]];
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    while (idx.size() > 3)
    {
        const size_t m = idx.size();
        bool clipped = false;
        for (size_t i = 0; i < m; i++)
        {
            const size_t i0 = (i + m - 1) % m;
            const size_t i1 = i;
            const size_t i2 = (i + 1) % m;
            const float cr = cross(i0, i1, i2);
            const bool convex = ccw ? cr > 0.0f : cr < 0.0f;
            if (!convex)
                continue;
            bool hasInside = false;
            for (size_t j = 0; j < m; j++)
            {
                if (j == i0 || j == i1 || j == i2)
                    continue;
                if (pointInTriangle(poly[idx[j]], poly[idx[i0]], poly[idx[i1]],
                                    poly[idx[i2]]))
                {
                    hasInside = true;
                    break;
                }
            }
            if (!hasInside)
            {
                out.push_back(idx[i0]);
                out.push_back(idx[i1]);
                out.push_back(idx[i2]);
                idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i1));
                clipped = true;
                break;
            }
        }
        if (!clipped)
            break; // 数值退化保护
    }
    if (idx.size() == 3)
    {
        out.push_back(idx[0]);
        out.push_back(idx[1]);
        out.push_back(idx[2]);
    }
}

// 圆（凸）
static void buildCirclePoly(std::vector<glm::vec2> &poly, glm::vec2 c, float r,
                            int seg = 48)
{
    poly.clear();
    for (int i = 0; i < seg; i++)
    {
        const float a = 6.2831853f * float(i) / float(seg);
        poly.push_back(c + glm::vec2(std::cos(a), std::sin(a)) * r);
    }
}

// 圆角矩形（凸）
static void buildRoundedRectPoly(std::vector<glm::vec2> &poly, glm::vec2 c,
                                 glm::vec2 half, float radius, int segPerArc = 10)
{
    poly.clear();
    const float r = std::min(std::max(radius, 0.0f), std::min(half.x, half.y));
    constexpr float PI = 3.14159265f;
    const std::array<glm::vec2, 4> corners = {
        glm::vec2(-half.x + r, -half.y + r), // 左上（y 向下：小 y 在上）
        glm::vec2(half.x - r, -half.y + r),  // 右上
        glm::vec2(half.x - r, half.y - r),   // 右下
        glm::vec2(-half.x + r, half.y - r),  // 左下
    };
    const std::array<float, 4> startAngles = {PI, PI * 1.5f, 0.0f, PI * 0.5f};
    for (int k = 0; k < 4; k++)
    {
        for (int i = 0; i < segPerArc; i++)
        {
            const float a = startAngles[k] + PI * 0.5f * float(i) / float(segPerArc);
            poly.push_back(corners[k] + glm::vec2(std::cos(a), std::sin(a)) * r);
        }
    }
}

// 星形（凹 → 耳切）
static void buildStarPoly(std::vector<glm::vec2> &poly, glm::vec2 c, float outer,
                          float inner, int spikes = 5)
{
    poly.clear();
    const float start = -1.5707963f; // 尖朝上（y 向下坐标）
    for (int k = 0; k < spikes * 2; k++)
    {
        const float a = start + 3.14159265f * float(k) / float(spikes);
        const float r = (k % 2 == 0) ? outer : inner;
        poly.push_back(c + glm::vec2(std::cos(a), std::sin(a)) * r);
    }
}

// 心形（三次贝塞尔 → 凹 → 耳切）
static void buildHeartPoly(std::vector<glm::vec2> &poly)
{
    PathTessellator t;
    t.begin();
    t.moveTo({0.0f, 0.35f});                                     // 底部尖端
    t.cubicTo({0.45f, 0.10f}, {0.40f, -0.30f}, {0.0f, -0.10f});  // 右瓣
    t.cubicTo({-0.40f, -0.30f}, {-0.45f, 0.10f}, {0.0f, 0.35f}); // 左瓣
    poly = t.close();
}

// 单瓣花瓣（2 段三次曲线，绕中心旋转 angle 后返回）
static void buildPetalPoly(std::vector<glm::vec2> &poly, float angle)
{
    PathTessellator t;
    t.begin();
    t.moveTo({0.0f, 0.0f});                                    // 中心
    t.cubicTo({0.35f, 0.15f}, {0.70f, 0.25f}, {1.0f, 0.0f});   // 上边 → 花瓣尖
    t.cubicTo({0.70f, -0.25f}, {0.35f, -0.15f}, {0.0f, 0.0f}); // 下边 → 回中心
    poly = t.close();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    for (auto &p : poly)
    {
        const float x = p.x;
        const float y = p.y;
        p.x = x * c - y * s;
        p.y = x * s + y * c;
    }
}

using mcs::vulkan::tool::resource_manager;

struct CameraInfo
{
    glm::mat4 view;
    glm::mat4 proj;
};
struct UniformBufferObject
{
    CameraInfo cameraInfo[2]; // 0: 3D, 1: UI
};
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

    // NOTE: 映射 数据 到 shader. 使用了下面的东西,注意用不到也不报错,我们开启了不报错的功能,这样
    // layout(set=0,binding=0), layout(set=0,binding=1),layout(set=0,binding=2)
    std::array<VkDescriptorBindingFlags, 3> bindingFlags = {
        // 绑定0：Uniform Buffer - 通常不需要绑定后更新
        0,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, //允许不连续占用纹理槽（如 1、3、5、33）
        0};

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
                         },
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
                         VkDescriptorPoolSize{
                             .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .descriptorCount = MAX_FRAMES_IN_FLIGHT,
                         },
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
                         },
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

//diff: [test_sdf_text] start: 移植自 test_dod18.cpp 的文字管线
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
//diff: [test_sdf_text] end

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

    BufferResourceWithAddress globalAttributeBuffer{}; // type 7 逐顶点属性池

    BufferResource indirectDrawBuffer{};
    BufferResourceWithAddress commandConstantsBuffer{};

    uint32_t drawCount = 0;
};
struct CommandConstant
{
    uint32_t type_id;
    uint32_t adddress_offset;
    uint32_t perInstanceAttributeCount; // type 7：每实例属性数
    uint32_t attributeOffset;           // type 7：属性池偏移（上传命令常量时解析）
};
constexpr uint32_t MAX_INSTANCE_COUNT = 1000;
constexpr uint32_t MAX_DRAW_CALLS = 32;
constexpr VkDeviceSize PATH_VERTEX_CAPACITY = 16 * 1024; // type 6 动态顶点（个）
constexpr VkDeviceSize PATH_INDEX_CAPACITY = 32 * 1024;  // type 6 动态索引（个）
constexpr auto initFrameResources(const LogicalDevice &device, const mesh_manager &m)
{
    auto &[allVertices, allIndices, meshMap] = m;
    std::array<FrameResources, MAX_FRAMES_IN_FLIGHT> frameResources;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        auto &batche = frameResources[i];
        {
            auto capacity = allVertices.size() * sizeof(Vertex) +
                            PATH_VERTEX_CAPACITY * sizeof(Vertex);
            batche.globalVertexBuffer =
                BufferResourceWithAddress{device, capacity,
                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                          VK_SHARING_MODE_EXCLUSIVE,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
            batche.globalVertexBuffer.write(0, allVertices.data(),
                                            allVertices.size() * sizeof(Vertex));
        }
        {
            auto capacity = allIndices.size() * sizeof(uint32_t) +
                            PATH_INDEX_CAPACITY * sizeof(uint32_t);
            batche.globalIndexBuffer =
                BufferResource{device, capacity, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               VK_SHARING_MODE_EXCLUSIVE,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
            batche.globalIndexBuffer.write(0, allIndices.data(),
                                           allIndices.size() * sizeof(uint32_t));
        }
        {
            constexpr VkDeviceSize MAX_INSTANCE_DATA_SIZE =
                MAX_DRAW_CALLS * MAX_INSTANCE_COUNT * 100;
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

            constexpr VkDeviceSize MAX_IndirectDraw_SIZE = 1 * 1024 * 1024;
            batche.indirectDrawBuffer = BufferResource{
                device, MAX_IndirectDraw_SIZE, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        }
        {
            // type 7 逐顶点属性池（与动态顶点等量）
            constexpr VkDeviceSize capacity =
                PATH_VERTEX_CAPACITY * sizeof(VertexAttribute);
            batche.globalAttributeBuffer =
                BufferResourceWithAddress{device, capacity,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
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

struct UIPushConstant
{
    uint64_t vertexAddress;
    uint64_t dataAddress;
    uint64_t commandConstantsAddress;
    uint64_t attributeAddress; // type 7 逐顶点属性池
    uint32_t cameraIndex;
};
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
         // R32G32B32_UINT 三分量不被 GPU 支持为颜色附件（VUID-02251），
         // 用 R32G32B32A32_UINT；片元输出 uvec4 与之严格对齐
         .format = VK_FORMAT_R32G32B32A32_UINT,
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
        constexpr auto BUFFER_SIZE =
            4 * sizeof(uint32_t); // R32G32B32A32_UINT 一个像素 = 16B
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
                                         .size = sizeof(UIPushConstant)}}})
            .build(device);

    using stage_info = create_graphics_pipeline::stage_info;
    // 定义两个颜色附件格式
    std::array<VkFormat, 2> mainColorFormats = {
        swapchainBuild.refImageFormat(), // location 0 (swapchain)
        VK_FORMAT_R32G32B32A32_UINT      // location 1 (picking)
    };

    auto makeUIPipeline = [&](VkPipelineCreateFlagBits flags,
                              VkPrimitiveTopology topology, VkPolygonMode polygonMode,
                              VkBool32 depthTest, VkBool32 depthWrite,
                              VkBool32 blendEnable,
                              std::vector<VkDynamicState> dynamicStates =
                                  {
                                      VK_DYNAMIC_STATE_VIEWPORT,
                                      VK_DYNAMIC_STATE_SCISSOR,
                                  },
                              VkPipeline basePipelineHandle = {},
                              int32_t basePipelineIndex = {}) {
        return create_graphics_pipeline{}
            .setCreateInfo({
                .pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                    {//diff: [test_indirectdraw_no_pick] start
                     .colorAttachmentCount =
                         static_cast<uint32_t>(mainColorFormats.size()),
                     .pColorAttachmentFormats = mainColorFormats.data(),
                     //diff: [test_indirectdraw_no_pick] end
                     .depthAttachmentFormat = depthFormat_ref}}),
                .flags = flags,
                .stages = create_graphics_pipeline::makeStages(
                    stage_info{.stage = VK_SHADER_STAGE_VERTEX_BIT,
                               .filePath = VERT_SHADER_PATH,
                               .pName = "main"},
                    stage_info{.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                               .filePath = FRAG_SHADER_PATH,
                               .pName = "main"}),
                .vertexInputState = {},
                .inputAssemblyState = {.topology = topology,
                                       .primitiveRestartEnable = VK_FALSE},
                .tessellationState = {},
                .viewportState = {.viewports = {VkViewport{}}, .scissors = {VkRect2D{}}},
                .rasterizationState = {.depthClampEnable = VK_FALSE,
                                       .rasterizerDiscardEnable = VK_FALSE,
                                       .polygonMode = polygonMode,
                                       .cullMode = VK_CULL_MODE_NONE,
                                       .frontFace = VK_FRONT_FACE_CLOCKWISE,
                                       .depthBiasEnable = VK_FALSE,
                                       .lineWidth = 1.0F},
                .multisampleState =
                    {
                        .rasterizationSamples = physical_device.getMaxUsableSampleCount(),
                        .sampleShadingEnable = VK_FALSE,
                        .minSampleShading = 1.0F,
                        .sampleMask = {},
                        .alphaToCoverageEnable = VK_FALSE,
                        .alphaToOneEnable = VK_FALSE,
                    },
                // 深度/模板状态: 片段着色器之后的固定操作. 控制丢弃片元
                .depthStencilState = {.depthTestEnable = depthTest,
                                      .depthWriteEnable = depthWrite,
                                      .depthCompareOp = VK_COMPARE_OP_LESS,
                                      .depthBoundsTestEnable = VK_FALSE,
                                      .stencilTestEnable = VK_FALSE,
                                      .front = {},
                                      .back = {},
                                      .minDepthBounds = 0.0F,
                                      .maxDepthBounds = 1.0F},
                // 颜色混合: 片段着色器之后的固定操作,控制画面颜色
                .colorBlendState =
                    {.logicOpEnable = VK_FALSE,
                     .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                     .attachments =
                         {
                             //附件0
                             {
                                 //diff: [test_dod12] start: 附件 0 – 开启混合，支持 MSDF 与普通纹理混合
                                 .blendEnable = blendEnable,
                                 .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                                 .dstColorBlendFactor =
                                     VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                 .colorBlendOp = VK_BLEND_OP_ADD,
                                 .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                 .dstAlphaBlendFactor =
                                     VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                 .alphaBlendOp = VK_BLEND_OP_ADD,
                                 .colorWriteMask =
                                     VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                                 //diff: [test_dod12] end
                             },
                             //diff: [test_indirectdraw_no_pick] start
                             {
                                 // location 1 (R32G32B32A32_UINT，不能混合)
                                 .blendEnable = VK_FALSE,
                                 .colorWriteMask =
                                     VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                             },
                             //diff: [test_indirectdraw_no_pick] end
                         }},
                .dynamicState = {.dynamicStates = std::move(dynamicStates)},
                .layout = *pipelineLayout,
                .renderPass = {},
                .subpass = {},
                .basePipelineHandle = basePipelineHandle,
                .basePipelineIndex = basePipelineIndex,
            })
            .build(device);
    };

    // 创建4个管线
    auto pipelineUITriangle = makeUIPipeline(
        VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_POLYGON_MODE_FILL, VK_FALSE, VK_FALSE, VK_TRUE,
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        });
    auto pipelineUIPoint = makeUIPipeline(
        VK_PIPELINE_CREATE_DERIVATIVE_BIT, VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        VK_POLYGON_MODE_FILL, VK_FALSE, VK_FALSE, VK_TRUE,
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        },
        *pipelineUITriangle, -1);
    auto pipelineUILine =
        makeUIPipeline(VK_PIPELINE_CREATE_DERIVATIVE_BIT, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                       VK_POLYGON_MODE_FILL, VK_FALSE, VK_FALSE, VK_TRUE,
                       {
                           VK_DYNAMIC_STATE_VIEWPORT,
                           VK_DYNAMIC_STATE_SCISSOR,
                           VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
                           VK_DYNAMIC_STATE_LINE_WIDTH,
                       },
                       *pipelineUITriangle, -1);
    return make_aggregate<"mainPipelineCtx", "pipelineLayout", "pipelineUITriangle",
                          "pipelineUIPoint", "pipelineUILine", "swapchainAttachments",
                          "pickingAttachments">(
        std::move(pipelineLayout), std::move(pipelineUITriangle),
        std::move(pipelineUIPoint), std::move(pipelineUILine),
        std::move(swapchainAttachments), std::move(pickingAttachments));
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

    auto inputDataCtx = inputInit(swapchain);
    auto &input = *inputDataCtx.input.get();
    auto &camera = inputDataCtx.camera;
    auto &uiCamera = inputDataCtx.uiCamera;
    auto &clock = inputDataCtx.clock;

    auto meshManager = initMeshManager();
    auto &[allVertices, allIndices, meshMap] = meshManager;

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

    // NOTE: 初始化全局顶点和缓冲区
    auto frameResources = initFrameResources(device, meshManager);

    auto mainPipelineCtx = initPipeline(hardwareCtx, descriptorCtx);
    auto &[pipelineLayout, pipelineUITriangle, pipelineUIPoint, pipelineUILine,
           swapchainAttachments, pickingAttachments] = mainPipelineCtx;
    auto &[depthResourcesBuild, depthResource, msaaResourcesBuild, msaaResource] =
        swapchainAttachments;
    auto &[pickResourcesBuild, pickResource, resolveResourcesBuild, resolveResource,
           pickingFrames, pickMouse] = pickingAttachments;

    struct record_info
    {
        uint32_t current_frame;
        uint32_t image_index;
    };
    auto recordCtx = make_aggregate<"recordCtx", "info">(record_info{});
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
        make_aggregate_ref<"mainCtx", "pipelineUITriangle", "pipelineUIPoint",
                           "pipelineUILine", "pipelineLayout", "depthResourcesBuild",
                           "depthResource", "msaaResourcesBuild", "msaaResource">(
            pipelineUITriangle, pipelineUIPoint, pipelineUILine, pipelineLayout,
            depthResourcesBuild, depthResource, msaaResourcesBuild, msaaResource);

    auto mainShaderCtx = make_aggregate_ref<"mainShaderCtx", "indirectDrawBatches",
                                            "uniformBuffers", "descriptorSets">(
        frameResources, uniformBuffers, descriptorSets);
    auto inputCtx =
        make_aggregate_ref<"inputCtx", "input", "camera", "uiCamera", "clock">(
            input, camera, uiCamera, clock);

    // NOTE: 流程面向对象,语义化
    auto mainPipeline = make_aggregate<"mainPipeline", "transitionImageLayout",
                                       "beginRendering", "setPipelineState", "draw",
                                       "endRendering">(
        [&] {
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
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}});
        },
        [&] {
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
                 .colorAttachmentCount = static_cast<uint32_t>(attachments.size()),
                 .pColorAttachments = attachments.data(),
                 .pDepthAttachment = &depthAttachment});
        },
        [&] {
            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto imageExtent = swapchain.imageExtent();
            auto &pipelineLayout = mainCtx.pipelineLayout;

            auto &frameResources = mainShaderCtx.indirectDrawBatches;
            auto &batch = frameResources[currentFrame];

            auto &descriptorSets = mainShaderCtx.descriptorSets;
            auto descriptorSet = descriptorSets[currentFrame];

            auto &uniformBuffers = mainShaderCtx.uniformBuffers;

            // 公共动态状态
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

            // 绑定公共资源：索引缓冲、描述符集、推送常量公共部分（如缓冲地址）
            commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             *pipelineLayout, 0, 1, &(descriptorSet), 0,
                                             nullptr);
            commandBuffer.bindIndexBuffer(batch.globalIndexBuffer.buffer.buffer(), 0,
                                          VK_INDEX_TYPE_UINT32);

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
        },
        [&] {
            // ============================================================
            // 前端 API：上传的 Rectangle 实例数据。
            // shader(test_sdf.vert/frag) 是后端，按这里的参数绘制；
            // 字段布局必须与 test_sdf.vert/frag 中的 struct Rectangle 完全一致。
            // ============================================================
            struct VertexTransform
            {
                glm::mat4 matrix;
            };
            struct UvTransform
            {
                glm::vec2 scale;
                glm::vec2 offset;
            };
            struct Rectangle
            {
                uint32_t entity_index; // 4B 拾取实体索引（outPicking.y）
                uint32_t effects;      // 4B 特效标志（见 FX_*，0=按值自动推断）
                // 颜色 = 实例数据：四顶点颜色（quad 网格固定 N=4；
                // 单色矩形 = 四顶点同色，彩色矩形 = 四顶点各色，圆角/阴影同样生效）
                std::array<glm::vec4, 4> colors; // 64B 每顶点颜色（最细粒度，含 alpha）
                glm::mat4 model;                 // 64B 平移 + 旋转
                VertexTransform vertexTransform; // 64B 顶点缩放（尺寸映射到公共顶点）
                UvTransform uvTransform;         // 16B UV 变换
                glm::vec2 size;                  // 8B 卡片完整宽/高（NDC，SDF 用）
                glm::vec2 shadowOffset;   // 8B 阴影偏移（相对卡片宽/高；x右 y下为正）
                glm::vec4 radiusSoftness; // 16B x=圆角 y=柔化 z=模糊 w=扩散
                glm::vec4 shadowColor;    // 16B 阴影色 RGBA（a=0 → 无阴影）
            };
            static_assert(sizeof(Rectangle) == 264); // 与 shader RECTANGLE_SIZE 一致

            // 特效标志（与 test_sdf.vert/frag 中的 FX_* 常量一致；追加式）
            constexpr uint32_t FX_ROUNDED = 1u; // 圆角
            constexpr uint32_t FX_SHADOW = 2u;  // 阴影
            constexpr uint32_t FX_FILL = 4u;    // 填充

            // 写入类型+命令
            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];
            auto &batch = mainShaderCtx.indirectDrawBatches[currentFrame];
            const auto &mesh = meshMap.at("quad"); // 使用 quad 网格

            // ---------- 1. 矩形构建器（前端 API，带标准默认参数）----------
            struct RectStyle
            {
                glm::vec4 fillColor{1.0f, 1.0f, 1.0f, 1.0f};   // 默认白色不透明
                glm::vec4 shadowColor{0.0f, 0.0f, 0.0f, 0.0f}; // 默认无阴影
                glm::vec2 shadowOffset{0.0f, 0.04f};           // 默认向下偏移 4% 卡片高
                float radius = 0.08f;        // 默认圆角（相对卡片短边比例）
                float edgeSoftness = 0.006f; // 默认边缘柔化（相对卡片宽比例）
                float shadowBlur = 0.08f;    // 默认阴影模糊（相对卡片宽比例）
                float shadowSpread = 1.0f;   // 默认阴影与卡片同大
                float rotation = 0.0f;       // 默认不旋转（度）
                uint32_t effects = 0u;       // 0=按 alpha/半径自动推断
            };
            auto makeRect = [&](glm::vec2 center, glm::vec2 size, RectStyle s) {
                Rectangle r{};
                r.colors = {s.fillColor, s.fillColor, s.fillColor, s.fillColor};
                r.model =
                    glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)),
                                glm::radians(s.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
                // 顶点缩放 = 卡片尺寸（quad 公共顶点是 [-0.5,0.5] 单位矩形）
                r.vertexTransform.matrix =
                    glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
                r.uvTransform = UvTransform{glm::vec2(1.0f), glm::vec2(0.0f)};
                r.size = size;
                r.shadowOffset = s.shadowOffset;
                r.radiusSoftness =
                    glm::vec4(s.radius, s.edgeSoftness, s.shadowBlur, s.shadowSpread);
                r.shadowColor = s.shadowColor;
                r.effects = s.effects;
                return r;
            };

            // ---------- 2. 准备实例数据（多实例、异构特效，全部参数化）----------
            std::vector<Rectangle> rectangles;
            const glm::vec4 WHITE{1.0f, 1.0f, 1.0f, 1.0f};

            // 追加矩形实例：entity_index 自动 = 当前实例数（vector size），
            // 与实例数据同步生成，无需手工维护
            auto addRect = [&](Rectangle r) {
                r.entity_index = static_cast<uint32_t>(rectangles.size());
                rectangles.push_back(r);
            };

            // [1] 整屏背景：白色 + 标准黑色阴影（HTML 页面效果）。
            //     说明：不透明填充会盖住自身阴影，这里的阴影主要作为参数演示，
            //     标准黑色阴影的视觉效果请看 [2][5][6]。
            addRect(makeRect({0.0f, 0.0f}, {2.0f, 2.0f},
                             {.fillColor = WHITE,
                              .shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f),
                              .shadowOffset = {0.0f, 0.015f},
                              .radius = 0.0f,
                              .shadowBlur = 0.015f}));

            // ==================== 视口效果演示（最小实现原理） ====================
            // 每个 view = {面板, 内容, scissor, 相机}。
            // 关键：setScissor(视图区域) → pushConstants(该视图命令常量基址+相机)
            //       → drawIndexedIndirect(该视图命令)。内容被裁剪在视图内，
            //       溢出部分（首行/末行半截）不会画出视图边界。
            // 动态：用运行时间驱动内容在视图内滑动（传送带式），
            //       颜色用 8 色调色板，保证辨识度。
            //
            // 【实现机制】
            // 1) 数据每帧重建：矩形实例的 y 坐标 = f(数据行号 i, 滚动偏移 scroll)，
            //    scroll 由时间算出 —— 这是"视图变换"（view transform）。
            //    原始数据（行号、颜色、基础位置）从未被修改；scroll=0 就是原始画面。
            // 2) scissor 裁剪：绘制每个 view 前 setScissor(视图矩形)，光栅化后
            //    视图外的片元被丢弃 —— 保证内容不出面板。
            //    注意：scissor 丢的是"片元"不是"顶点"。视口外的行照常上传、
            //    变换、光栅化，所以它带来的是"正确性"而不是性能。
            //    想省性能要在 CPU 侧只生成可见行（这里两段拷贝是故意全画）。
            // 3) 若做成"滑块"：scroll 从"时间函数"换成"滑块值"即可，
            //    数据输入不变，变的只是视图偏移。

            // 高辨识度调色板（红/橙/黄/绿/青/蓝/紫/粉）
            const std::array<glm::vec4, 8> palette = {
                glm::vec4(0.92f, 0.28f, 0.28f, 1.0f), // 红
                glm::vec4(0.96f, 0.58f, 0.12f, 1.0f), // 橙
                glm::vec4(0.92f, 0.80f, 0.18f, 1.0f), // 黄
                glm::vec4(0.32f, 0.80f, 0.38f, 1.0f), // 绿
                glm::vec4(0.16f, 0.75f, 0.72f, 1.0f), // 青
                glm::vec4(0.28f, 0.48f, 0.95f, 1.0f), // 蓝
                glm::vec4(0.62f, 0.36f, 0.92f, 1.0f), // 紫
                glm::vec4(0.92f, 0.38f, 0.70f, 1.0f), // 粉
            };
            const glm::vec4 panelDark{0.13f, 0.15f, 0.21f, 1.0f};
            const glm::vec4 panelLight{0.96f, 0.97f, 0.99f, 1.0f};
            const float elapsed = inputCtx.clock.getElapsed(); // 程序运行秒数

            struct ViewRect
            {
                glm::vec2 center;
                glm::vec2 size;
            };

            std::vector<ViewRect> views;         // 视图矩形（NDC）
            std::vector<uint32_t> viewContentStart; // 内容在 rectangles 中的起点
            std::vector<uint32_t> viewContentCount; // 内容数量（含面板）
            std::vector<VkRect2D> viewScissors;  // NDC → 像素
            std::vector<bool> viewClip;          // 是否启用裁剪

            auto beginView = [&](ViewRect v) {
                views.push_back(v);
                viewContentStart.push_back(static_cast<uint32_t>(rectangles.size()));
                viewContentCount.push_back(0);
                // NDC（UI 相机 y 向下）→ 像素（scissor y 向下，一致）
                const auto ext = swapchain.imageExtent();
                const auto px = [&](float n, uint32_t full) {
                    return static_cast<int32_t>((n * 0.5f + 0.5f) *
                                                static_cast<float>(full));
                };
                const int x0 = px(v.center.x - v.size.x * 0.5f, ext.width);
                const int y0 = px(v.center.y - v.size.y * 0.5f, ext.height);
                const int x1 = px(v.center.x + v.size.x * 0.5f, ext.width);
                const int y1 = px(v.center.y + v.size.y * 0.5f, ext.height);
                viewScissors.push_back(VkRect2D{{x0, y0},
                                                {static_cast<uint32_t>(x1 - x0),
                                                 static_cast<uint32_t>(y1 - y0)}});
                viewClip.push_back(true);
            };
            auto endView = [&] {
                viewContentCount.back() =
                    static_cast<uint32_t>(rectangles.size()) - viewContentStart.back();
            };

            // ---- view 1：list_view（带 scissor，纵向传送带滑动）----
            // 数据行 24 条，视图只显示其中一段；滚动时首/末行在边界被裁，
            // 不会画出面板。两段拷贝（rep=0/1）实现无缝循环。
            beginView({{-0.62f, 0.0f}, {0.74f, 1.62f}});
            addRect(makeRect({-0.62f, 0.0f}, {0.74f, 1.62f},
                             {.fillColor = panelDark, .radius = 0.03f}));
            {
                constexpr float GAP = 0.105f;
                constexpr int COUNT = 24;
                const float scroll = std::fmod(elapsed * 0.22f, float(COUNT) * GAP);
                for (int rep = 0; rep < 2; rep++)
                {
                    for (int i = 0; i < COUNT; i++)
                    {
                        const float y = -0.83f - scroll + float(i + rep * COUNT) * GAP;
                        addRect(makeRect({-0.62f, y}, {0.66f, 0.09f},
                                         {.fillColor = palette[i % 8],
                                          .radius = 0.012f}));
                    }
                }
            }
            endView();

            // ---- view 2：table_view（带 scissor，横向传送带滑动）----
            // 数据列 20 列 × 7 行；列向左滑动，视图只显示其中 6 列左右。
            beginView({{0.52f, -0.36f}, {0.88f, 0.84f}});
            addRect(makeRect({0.52f, -0.36f}, {0.88f, 0.84f},
                             {.fillColor = panelDark, .radius = 0.02f}));
            {
                constexpr float CW = 0.13f; // 列宽
                constexpr float CH = 0.085f; // 行高
                constexpr int COLS = 20;
                constexpr int ROWS = 7;
                const float scroll = std::fmod(elapsed * 0.18f, float(COLS) * CW);
                for (int rep = 0; rep < 2; rep++)
                {
                    for (int c = 0; c < COLS; c++)
                    {
                        for (int r = 0; r < ROWS; r++)
                        {
                            const float x = 0.11f - scroll + float(c + rep * COLS) * CW;
                            const float y = -0.78f + float(r) * CH;
                            const bool even = ((c + r) % 2 == 0);
                            const glm::vec4 base = palette[c % 8];
                            const glm::vec4 cell = even
                                                       ? base
                                                       : glm::vec4(glm::vec3(base) * 0.55f,
                                                                   1.0f);
                            addRect(makeRect({x, y}, {0.115f, 0.075f},
                                             {.fillColor = cell, .radius = 0.008f}));
                        }
                    }
                }
            }
            endView();

            // ---- view 3：对照（不裁剪 → 行溢出面板，半截 UI 外漏）----
            // 同样的滚动内容，不开 scissor：行滑出面板后直接画在背景上。
            beginView({{0.52f, 0.60f}, {0.70f, 0.48f}});
            addRect(makeRect({0.52f, 0.60f}, {0.70f, 0.48f},
                             {.fillColor = panelLight, .radius = 0.02f}));
            {
                constexpr float GAP = 0.10f;
                const float scroll = std::fmod(elapsed * 0.28f, 4.0f * GAP);
                for (int i = 0; i < 8; i++)
                {
                    const float y = 0.40f - scroll + float(i) * GAP; // 越出面板底部
                    addRect(makeRect({0.52f, y}, {0.62f, 0.085f},
                                     {.fillColor = glm::vec4(0.95f, 0.30f, 0.30f, 1.0f),
                                      .radius = 0.012f}));
                }
            }
            endView();
            viewClip.back() = false; // 故意不裁剪 → 行溢出可见

            // ==================== 数据上传 ====================
            // 实例堆：bg + 每个 view 一段（面板 + 内容），全部 type 0（Rectangle）
            const VkDeviceSize rectBytes = rectangles.size() * sizeof(Rectangle);
            batch.globalInstanceBuffer.write(0, rectangles.data(), rectBytes);

            // 命令布局：cmd 0 = bg；每个 view 两条命令（面板 + 内容）
            std::vector<VkDrawIndexedIndirectCommand> drawCommands;
            std::vector<CommandConstant> cmdConsts;
            auto pushCommand = [&](uint32_t firstRect, uint32_t count) {
                drawCommands.push_back(
                    {.indexCount = mesh.indexCount,
                     .instanceCount = count,
                     .firstIndex = mesh.indexOffset,
                     .vertexOffset = static_cast<int32_t>(mesh.vertexOffset),
                     .firstInstance = 0});
                cmdConsts.push_back(
                    {.type_id = 0,
                     .adddress_offset =
                         static_cast<uint32_t>(firstRect * sizeof(Rectangle))});
            };

            pushCommand(0, 1); // bg
            for (size_t v = 0; v < views.size(); v++)
            {
                pushCommand(viewContentStart[v], 1);   // 面板
                pushCommand(viewContentStart[v] + 1,
                            viewContentCount[v] - 1); // 内容
            }

            batch.indirectDrawBuffer.write(0, drawCommands.data(),
                                           drawCommands.size() *
                                               sizeof(VkDrawIndexedIndirectCommand));
            batch.commandConstantsBuffer.write(0, cmdConsts.data(),
                                               cmdConsts.size() *
                                                   sizeof(CommandConstant));

            // ==================== 处理（绘制） ====================
            // 主 push 常量：bg 用命令常量 [0]
            UIPushConstant pc{.vertexAddress = batch.globalVertexBuffer.address,
                              .dataAddress = batch.globalInstanceBuffer.address,
                              .commandConstantsAddress =
                                  batch.commandConstantsBuffer.address,
                              .attributeAddress = batch.globalAttributeBuffer.address,
                              .cameraIndex = 1}; // UI 相机
            commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       *mainCtx.pipelineUITriangle);
            commandBuffer.setPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            const auto imageExtent = swapchain.imageExtent();
            constexpr VkDeviceSize CMD_STRIDE = sizeof(VkDrawIndexedIndirectCommand);

            // bg：全屏 scissor
            commandBuffer.setScissor(
                0, std::array<VkRect2D, 1>{VkRect2D{{0, 0}, imageExtent}});
            commandBuffer.pushConstants(*mainCtx.pipelineLayout,
                                        VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(UIPushConstant), &pc);
            commandBuffer.drawIndexedIndirect(batch.indirectDrawBuffer.buffer.buffer(),
                                              0, 1, CMD_STRIDE);

            // 每 view：scissor（或全屏对照）→ 命令常量基址 → 绘制面板 + 内容。
            // scissor 在光栅化后按矩形丢弃片元：视口外的像素被丢弃（不是跳过绘制），
            // 性能无提升，只保证"内容不出视图边界"这个正确性。
            for (size_t v = 0; v < views.size(); v++)
            {
                if (viewClip[v])
                    commandBuffer.setScissor(
                        0, std::array<VkRect2D, 1>{viewScissors[v]});
                else
                    commandBuffer.setScissor(
                        0, std::array<VkRect2D, 1>{VkRect2D{{0, 0}, imageExtent}});

                UIPushConstant pcView = pc;
                pcView.commandConstantsAddress =
                    batch.commandConstantsBuffer.address +
                    (1 + 2 * v) * sizeof(CommandConstant);
                commandBuffer.pushConstants(*mainCtx.pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT, 0,
                                            sizeof(UIPushConstant), &pcView);
                commandBuffer.drawIndexedIndirect(
                    batch.indirectDrawBuffer.buffer.buffer(),
                    (1 + 2 * v) * CMD_STRIDE, 2, CMD_STRIDE);
            }
        },
        [&] {
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
            }
        });
    auto recordCommandBuffer = [&] {
        auto [currentFrame, imageIndex] = recordCtx.info;
        const auto &commandBuffer = commandBuffers[currentFrame];
        VkImage image = swapchain.image(imageIndex);

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        mainPipeline.transitionImageLayout();
        mainPipeline.beginRendering();
        mainPipeline.setPipelineState();
        mainPipeline.draw();
        mainPipeline.endRendering();

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

    auto recreateSwapChain = [&] {
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

        frameContext.rebuild(swapchain.imagesSize());
    };

    struct object_key
    {
        uint32_t object_type;  // 对象类型
        uint32_t entity_index; // 实体索引
    };
    struct picking_result
    {
        object_key key;        // 对应 outPicking.xy
        uint32_t primitive_id; // 对应 outPicking.z
        uint32_t reserved;     // 对应 outPicking.w（附件第 4 分量，恒 0）
    };
    // picking_result 直接映射着色器输出的 uvec4(object_type, entity_index, primitive_id, 0)
    static_assert(sizeof(picking_result) == 4 * sizeof(uint32_t));
    auto drawFrame = [&] {
        auto &inFlightFences = frameContext.inFlightFences;
        auto &currentFrame = frameContext.currentFrame;
        auto &presentCompleteSemaphore = frameContext.presentCompleteSemaphore;
        auto &semaphoreIndex = frameContext.semaphoreIndex;
        auto &renderFinishedSemaphore = frameContext.renderFinishedSemaphore;

        while (device.waitForFences(1, inFlightFences[currentFrame], VK_TRUE,
                                    UINT64_MAX) == VK_TIMEOUT)
            ;

        {
            auto &pickMouse = pickCtx.mouse;
            auto &pickingFrames = pickCtx.frames;

            // 等待栅栏后，正式获取图像前
            if (currentFrame > 0 && pickMouse.valid)
            {
                uint32_t readIdx =
                    (currentFrame - 1 + MAX_FRAMES_IN_FLIGHT) % MAX_FRAMES_IN_FLIGHT;
                //diff: [test_dod16] start
                //  NOTE: 应该提取出来的
                auto *data =
                    static_cast<picking_result *>(pickingFrames[readIdx].mapPtr());
                if (data->key.object_type != 0xFFFFFFFF)
                {
                    uint32_t type = data->key.object_type;
                    uint32_t idx = data->key.entity_index;
                    uint32_t prim = data->primitive_id;
                    std::cout << "object_type: " << type << " , entity_index: " << idx
                              << " , primitive_id: " << prim << '\n';
                }
                //diff: [test_dod16] end
            }
        }

        auto [result, imageIndex] = swapchain.acquireNextImage(
            UINT64_MAX, presentCompleteSemaphore[semaphoreIndex], nullptr);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            recreateSwapChain();
            return;
        }
        if (result != VK_SUCCESS)
            throw std::runtime_error("failed to acquire swap chain image!");
        device.resetFences(1, inFlightFences[currentFrame]);

        const auto &commandBuffer = commandBuffers[currentFrame];
        commandBuffer.reset({});
        recordCtx.info = {.current_frame = currentFrame, .image_index = imageIndex};
        recordCommandBuffer();

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
            recreateSwapChain();
        }
        semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphore.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    };
    while (globalCtx.window.shouldClose() == 0)
    {
        {
            Window::pollEvents();

            auto cur = input.cursorPos();
            auto ext = swapchain.refImageExtent();
            if (cur.xpos >= 0 && cur.xpos < static_cast<int>(ext.width) &&
                cur.ypos >= 0 && cur.ypos < static_cast<int>(ext.height))
            {
                pickMouse.pos = {cur.xpos, cur.ypos};
                pickMouse.valid = true;
            }
            else
            {
                pickMouse.valid = false;
            }
        }
        drawFrame();
    }
    device.waitIdle();
    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}
// NOLINTEND
