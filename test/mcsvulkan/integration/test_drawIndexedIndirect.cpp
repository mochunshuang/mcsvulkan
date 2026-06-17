#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>
#include <random>
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

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_indirect_instanced";

// 简单的 Host 可见缓冲区创建辅助函数
template <typename T>
auto createHostBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                      VkBufferUsageFlags usage, const T *data = nullptr)
{
    VkBuffer buffer;
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, buffer, &memReqs);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    uint32_t memTypeIndex = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((memReqs.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            memTypeIndex = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = memTypeIndex,
    };
    VkDeviceMemory memory;
    vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    vkBindBufferMemory(device, buffer, memory, 0);

    void *mapped;
    vkMapMemory(device, memory, 0, size, 0, &mapped);

    if (data)
    {
        memcpy(mapped, data, size);
    }

    return std::make_tuple(buffer, memory, mapped);
}

// 每个实例的数据布局（与着色器中的 per-instance 输入对应）
struct InstanceData
{
    float offsetX, offsetY; // 位置偏移 (location = 1)
    float r, g, b;          // 颜色 (location = 2)
};

// 图像布局转换（原框架提供）
struct my_render
{
    constexpr static void transition_image_layout(
        const CommandBufferView &commandBuffer, VkImage image,
        VkImageAspectFlags aspectMask, VkImageLayout oldLayout, VkImageLayout newLayout,
        VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask,
        VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask) noexcept
    {
        VkImageMemoryBarrier2 barrier = {.sType = sType<VkImageMemoryBarrier2>(),
                                         .srcStageMask = srcStageMask,
                                         .srcAccessMask = srcAccessMask,
                                         .dstStageMask = dstStageMask,
                                         .dstAccessMask = dstAccessMask,
                                         .oldLayout = oldLayout,
                                         .newLayout = newLayout,
                                         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .image = image,
                                         .subresourceRange = {.aspectMask = aspectMask,
                                                              .baseMipLevel = 0,
                                                              .levelCount = 1,
                                                              .baseArrayLayer = 0,
                                                              .layerCount = 1}};
        VkDependencyInfo dep = {.sType = sType<VkDependencyInfo>(),
                                .imageMemoryBarrierCount = 1,
                                .pImageMemoryBarriers = &barrier};
        commandBuffer.pipelineBarrier2(dep);
    }
};

int main()
try
{
    // 着色器路径（由构建系统定义）
#ifdef VERT_SHADER_PATH
    std::cout << "VERT_SHADER_PATH: " << VERT_SHADER_PATH << '\n';
#endif
#ifdef FRAG_SHADER_PATH
    std::cout << "FRAG_SHADER_PATH: " << FRAG_SHADER_PATH << '\n';
#endif

    raii_vulkan ctx{};
    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE);

    constexpr auto APIVERSION = VK_API_VERSION_1_4;
    auto enables = enable_intance_build{}
                       .enableDebugExtension()
                       .enableValidationLayer()
                       .enableSurfaceExtension<surface>();
    enables.check();
    enables.print();

    Instance instance =
        create_instance{}
            .setCreateInfo(
                {.applicationInfo = {.pApplicationName = "Indirect Instanced",
                                     .applicationVersion = vkMakeVersion(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = vkMakeVersion(1, 0, 0),
                                     .apiVersion = APIVERSION},
                 .enabledLayers = enables.enabledLayers(),
                 .enabledExtensions = enables.enabledExtensions()})
            .build();
    // 加载实例级函数指针
    volkLoadInstance(*instance);

    auto debuger = create_debugger{}
                       .setCreateInfo(create_debugger::defaultCreateInfo())
                       .build(instance);

    std::vector<const char *> requiredDeviceExt = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
    };

    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        featureChain = {{.features =
                             {
                                 .multiDrawIndirect = VK_TRUE, //diff: [test_indirectdraw]
                             }},
                        {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
                        {.extendedDynamicState = VK_TRUE}};

    auto [id, physical_device] =
        physical_device_selector{instance}
            .requiredDeviceExtension(requiredDeviceExt)
            .requiredProperties([](const VkPhysicalDeviceProperties &props) {
                return props.apiVersion >= VK_API_VERSION_1_3;
            })
            .requiredQueueFamily([](const VkQueueFamilyProperties &qfp) {
                return !!(qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT);
            })
            .requiredFeatures([](const PhysicalDevice &phys) {
                // auto query =
                //     structure_chain<VkPhysicalDeviceFeatures2,
                //                     VkPhysicalDeviceVulkan13Features,
                //                     VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>{};
                // phys.getFeatures2(&query.head());
                // auto &v13 = query.template get<VkPhysicalDeviceVulkan13Features>();
                // auto &ext =
                //     query.template get<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>();
                // return v13.dynamicRendering && v13.synchronization2 &&
                //        ext.extendedDynamicState;
                return true;
            })
            .select()[0];

    mcs::vulkan::surface auto surface = surface_impl(physical_device, window);
    const uint32_t GRAPHICS_QUEUE_FAMILY_IDX =
        queue_family_index_selector{physical_device}
            .requiredQueueFamily([&](const VkQueueFamilyProperties &qfp, uint32_t idx) {
                return (qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                       physical_device.getSurfaceSupportKHR(idx, *surface);
            })
            .select()[0];

    LogicalDevice device =
        create_logical_device{}
            .setCreateInfo(
                {.pNext = make_pNext(featureChain),
                 .queueCreateInfos = create_logical_device::makeQueueCreateInfos(
                     create_logical_device::queue_create_info{
                         .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX,
                         .queueCount = 1,
                         .queuePrioritie = 1.0}),
                 .enabledExtensions = requiredDeviceExt})
            .build(physical_device);
    // 加载设备级函数指针
    volkLoadDevice(*device);

    requiredDeviceExt.clear();

    const auto GRAPHICS_AND_PRESENT = Queue(
        device, {.queue_family_index = GRAPHICS_QUEUE_FAMILY_IDX, .queue_index = 0});

    // ---------- 创建全局几何体 ----------
    const std::vector<float> quadVertices = {-0.5f, -0.5f, 0.5f,  -0.5f,
                                             0.5f,  0.5f,  -0.5f, 0.5f};
    const std::vector<uint32_t> quadIndices = {0, 1, 2, 0, 2, 3};

    // 实例数量
    constexpr uint32_t INSTANCE_COUNT = 1000000;
    // #define BATCH_ONLY
    // #define BATCH_BUFFER

#ifdef BATCH_ONLY
    constexpr uint32_t BATCH_SIZE = 100000; // 每个间接命令的实例数
    constexpr uint32_t DRAW_COUNT = (INSTANCE_COUNT + BATCH_SIZE - 1) / BATCH_SIZE;
#endif //

    // 缓冲区句柄
    VkBuffer vertexBuffer, indexBuffer, instanceBuffer, indirectBuffer;
    VkDeviceMemory vertexMemory, indexMemory, instanceMemory, indirectMemory;

    // 创建顶点缓冲
    {
        VkDeviceSize size = quadVertices.size() * sizeof(float);
        std::tie(vertexBuffer, vertexMemory, std::ignore) = createHostBuffer<float>(
            *device, *physical_device, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            quadVertices.data());
    }
    // 创建索引缓冲
    {
        VkDeviceSize size = quadIndices.size() * sizeof(uint32_t);
        std::tie(indexBuffer, indexMemory, std::ignore) = createHostBuffer<uint32_t>(
            *device, *physical_device, size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            quadIndices.data());
    }
    // 创建实例数据缓冲（作为顶点输入，binding=1, per-instance）
    {
        VkDeviceSize size = INSTANCE_COUNT * sizeof(InstanceData);
        void *mapped;
        std::tie(instanceBuffer, instanceMemory, mapped) = createHostBuffer<InstanceData>(
            *device, *physical_device, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, nullptr);
        // 填充随机数据
        auto *instances = static_cast<InstanceData *>(mapped);
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> posDist(-400.0f, 400.0f);
        std::uniform_real_distribution<float> colDist(0.0f, 1.0f);
        for (uint32_t i = 0; i < INSTANCE_COUNT; ++i)
        {
            instances[i].offsetX = posDist(rng);
            instances[i].offsetY = posDist(rng);
            instances[i].r = colDist(rng);
            instances[i].g = colDist(rng);
            instances[i].b = colDist(rng);
        }
    }
#ifdef BATCH_ONLY
#ifdef BATCH_BUFFER
    // 数组存放 DRAW_COUNT 个 VkBuffer + VkDeviceMemory
    std::vector<VkBuffer> indirectBuffers(DRAW_COUNT);
    std::vector<VkDeviceMemory> indirectMemories(DRAW_COUNT);

    for (uint32_t i = 0; i < DRAW_COUNT; ++i)
    {
        VkDrawIndexedIndirectCommand cmd;
        cmd.indexCount = static_cast<uint32_t>(quadIndices.size());
        cmd.instanceCount = std::min(BATCH_SIZE, INSTANCE_COUNT - i * BATCH_SIZE);
        cmd.firstIndex = 0;
        cmd.vertexOffset = 0;
        cmd.firstInstance = i * BATCH_SIZE;

        void *mapped;
        std::tie(indirectBuffers[i], indirectMemories[i], mapped) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, sizeof(VkDrawIndexedIndirectCommand),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                &cmd); // 直接填充
    }
#else
    // 创建间接命令缓冲（多条命令）
    {
        VkDeviceSize size = DRAW_COUNT * sizeof(VkDrawIndexedIndirectCommand);
        void *mapped;
        std::tie(indirectBuffer, indirectMemory, mapped) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                nullptr);
        auto *cmds = static_cast<VkDrawIndexedIndirectCommand *>(mapped);
        for (uint32_t i = 0; i < DRAW_COUNT; ++i)
        {
            cmds[i].indexCount = static_cast<uint32_t>(quadIndices.size());
            cmds[i].instanceCount = std::min(BATCH_SIZE, INSTANCE_COUNT - i * BATCH_SIZE);
            cmds[i].firstIndex = 0;
            cmds[i].vertexOffset = 0;
            cmds[i].firstInstance = i * BATCH_SIZE; // 实例偏移
        }
    }
#endif //BATCH_BUFFER

#else
    // 创建间接命令缓冲
    {
        VkDeviceSize size = sizeof(VkDrawIndexedIndirectCommand);
        void *mapped;
        std::tie(indirectBuffer, indirectMemory, mapped) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                nullptr);
        auto &cmd = *static_cast<VkDrawIndexedIndirectCommand *>(mapped);
        cmd.indexCount = static_cast<uint32_t>(quadIndices.size()); // 6
        cmd.instanceCount = INSTANCE_COUNT;
        cmd.firstIndex = 0;
        cmd.vertexOffset = 0;
        cmd.firstInstance = 0;
    }
#endif

    // ---------- 管线 ----------
    // 1. 顶点输入状态（两个绑定：顶点 + 实例数据）

    // 2. 管线布局（只需推送常量，无需描述符集）
    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(float) * 2,
    };
    VkPipelineLayout pipelineLayout;
    {
        VkPipelineLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 0,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushRange,
        };
        vkCreatePipelineLayout(*device, &layoutInfo, nullptr, &pipelineLayout);
    }

    // 3. 交换链、图形管线
    auto swapchainBuild =
        create_swap_chain{surface, device}
            .setCreateInfo(
                {.changeMinImageCount = [](uint32_t min) noexcept { return min + 1; },
                 .candidateSurfaceFormats = {{.format = VK_FORMAT_B8G8R8A8_SRGB,
                                              .colorSpace =
                                                  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}},
                 .imageArrayLayers = 1,
                 .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                 .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
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

    using stage_info = create_graphics_pipeline::stage_info;
    auto graphicsPipeline =
        create_graphics_pipeline{}
            .setCreateInfo({
                .pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                    {.colorAttachmentCount = 1,
                     .pColorAttachmentFormats = &swapchainBuild.refImageFormat()}}),
                .stages = create_graphics_pipeline::makeStages(
                    stage_info{.stage = VK_SHADER_STAGE_VERTEX_BIT,
                               .filePath = VERT_SHADER_PATH,
                               .pName = "main"},
                    stage_info{.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                               .filePath = FRAG_SHADER_PATH,
                               .pName = "main"}),
                .vertexInputState =
                    {.vertexBindingDescriptions = {{
                         {0, 2 * sizeof(float),
                          VK_VERTEX_INPUT_RATE_VERTEX}, // binding 0: 位置
                         {1, sizeof(InstanceData),
                          VK_VERTEX_INPUT_RATE_INSTANCE} // binding 1: 实例数据
                     }},
                     .vertexAttributeDescriptions = {{
                         {0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, // location 0: 顶点位置
                         {1, 1, VK_FORMAT_R32G32_SFLOAT,
                          offsetof(InstanceData, offsetX)}, // location 1: 偏移
                         {2, 1, VK_FORMAT_R32G32B32_SFLOAT,
                          offsetof(InstanceData, r)} // location 2: 颜色
                     }}},                            // <-- 使用我们的顶点输入状态
                .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
                .viewportState = {.viewports = {VkViewport{}}, .scissors = {VkRect2D{}}},
                .rasterizationState = {.polygonMode = VK_POLYGON_MODE_FILL,
                                       .cullMode = VK_CULL_MODE_NONE,
                                       .frontFace = VK_FRONT_FACE_CLOCKWISE,
                                       .lineWidth = 1.0f},
                .multisampleState = {.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT},
                .colorBlendState = {.logicOpEnable = VK_FALSE,
                                    .attachments = {{.blendEnable = VK_FALSE,
                                                     .colorWriteMask =
                                                         VK_COLOR_COMPONENT_R_BIT |
                                                         VK_COLOR_COMPONENT_G_BIT |
                                                         VK_COLOR_COMPONENT_B_BIT |
                                                         VK_COLOR_COMPONENT_A_BIT}}},
                .dynamicState = {.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                   VK_DYNAMIC_STATE_SCISSOR}},
                .layout = pipelineLayout,
            })
            .build(device);

    // 命令池、帧上下文
    CommandPool commandPool =
        create_command_pool{}
            .setCreateInfo({.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                            .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX})
            .build(device);

    constexpr auto MAX_FRAMES_IN_FLIGHT = 2;
    CommandBuffers commandBuffers =
        commandPool.allocateCommandBuffers({.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                            .commandBufferCount = MAX_FRAMES_IN_FLIGHT});
    frame_context<MAX_FRAMES_IN_FLIGHT> frameContext{device, swapchain.imagesSize()};

    // 录制命令回调
    const auto recordCommandBuffer = [&](const CommandBufferView &cmdBuffer,
                                         uint32_t imageIndex) {
        VkImage image = swapchain.image(imageIndex);
        VkImageView imageView = swapchain.imageView(imageIndex);
        auto extent = swapchain.imageExtent();

        cmdBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        my_render::transition_image_layout(
            cmdBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {},
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkClearValue clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}};
        VkRenderingAttachmentInfo colorAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = imageView,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clearValue};
        cmdBuffer.beginRendering({
            .sType = sType<VkRenderingInfo>(),
            .renderArea = {.offset = {0, 0}, .extent = extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
        });

        // 推送常量：窗口尺寸
        float windowSize[2] = {static_cast<float>(800), static_cast<float>(600)};
        cmdBuffer.pushConstants(pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                sizeof(windowSize), windowSize);

        cmdBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *graphicsPipeline);

        cmdBuffer.setViewport(0, std::array<VkViewport, 1>{VkViewport{
                                     .x = 0.0F,
                                     .y = 0.0F,
                                     .width = static_cast<float>(extent.width),
                                     .height = static_cast<float>(extent.height),
                                     .minDepth = 0.0F,
                                     .maxDepth = 1.0F}});
        cmdBuffer.setScissor(0, std::array<VkRect2D, 1>{VkRect2D{
                                    .offset = {.x = 0, .y = 0}, .extent = extent}});

        // 绑定两个顶点缓冲区：binding 0 顶点数据，binding 1 实例数据
        cmdBuffer.bindVertexBuffers(0, 1, vertexBuffer, 0);   // binding 0
        cmdBuffer.bindVertexBuffers(1, 1, instanceBuffer, 0); // binding 1

        // 索引缓冲
        cmdBuffer.bindIndexBuffer(indexBuffer, 0, VK_INDEX_TYPE_UINT32);
#ifdef BATCH_ONLY
#ifdef BATCH_BUFFER
        // NOTE: 少了 10 FPS左右。 几乎没区别？
        // 380 上下浮动
        for (uint32_t i = 0; i < DRAW_COUNT; ++i)
        {
            cmdBuffer.drawIndexedIndirect(indirectBuffers[i], // 每个不同的 buffer
                                          0,                  // 偏移为 0
                                          1,                  // 每次只画一条命令
                                          sizeof(VkDrawIndexedIndirectCommand));
        }
#else
        // 390浮动
        cmdBuffer.drawIndexedIndirect(indirectBuffer, 0, DRAW_COUNT,
                                      sizeof(VkDrawIndexedIndirectCommand));
#endif //BATCH_BUFFER

#else
        // 390浮动
        // 间接绘制：只调用一次，10000个实例
        cmdBuffer.drawIndexedIndirect(indirectBuffer, 0, 1,
                                      sizeof(VkDrawIndexedIndirectCommand));
#endif
        cmdBuffer.endRendering();

        my_render::transition_image_layout(
            cmdBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
        cmdBuffer.end();
    };

    // 交换链重建和帧循环（与原始代码相同）
    const auto recreateSwapChain = [&]() {
        surface.waitGoodFramebufferSize();
        device.waitIdle();
        swapchain.destroy();
        swapchain = swapchainBuild.rebuild();
    };

    const auto drawFrame = [&]() {
        auto &inFlightFences = frameContext.inFlightFences;
        auto &currentFrame = frameContext.currentFrame;
        auto &presentSemaphore = frameContext.presentCompleteSemaphore;
        auto &semIdx = frameContext.semaphoreIndex;
        auto &renderSemaphore = frameContext.renderFinishedSemaphore;

        while (device.waitForFences(1, inFlightFences[currentFrame], VK_TRUE,
                                    UINT64_MAX) == VK_TIMEOUT)
            ;

        auto [result, imageIndex] =
            swapchain.acquireNextImage(UINT64_MAX, presentSemaphore[semIdx], nullptr);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapChain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("failed to acquire swap chain image!");

        device.resetFences(1, inFlightFences[currentFrame]);

        const auto &cmdBuffer = commandBuffers[currentFrame];
        cmdBuffer.reset({});
        recordCommandBuffer(cmdBuffer, imageIndex);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        GRAPHICS_AND_PRESENT.submit(1,
                                    {.sType = sType<VkSubmitInfo>(),
                                     .waitSemaphoreCount = 1,
                                     .pWaitSemaphores = &presentSemaphore[semIdx],
                                     .pWaitDstStageMask = &waitStage,
                                     .commandBufferCount = 1,
                                     .pCommandBuffers = &*cmdBuffer,
                                     .signalSemaphoreCount = 1,
                                     .pSignalSemaphores = &renderSemaphore[imageIndex]},
                                    inFlightFences[currentFrame]);

        result = GRAPHICS_AND_PRESENT.presentKHR({
            .sType = sType<VkPresentInfoKHR>(),
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderSemaphore[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &(*swapchain),
            .pImageIndices = &imageIndex,
        });
        if (window.refFramebufferResized() || result == VK_ERROR_OUT_OF_DATE_KHR ||
            result == VK_SUBOPTIMAL_KHR)
        {
            window.refFramebufferResized() = false;
            recreateSwapChain();
        }
        semIdx = (semIdx + 1) % presentSemaphore.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    };

    // 主循环
    while (window.shouldClose() == 0)
    {
        surface::pollEvents();
        drawFrame();
        {
            // ---------- 帧数统计 ----------
            static auto lastTime = std::chrono::high_resolution_clock::now();
            static int frameCount = 0;

            frameCount++;
            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(now - lastTime).count();
            if (elapsed >= 1.0f)
            {
                float fps = frameCount / elapsed;
                std::println("FPS: {:.1f}", fps);
                frameCount = 0;
                lastTime = now;
            }
        }
    }
    device.waitIdle();

#ifdef BATCH_BUFFER
    for (uint32_t i = 0; i < indirectBuffers.size(); ++i)
    {
        vkDestroyBuffer(*device, indirectBuffers[i], nullptr);
        vkFreeMemory(*device, indirectMemories[i], nullptr);
    }
#endif //BATCH_BUFFER
    // 清理手动创建的缓冲
    vkDestroyBuffer(*device, vertexBuffer, nullptr);
    vkFreeMemory(*device, vertexMemory, nullptr);
    vkDestroyBuffer(*device, indexBuffer, nullptr);
    vkFreeMemory(*device, indexMemory, nullptr);
    vkDestroyBuffer(*device, instanceBuffer, nullptr);
    vkFreeMemory(*device, instanceMemory, nullptr);
    vkDestroyBuffer(*device, indirectBuffer, nullptr);
    vkFreeMemory(*device, indirectMemory, nullptr);
    vkDestroyPipelineLayout(*device, pipelineLayout, nullptr);

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}