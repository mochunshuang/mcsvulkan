#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>
#include <vector>

#include "../head.hpp"

#include <random>

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
constexpr auto TITLE = "test_my_triangle";
using mcs::vulkan::MCS_ASSERT;

// diff: [test_vertext] start
using mcs::vulkan::memory::create_buffer;
using mcs::vulkan::memory::create_staging_buffer;
using mcs::vulkan::memory::auto_map_buffer;
using mcs::vulkan::memory::find_memory_type_index;
using mcs::vulkan::tool::simple_copy_buffer;
struct Vertex // NOLINT
{
    glm::vec2 pos;

    static constexpr auto vertex_input_state() noexcept // NOLINT
    {
        return create_graphics_pipeline::vertex_input_state{
            .vertexBindingDescriptions = {{.binding = 0,
                                           .stride = sizeof(Vertex),
                                           .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}},
            .vertexAttributeDescriptions = {{.location = 0,
                                             .binding = 0,
                                             .format = VK_FORMAT_R32G32_SFLOAT,
                                             .offset = offsetof(Vertex, pos)}}};
    }
};
// diff: [test_vertext2] start
struct InstanceData
{
    glm::vec2 offset;
    glm::vec3 color; //NOTE: 迁移颜色到 SSBO
};
struct PushData
{
    uint64_t instance_address;
};
// diff: [test_vertext2] end

// diff: [test_vertext] end

struct my_render
{
    // NOLINTNEXTLINE
    constexpr static void transition_image_layout(
        const CommandBufferView &commandBuffer, VkImage image,
        VkImageAspectFlags aspectMask, VkImageLayout oldLayout, VkImageLayout newLayout,
        VkAccessFlags2 srcAccessMask,
        VkAccessFlags2 dstAccessMask, // NOLINT
        VkPipelineStageFlags2 srcStageMask,
        VkPipelineStageFlags2 dstStageMask // NOLINT
        ) noexcept
    {
        VkImageMemoryBarrier2 barrier = {
            .sType = sType<VkImageMemoryBarrier2>(),
            // Specify the pipeline stages and access masks for the barrier
            .srcStageMask = srcStageMask,
            .srcAccessMask = srcAccessMask,
            .dstStageMask = dstStageMask,
            .dstAccessMask = dstAccessMask,
            // Specify the old and new layouts of the image
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            // We are not changing the ownership between queues
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            // Specify the image to be affected by this barrier
            .image = image,
            // Define the subresource range (which parts of the image are affected)
            .subresourceRange = {.aspectMask = aspectMask,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};
        VkDependencyInfo dependency_info = {.sType = sType<VkDependencyInfo>(),
                                            .dependencyFlags = {},
                                            .imageMemoryBarrierCount = 1,
                                            .pImageMemoryBarriers = &barrier};
        commandBuffer.pipelineBarrier2(dependency_info);
    }
};

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
                                     // apiVersion必须是应用程序设计使用的Vulkan的最高版本
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
    requiredDeviceExtension.emplace_back(
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME); //diff: [test_vertext2] 推送buffer地址
    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan12Features,
                    VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        enablefeatureChain = {
            //diff: [test_vertext2] start: 着色器和draw 改成批量
            {.features = {.multiDrawIndirect = VK_TRUE, .shaderInt64 = VK_TRUE}},
            {.scalarBlockLayout = VK_TRUE, .bufferDeviceAddress = VK_TRUE},
            //diff: [test_vertext2] end
            {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
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
                auto query =
                    structure_chain<VkPhysicalDeviceFeatures2,
                                    VkPhysicalDeviceVulkan13Features,
                                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>{
                        {}, {}, {}};
                physicalDevice.getFeatures2(&query.head());
                auto &query_vulkan13_features =
                    query.template get<VkPhysicalDeviceVulkan13Features>();
                auto &query_extended_dynamic_state_features =
                    query.template get<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>();
                return query_vulkan13_features.dynamicRendering &&
                       query_vulkan13_features.synchronization2 &&
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
                // NOTE: 下面的方式是错误的。因为 features.next=null
                //  .pEnabledFeatures = &enablefeatureChain.head().features,
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

    //diff: [test_vertext2] start
    auto pipelineLayout =
        create_pipeline_layout{}
            .setCreateInfo(
                {.setLayouts = {},
                 .pushConstantRanges = {{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                         .offset = 0,
                                         .size = sizeof(PushData)}}})
            .build(device);
    //diff: [test_vertext2] end

    using stage_info = create_graphics_pipeline::stage_info;
    auto graphicsPipeline =
        create_graphics_pipeline{}
            .setCreateInfo(
                {.pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                     {.colorAttachmentCount = 1,
                      .pColorAttachmentFormats = &swapchainBuild.refImageFormat()}}),
                 .stages = create_graphics_pipeline::makeStages(
                     stage_info{.stage = VK_SHADER_STAGE_VERTEX_BIT,
                                .filePath = VERT_SHADER_PATH,
                                .pName = "main"},
                     stage_info{.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                .filePath = FRAG_SHADER_PATH,
                                .pName = "main"}),
                 // diff: test_vertext start
                 .vertexInputState = Vertex::vertex_input_state(),
                 // diff: test_vertext start
                 .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                        .primitiveRestartEnable = VK_FALSE},
                 .tessellationState = {},
                 .viewportState = {.viewports = {VkViewport{}}, .scissors = {VkRect2D{}}},
                 .rasterizationState = {.depthClampEnable = VK_FALSE,
                                        .rasterizerDiscardEnable = VK_FALSE,
                                        .polygonMode = VK_POLYGON_MODE_FILL,
                                        .cullMode = VK_CULL_MODE_BACK_BIT,
                                        .frontFace = VK_FRONT_FACE_CLOCKWISE,
                                        // .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                        .depthBiasEnable = VK_FALSE,
                                        .lineWidth = 1.0F},
                 .multisampleState =
                     {
                         // 没有硬件采样配置
                         .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                         // NOTE: 9. 这里可以改进内部颜色质量
                         .sampleShadingEnable = VK_FALSE,
                     },
                 .depthStencilState = {},
                 .colorBlendState = {.logicOpEnable = VK_FALSE,
                                     .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                                     .attachments = {{.blendEnable = VK_FALSE, // 关闭混合
                                                      .colorWriteMask =
                                                          VK_COLOR_COMPONENT_R_BIT |
                                                          VK_COLOR_COMPONENT_G_BIT |
                                                          VK_COLOR_COMPONENT_B_BIT |
                                                          VK_COLOR_COMPONENT_A_BIT}}},
                 .dynamicState = {.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR}},
                 .layout = *pipelineLayout})
            .build(device);

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

    //diff: [test_vertext] start
    // diff: [test_vertext3] start
    // 定义顶点数据（一个四边形，两个三角形）
    const std::vector<Vertex> vertices = {
        {{-0.01f, -0.01f}}, // 左下，红
        {{0.01f, -0.01f}},  // 右下，绿
        {{0.01f, 0.01f}},   // 右上，蓝
        {{-0.01f, 0.01f}}   // 左上，黄
    }; // diff: [test_vertext3] send
    const std::vector<uint32_t> indices = {
        0, 1, 2, // 第一个三角形
        0, 2, 3  // 第二个三角形
    };
    auto make_buffer = [&]<typename Data>(const std::span<Data> &data,
                                          VkBufferUsageFlags usage, // NOLINT
                                          VkMemoryPropertyFlags properties) {
        VkDeviceSize buffer_size = sizeof(Data) * data.size();
        // using memory_allocate_info = create_buffer::memory_allocate_info;
        auto buffer =
            create_buffer{}
                .setCreateInfo({.size = buffer_size,
                                .usage = usage,
                                .sharingMode = VK_SHARING_MODE_EXCLUSIVE})
                .setGenMemoryAllocateInfo([&](VkMemoryRequirements memRequirements,
                                              VkPhysicalDeviceMemoryProperties
                                                  memoryProperties)
                                              -> create_buffer::memory_allocate_info {
                    return {
                        .pNext =
                            (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                                ? make_pNext(structure_chain<VkMemoryAllocateFlagsInfo>{
                                      VkMemoryAllocateFlagsInfo{
                                          .pNext = nullptr,
                                          .flags =
                                              VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT}})
                                : nullptr,
                        .allocationSize = memRequirements.size,
                        .memoryTypeIndex =
                            find_memory_type_index(memRequirements.memoryTypeBits,
                                                   memoryProperties, properties)};
                })
                .build(device);
        auto staging_buffer = create_staging_buffer(device, buffer_size);
        ::memcpy(staging_buffer.mapPtr(), data.data(), buffer_size);

        simple_copy_buffer(
            commandPool, GRAPHICS_AND_PRESENT, staging_buffer.buffer(), buffer.buffer(),
            VkBufferCopy{.srcOffset = {}, .dstOffset = {}, .size = buffer_size});
        return buffer;
    };
    auto vertexBuffer =
        make_buffer(std::span{vertices},
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto indexBuffer =
        make_buffer(std::span{indices},
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //diff: [test_vertext2] start
    // diff: [test_vertext3] start
    // NOTE: 红先被绘制，后面绘制的会覆盖前面的，因此红最小，黄最大。管线关闭了混合（blendEnable = VK_FALSE），逻辑操作为直接复制，所以后写入的像素会完全覆盖先写入的像素
    // NOTE: 一千万： FPS: 38.6 (avg: 39.2) | FrameTime: 25.94ms | Total: 79
    // NOTE: 十万： FPS: 1932.2 (avg: 1941.6) | FrameTime: 0.52ms | Total: 34958  才加3M的内存
    const uint32_t INSTANCE_COUNT = 1000; // 修改此值即可改变实例数量
    /*
struct InstanceData {
    glm::vec2 offset; // 2 × 4 = 8 字节
    glm::vec3 color;  // 3 × 4 = 12 字节
};
32 字节 × 100,000 = 3,200,000 字节 ≈ 3.05 MiB（约 3.2 MB）。 洒洒水了
*/
    std::vector<InstanceData> instancesDta;
    instancesDta.reserve(INSTANCE_COUNT);

    // 动态计算网格尺寸（保证覆盖[-1,1]区域）
    int gridSize = static_cast<int>(std::ceil(std::sqrt(INSTANCE_COUNT)));
    // 如果实例数正好是平方数，gridSize^2 >= INSTANCE_COUNT，取前 INSTANCE_COUNT 个即可
    // 调整每个格子的宽度/高度，使所有格子填满整个屏幕区域
    const float cellWidth = 2.0f / gridSize;
    const float cellHeight = 2.0f / gridSize;

    std::random_device rd;
    std::mt19937 gen(rd());
    // 随机偏移范围控制在格子大小的 40% 以内（避免重叠越界）
    std::uniform_real_distribution<float> dis(-0.4f, 0.4f);

    auto generateData = [&]() {
        uint32_t generated = 0;
        for (int i = 0; i < gridSize && generated < INSTANCE_COUNT; ++i)
        {
            for (int j = 0; j < gridSize && generated < INSTANCE_COUNT; ++j)
            {
                // 格子中心坐标
                float xCenter = -1.0f + (i + 0.5f) * cellWidth;
                float yCenter = -1.0f + (j + 0.5f) * cellHeight;
                // 随机偏移（乘以格子尺寸的一半，使得偏移在格子内部）
                float offsetX = dis(gen) * cellWidth * 0.5f;
                float offsetY = dis(gen) * cellHeight * 0.5f;
                // 颜色根据网格位置渐变（有规律）
                float r = static_cast<float>(i) / gridSize;
                float g = static_cast<float>(j) / gridSize;
                float b = 0.5f + 0.5f * (r + g) * 0.5f;
                instancesDta.push_back(
                    {.offset = glm::vec2{xCenter + offsetX, yCenter + offsetY},
                     .color = glm::vec3{r, g, b}});
                ++generated;
            }
        }
    };
    generateData();

    // diff: [test_vertext3] end

    auto instanceBuffer = make_buffer(std::span{instancesDta},
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceAddress instanceAddress = device.getBufferDeviceAddress(
        {.sType = sType<VkBufferDeviceAddressInfo>(), .buffer = instanceBuffer.buffer()});
    std::vector<VkDrawIndexedIndirectCommand> indirectCmds = {
        {.indexCount = static_cast<uint32_t>(indices.size()),
         .instanceCount = static_cast<uint32_t>(instancesDta.size()),
         .firstIndex = 0,
         .vertexOffset = 0,
         .firstInstance = 0}};
    auto indirectBuffer = make_buffer(std::span{indirectCmds},
                                      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //NOTE: 网格数据应该唯一。
    //diff: [test_vertext3] start: 使用同一种数据源，但是网格不一样，画三角形. 确定了。顶点数据可以共享
    instancesDta.clear();
    generateData();
    auto instanceBuffer2 = make_buffer(std::span{instancesDta},
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                           VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceAddress instanceAddress2 =
        device.getBufferDeviceAddress({.sType = sType<VkBufferDeviceAddressInfo>(),
                                       .buffer = instanceBuffer2.buffer()});
    std::vector<VkDrawIndexedIndirectCommand> indirectCmds2 = {
        {.indexCount = 3, //NOTE: 只用前三个
         .instanceCount = static_cast<uint32_t>(instancesDta.size()),
         .firstIndex = 0,
         .vertexOffset = 0,
         .firstInstance = 0}};
    auto indirectBuffer2 = make_buffer(std::span{indirectCmds2},
                                       VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                           VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    //diff: [test_vertext3] end

    //diff: [test_vertext2] end

    //diff: [test_vertext] end

    // NOLINTNEXTLINE
    const auto recordCommandBuffer = [&](const CommandBufferView &commandBuffer,
                                         uint32_t imageIndex) {
        VkImage image = swapchain.image(imageIndex);
        VkImageView imageView = swapchain.imageView(imageIndex);
        auto imageExtent = swapchain.imageExtent();

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        my_render::transition_image_layout(
            commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            {}, // srcAccessMask (no need to wait for previous operations)
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkClearValue clearColor = {.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}};

        VkRenderingAttachmentInfo colorAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = imageView,
            .imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clearColor};

        commandBuffer.beginRendering(
            {.sType = sType<VkRenderingInfo>(),
             .renderArea = {.offset = {.x = 0, .y = 0}, .extent = imageExtent},
             .layerCount = 1,
             .colorAttachmentCount = 1,
             .pColorAttachments = &colorAttachment,
             .pDepthAttachment = nullptr});
        commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *graphicsPipeline);
        std::array<VkViewport, 1> viewport = {
            VkViewport{.x = 0.0F,
                       .y = 0.0F,
                       .width = static_cast<float>(imageExtent.width),
                       .height = static_cast<float>(imageExtent.height),
                       .minDepth = 0.0F,
                       .maxDepth = 1.0F}};
        commandBuffer.setViewport(0, viewport);

        std::array<VkRect2D, 1> scissors = {
            VkRect2D{.offset = {.x = 0, .y = 0}, .extent = imageExtent}};
        commandBuffer.setScissor(0, scissors);

        //diff: [test_vertext2] start
        // 1. 绑定全局顶点/索引缓冲（仅一次）
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffer.buffer(), 0);
        commandBuffer.bindIndexBuffer(indexBuffer.buffer(), 0, VK_INDEX_TYPE_UINT32);

        // 2. 推送实例数据地址
        PushData pushConstants = {instanceAddress};
        commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                    sizeof(PushData), &pushConstants);

        // 3. 间接绘制
        commandBuffer.drawIndexedIndirect(indirectBuffer.buffer(), 0, indirectCmds.size(),
                                          sizeof(VkDrawIndexedIndirectCommand));
        {
            //diff: [test_vertext3] start
            // 2. 推送实例数据地址
            PushData pushConstants = {instanceAddress2};
            commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(PushData), &pushConstants);

            // 3. 间接绘制
            commandBuffer.drawIndexedIndirect(indirectBuffer2.buffer(), 0,
                                              indirectCmds2.size(),
                                              sizeof(VkDrawIndexedIndirectCommand));
            //diff: [test_vertext3] end
        }
        //diff: [test_vertext2] end

        commandBuffer.endRendering();

        my_render::transition_image_layout(
            commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
        commandBuffer.end();
    };
    // NOLINTNEXTLINE
    const auto recreateSwapChain = [&]() constexpr {
        surface.waitGoodFramebufferSize();
        device.waitIdle();

        swapchain.destroy();
        swapchain = swapchainBuild.rebuild();
    };
    // NOLINTNEXTLINE
    const auto drawFrame = [&]() constexpr {
        auto &inFlightFences = frameContext.inFlightFences;
        auto &currentFrame = frameContext.currentFrame;
        auto &presentCompleteSemaphore = frameContext.presentCompleteSemaphore;
        auto &semaphoreIndex = frameContext.semaphoreIndex;
        auto &renderFinishedSemaphore = frameContext.renderFinishedSemaphore;

        const LogicalDevice *logicalDevice = frameContext.device_;

        while (logicalDevice->waitForFences(1, inFlightFences[currentFrame], VK_TRUE,
                                            UINT64_MAX) == VK_TIMEOUT)
            ;

        auto [result, imageIndex] = swapchain.acquireNextImage(
            UINT64_MAX, presentCompleteSemaphore[semaphoreIndex], nullptr);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapChain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("failed to acquire swap chain image!");

        logicalDevice->resetFences(1, inFlightFences[currentFrame]);

        const auto &commandBuffer = commandBuffers[currentFrame];
        commandBuffer.reset({});
        recordCommandBuffer(commandBuffer, imageIndex);

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
    while (window.shouldClose() == 0)
    {
        surface::pollEvents();
        drawFrame();
        {
            // NOLINTBEGIN
            static auto lastTime = std::chrono::high_resolution_clock::now();
            static int frameCount = 0;
            static int totalFrames = 0;         // 总帧数
            static float accumulatedFPS = 0.0f; // 累计FPS用于平均
            static int avgSamples = 0;          // 平均样本数

            frameCount++;
            totalFrames++;

            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(now - lastTime).count();

            // 每秒更新一次统计
            if (elapsed >= 1.0F)
            {
                float fps = frameCount / elapsed;
                accumulatedFPS += fps;
                avgSamples++;

                // 平均FPS（平滑显示）
                float avgFPS = accumulatedFPS / avgSamples;

                // 帧时间（毫秒）
                float frameTimeMs = (elapsed / frameCount) * 1000.0f;

                std::println(
                    "FPS: {:.1f} (avg: {:.1f}) | FrameTime: {:.2f}ms | Total: {}", fps,
                    avgFPS, frameTimeMs, totalFrames);

                // 重置秒级计数器
                frameCount = 0;
                lastTime = now;
            } // NOLINTEND
        }
    }
    device.waitIdle();

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}