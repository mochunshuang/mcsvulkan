#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>

#include "../mcsvulkan/head.hpp"
#include "manifold/common.h"

#include <manifold/manifold.h>
#include <manifold/polygon.h>
#include <manifold/cross_section.h>

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
using mcs::vulkan::Fence;

using raii_vma = mcs::vulkan::raii_vma;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";
using mcs::vulkan::MCS_ASSERT;

static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;

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

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

namespace mcs::vulkan
{
    struct buffer_base
    {
        buffer_base() = default;
        constexpr buffer_base(VkBuffer buffer, VmaAllocator allocator,
                              VmaAllocation allocation) noexcept
            : buffer_{buffer}, allocator_{allocator}, allocation_{allocation}
        {
        }
        constexpr ~buffer_base() noexcept
        {
            destroy();
        }
        buffer_base(const buffer_base &) = delete;
        buffer_base &operator=(const buffer_base &) = delete;

        constexpr buffer_base(buffer_base &&other) noexcept
            : buffer_{std::exchange(other.buffer_, {})},
              allocator_{std::exchange(other.allocator_, {})},
              allocation_{std::exchange(other.allocation_, {})}
        {
        }

        constexpr buffer_base &operator=(buffer_base &&other) noexcept
        {
            if (&other != this)
            {
                this->destroy();
                buffer_ = std::exchange(other.buffer_, {});
                allocator_ = std::exchange(other.allocator_, {});
                allocation_ = std::exchange(other.allocation_, {});
            }
            return *this;
        }

        [[nodiscard]] constexpr VkBuffer buffer() const noexcept
        {
            return buffer_;
        }

        [[nodiscard]] constexpr VmaAllocation allocation() const noexcept
        {
            return allocation_;
        }

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return buffer_ != nullptr;
        }

        [[nodiscard]] void *map() const
        {
            void *data; // NOLINT
            ::vmaMapMemory(allocator_, allocation_, &data);
            return data;
        }

        void unmap() const noexcept
        {
            ::vmaUnmapMemory(allocator_, allocation_);
        }

        void copyDataToBuffer(const void *src, size_t size) const
        {
            void *data = map();
            ::memcpy(data, src, size);
            unmap();
        }

        void clear()
        {
            destroy();
        }

      private:
        VkBuffer buffer_{};
        VmaAllocator allocator_{};
        VmaAllocation allocation_{};

        constexpr void destroy() noexcept
        {
            if (buffer_ != nullptr)
            {
                ::vmaDestroyBuffer(allocator_, buffer_, allocation_);
                buffer_ = {};
                allocator_ = {};
                allocation_ = {};
            }
        }
    };

    // 修改 create_buffer 函数，移除 properties 参数
    [[nodiscard]] static constexpr buffer_base create_buffer(
        VmaAllocator allocator, const VkBufferCreateInfo &bufferInfo,
        const VmaAllocationCreateInfo &allocCreateInfo)
    {
        VkBuffer buffer = nullptr;
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo allocInfo;

        check_vkresult(::vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo,
                                         &buffer, &allocation, &allocInfo));

        buffer_base result{buffer, allocator, allocation};

        return result;
    }

    // 修改 staging_buffer 函数，直接使用 VMA 的便利标志
    static constexpr auto staging_buffer(VmaAllocator allocator, size_t buffer_size)
    {
        // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html#:~:text=Persistently%20mapped%20memory
        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        return create_buffer(allocator,
                             {.sType = sType<VkBufferCreateInfo>(),
                              .size = buffer_size,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                             allocCreateInfo);
    }

    static constexpr auto simple_copy_buffer(const CommandPool &commandpool,
                                             const Queue &queue, VkBuffer srcBuffer,
                                             VkBuffer dstBuffer,
                                             const VkBufferCopy &regions)
    {
        const auto *logicalDevice = commandpool.device();
        CommandBuffer commandCopyBuffer = commandpool.allocateOneCommandBuffer(
            {.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY});

        commandCopyBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>(),
                                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
        commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, {&regions, 1});
        commandCopyBuffer.end();

        Fence fence{*logicalDevice, {.sType = sType<VkFenceCreateInfo>(), .flags = 0}};
        queue.submit(1,
                     {.sType = sType<VkSubmitInfo>(),
                      .commandBufferCount = 1,
                      .pCommandBuffers = &*commandCopyBuffer},
                     *fence);
        check_vkresult(logicalDevice->waitForFences(1, *fence, VK_TRUE, UINT64_MAX));
    }

}; // namespace mcs::vulkan

struct PushConstants
{
    uint64_t vertexBufferAddress; // 使用uint64_t而不是VkDeviceAddress
};
struct Vertex // NOLINT
{
    glm::vec2 pos;
    glm::vec3 color;
};
static_assert(alignof(Vertex) == 4, "check alignof error");
static_assert(sizeof(Vertex) == 20, "check sizeof error");

struct mesh_base
{
    using index_type = uint32_t;
    // 双缓冲顶点数据：每个飞行帧都有自己的缓冲区
    struct FrameBuffers
    {
        mcs::vulkan::buffer_base vertexBuffer;
        mcs::vulkan::buffer_base indexBuffer;
        VkDeviceAddress vertexBufferAddress = 0;
    };

    static consteval auto indexType()
    {
        return VK_INDEX_TYPE_UINT32; // 必须匹配 index_type
    }

    struct mesh_data
    {
        std::vector<Vertex> vertices;
        std::vector<index_type> indices; // NOTE: 需要保留
        void clear()
        {
            vertices.clear();
        }
    };

    const CommandPool *commandpool{};
    const Queue *queue{};

    std::array<FrameBuffers, MAX_FRAMES_IN_FLIGHT> frameBuffers;
    mesh_data queue_data;
    uint32_t count = 0;

    mesh_base(VmaAllocator allocator, const CommandPool &pool, const Queue &queue,
              mesh_data init_data)
        : commandpool{&pool}, queue{&queue}, queue_data{std::move(init_data)}
    {
        for (auto &fb : frameBuffers)
        {
            createVertexBufferForFrame(allocator, fb);
            createIndexBufferForFrame(allocator, fb);
            getBufferDeviceAddresses(fb);
        }
    }

    // buffer 内存要求
    static constexpr auto REQUIRE_BUFFER_USAGE =
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    // NOTE: 不再需要重复设置下面的内存配置
    //  static constexpr auto REQUIRE_MEMORY_FLAG = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    constexpr void createVertexBufferForFrame(VmaAllocator allocator, FrameBuffers &fb)
    {
        auto &vertices = queue_data.vertices;
        const VkDeviceSize BUFFER_SIZE = sizeof(vertices[0]) * vertices.size();
        auto &destBuffer = fb.vertexBuffer;
        constexpr auto USAGE = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        // 直接使用重构后的 create_buffer 函数
        destBuffer = mcs::vulkan::create_buffer(
            allocator,
            {.sType = sType<VkBufferCreateInfo>(),
             .size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | USAGE | REQUIRE_BUFFER_USAGE,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            {
                .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
            });

        // 创建暂存缓冲区并复制数据
        const auto STAGING_BUFFER = mcs::vulkan::staging_buffer(allocator, BUFFER_SIZE);
        STAGING_BUFFER.copyDataToBuffer(vertices.data(), BUFFER_SIZE);

        mcs::vulkan::simple_copy_buffer(*commandpool, *queue, STAGING_BUFFER.buffer(),
                                        destBuffer.buffer(),
                                        VkBufferCopy{.size = BUFFER_SIZE});
    }

    void createIndexBufferForFrame(VmaAllocator allocator, FrameBuffers &fb)
    {
        auto &indices = queue_data.indices;
        const VkDeviceSize BUFFER_SIZE = sizeof(indices[0]) * indices.size();
        auto &destBuffer = fb.indexBuffer;
        constexpr auto USAGE = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        // 直接使用重构后的 create_buffer 函数
        destBuffer = mcs::vulkan::create_buffer(
            allocator,
            {.sType = sType<VkBufferCreateInfo>(),
             .size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | USAGE | REQUIRE_BUFFER_USAGE,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            {.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
             .usage = VMA_MEMORY_USAGE_AUTO});

        // 创建暂存缓冲区并复制数据
        const auto STAGING_BUFFER = mcs::vulkan::staging_buffer(allocator, BUFFER_SIZE);
        STAGING_BUFFER.copyDataToBuffer(indices.data(), BUFFER_SIZE);

        mcs::vulkan::simple_copy_buffer(*commandpool, *queue, STAGING_BUFFER.buffer(),
                                        destBuffer.buffer(),
                                        {VkBufferCopy{.size = BUFFER_SIZE}});
    }

    void getBufferDeviceAddresses(FrameBuffers &fb)
    {
        const auto *device = commandpool->device();
        if (fb.vertexBuffer.buffer() != VK_NULL_HANDLE)
        {
            fb.vertexBufferAddress = device->getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(),
                 .buffer = fb.vertexBuffer.buffer()});
        }
    }

    [[nodiscard]] VkDeviceAddress getVertexBufferAddress(
        uint32_t currentFrame) const noexcept
    {
        return frameBuffers[currentFrame].vertexBufferAddress;
    }

    // 只排队，不立即执行
    void queueUpdate(std::vector<Vertex> vertices, std::vector<index_type> indices)
    {
        queue_data.vertices = std::move(vertices);
        queue_data.indices = std::move(indices);
        count = 2;
    }

    void requestUpdate(VmaAllocator allocator, uint32_t currentFrame)
    {
        if (count == 0)
        {
            queue_data.clear();
            return;
        }
        --count;

        // 更新GPU缓冲区（这个帧当前没有被GPU使用）
        auto &fb = frameBuffers[currentFrame];
        createVertexBufferForFrame(allocator, fb);
        createIndexBufferForFrame(allocator, fb);
        getBufferDeviceAddresses(fb);
    }

    constexpr void clear() noexcept
    {
        frameBuffers = {};
    }
};

// vma conifg ---------------------------------------------
constexpr auto VMA_USE_BIND_MEMORY2 = true;
constexpr auto VMA_USE_MEMORY_BUDGET = true;
constexpr auto VMA_USE_BUFFER_DEVICE_ADDRESS = true;

static auto initVmaFlag(std::vector<const char *> &requiredDeviceExtension)
{
    // NOTE: 配置 vma 的 flags
    VmaAllocatorCreateFlags vma_flags = 0;
    // @see
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html#:~:text=of%20this%20function.-,Enabling%20extensions,-VMA%20can%20automatically
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
    // diff: 添加扩展
    requiredDeviceExtension.emplace_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    // diff: [new] 启用bufferDeviceAddress特性
    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceVulkan12Features, // diff: 添加Vulkan 1.2特性
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        enablefeatureChain = {
            {.features = {.shaderInt64 = VK_TRUE}}, // diff: shader 扩展这里也要打开
            {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
            {
                .scalarBlockLayout = VK_TRUE, // diff: [new] 启用标量块布局
                .bufferDeviceAddress =
                    VK_TRUE, // diff: [new] 在Vulkan 1.3中也启用bufferDeviceAddress
            },
            {.extendedDynamicState = VK_TRUE}};

    // NOTE: 配置 vma 的 flags
    VmaAllocatorCreateFlags vma_flags = initVmaFlag(requiredDeviceExtension);

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
                                    VkPhysicalDeviceVulkan12Features,
                                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>{
                        {}, {}, {}, {}};
                physicalDevice.getFeatures2(&query.head());
                auto &features2 = query.template get<VkPhysicalDeviceFeatures2>();
                auto &query_vulkan13_features =
                    query.template get<VkPhysicalDeviceVulkan13Features>();
                auto &query_vulkan12_features = // diff: [new] 检查Vulkan 1.2特性
                    query.template get<VkPhysicalDeviceVulkan12Features>();
                auto &query_extended_dynamic_state_features =
                    query.template get<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>();
                return features2.features.shaderInt64 &&
                       query_vulkan13_features.dynamicRendering &&
                       query_vulkan13_features.synchronization2 &&

                       query_vulkan12_features.bufferDeviceAddress && // diff: [new]
                       // 检查Vulkan 1.2中的bufferDeviceAddress
                       query_vulkan12_features.scalarBlockLayout && // diff: [new]
                       // 检查scalarBlockLayout

                       query_extended_dynamic_state_features.extendedDynamicState;
            })
            .select()[0];

    // NOTE: 根据GPU 配置 vma
    updateVmaFlag(vma_flags, physical_device, requiredDeviceExtension);

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

    // NOTE: 逻辑设备确定后，就能初始化 vma了
    raii_vma vma{{.flags = vma_flags,
                  .physicalDevice = *physical_device,
                  .device = *device,
                  .instance = *instance,
                  .vulkanApiVersion = APIVERSION}};
    [[maybe_unused]] VmaAllocator allocator = vma.allocator();

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

    auto pipelineLayout =
        create_pipeline_layout{}
            .setCreateInfo(
                {.setLayouts = {}, // diff: [new] 布局定义推送常量范围
                 .pushConstantRanges = {{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                         .offset = 0,
                                         .size = sizeof(PushConstants)}}})
            .build(device);

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
                 .vertexInputState = {},
                 .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                        .primitiveRestartEnable = VK_FALSE},
                 .tessellationState = {},
                 .viewportState = {.viewports = {VkViewport{}}, .scissors = {VkRect2D{}}},
                 .rasterizationState =
                     {.depthClampEnable = VK_FALSE,
                      .rasterizerDiscardEnable = VK_FALSE,
                      .polygonMode = VK_POLYGON_MODE_FILL,

                      // diff: [test_triangle] start 逆时针为正面的约定，兼容性配置固定
                      .cullMode = VK_CULL_MODE_BACK_BIT,            // 恢复背面剔除
                      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, // 逆时针为正面
                      // diff: [test_triangle] end
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

    // diff: 准备好顶点和顶点索引--------------- // NOLINTBEGIN
    // diff: [test_triangle] start
    /*
第一部分：单 API 能力映射表（从 API 出发）
分类	API 名称	能实现的 2D 目标	为什么这个 API 是实现该目标的关键？
构建	
CrossSection(contour)
CrossSection(contours)	将草图数据转化为可计算的对象	这是唯一入口。所有外部传入的轮廓线（如 SVG 路径、DXF 轮廓）必须通过它才能进入 Manifold 世界。它在构造时强制做 Union，保证后续运算不会因自交而崩溃。
Square	生成标准矩形截面	非零尺寸直接生成，且支持居中/贴角两种定位模式。是构建边框、板材、加强筋的基础。
Circle	生成标准圆形截面（或正多边形）	可控精度（分段数）。是实现通孔、销钉、圆角基底的直接手段。

信息	
IsEmpty	检测无效运算结果	唯一的安全阀。当布尔运算（如相交）无结果，或 Offset 内缩过度导致图形消失时，它返回 true。不检查此标志会导致后续 Extrude 产生空几何体错误。
NumVert / NumContour	评估性能与复杂度	用于判断 Offset 是否产生了过多顶点（需调用 Simplify），或判断布尔运算后是否产生了意外的孔洞（轮廓数变多）。
Bounds	获取图形占位范围	是实现自动居中排版、计算边界间距、确定外框的唯一数据源。
Area	校验尺寸准确性	2D 世界的“称重计”。验证 Offset 或布尔运算是否改变了预期尺寸的唯一量化标准。

变换	
Translate	图形阵列与定位	结合循环可实现线性阵列、矩形阵列。是所有复杂布局（如散热孔排布）的基石。
Rotate	任意角度摆放	实现斜向特征、旋转对称（如扇叶、法兰盘孔位）的核心。
Scale	生成公差间隙或锥度	实现过盈配合（缩小）或松动配合（放大）。比如让螺丝孔比螺丝杆稍大。
Mirror	生成对称结构	效率翻倍工具。只做一半图形，镜像得到另一半。适用于对称的支架、挂钩。
Transform	批量应用复杂矩阵	结合矩阵库可一次性完成旋转+平移+错切。是高级 2D 仿射变换的唯一入口。
Warp / WarpBatch	实现自由曲面变形	打破规则形状的唯一手段。可实现波浪边缘、随曲线弯曲的文字、自定义函数生成的异形截面。

修整	
Simplify	清理运算产生的碎面	性能优化命脉。Offset 圆角会产生极短边，若不清理，后续连续 Offset 会指数级变慢甚至卡死。
Offset	轮廓缩放与倒角美化	2D 加工的核心。正数：生成包络盒/外框；负数：生成内缩槽/安全边距。结合 JoinType 可实现内外圆角/直角处理。

布尔	
+ (Union)	合并多个零件截面	将分散的图形合并为一个整体，用于生成单一拉伸实体或计算总占地面积。
- (Subtract)	打孔、开槽、修边	2D 去除材料的唯一方式。实现：板上的螺丝孔、边缘的缺口、内部的镂空。
^ (Intersect)	提取重叠区域	用于生成加强筋（两板相交部分）或计算裁剪保留区。
BatchBoolean	一次性多零件融合	高阶性能优化。当需要对 100 个圆执行 Union 时，它比 99 次 + 运算快一个数量级。

拓扑	
Decompose	分离独立零件	做了一次复杂 Union 后，可能生成了两个互不相连的零件。必须用它拆分才能分别拉伸不同高度。
Compose	重新组合独立零件	对应 Decompose 的逆过程，或用于手动组装多个截面。

凸包	
Hull	生成最小包围凸形状	实现随形包络。给定 4 个杂乱的点或圆，生成包裹它们的最小凸多边形。用于生成连接件、支架基座、橡皮筋效果。
*/

    /*
第二部分：组合任务计划书（多 API 协同）
某些实际的 2D 设计目标无法由单个 API 完成，必须组合调用。


组合任务名称	                    涉及 API 组合链	                    最终 2D 效果	                    组合逻辑解释
任务 1：制作圆角矩形板	            Square → Offset	                    带有指定半径圆角的矩形轮廓	        Square 生成直角矩形，Offset 负责将尖角膨胀为圆角（Round）。
任务 2：制作带均匀边距的镂空	    Square → Offset(-)	                大矩形向内缩小形成边框	            Square 外框，Offset 传入负数生成内框，两者相减得到壁厚均匀的边框。
任务 3：在一块板上阵列打孔	        Circle → Translate → BatchBoolean	板上布满规则排列的孔	            Circle 生孔，Translate 移动位置，最后用 BatchBoolean 一次性从 Square 中减去。
任务 4：制作异形倒角字体	        Text轮廓 → Offset → Simplify	    将文字的尖角变成圆角	            文字轮廓尖角多，Offset 正偏移将外角变圆，内角变圆（需配合 FillRule），Simplify 清理碎片。
任务 5：制作连接两个圆的“骨头”形	Circle(A) + Circle(B) → Hull	    哑铃形/药丸形截面	                两个分离的圆无法直接拉伸成连接杆，Hull 会计算覆盖两圆的最小凸包，形成平滑连接。
任务 6：分离排版中的多个零件	    Compose → Decompose	                将拼在一起的多个图形拆成独立个体	导入的 DXF 可能是一整块，Decompose 识别不连通的部分，拆开后才能分别移动排版（节省材料）。
任务 7：生成不规则的艺术边框	    Square → Warp (正弦函数)	        边缘呈波浪状的异形面板	            仅靠规则图形无法做到，必须通过 Warp 遍历顶点修改 Y 值，实现周期波动。
*/

    /*
学习路线建议（基于上述映射）
输入阶段：先看 Square, Circle 和构造器 —— 搞清楚图形从哪来。
观察阶段：接着看 Area, Bounds, IsEmpty —— 搞清楚图形长什么样。
移动阶段：再看 Translate, Rotate —— 搞清楚图形放哪里。
变形阶段：重点看 Offset 和 Simplify —— 这是 2D 的美容与加工核心。
组合阶段：最后看 +, -, Hull —— 搞清楚图形怎么拼装。
后处理阶段：掌握 Decompose —— 搞清楚怎么把拼好的东西再拆开用。
*/
    /**
NOTE: 坐标映射已正确：Scale(scale) 就是像素到 NDC 的桥梁。
统一风格：上述代码强调了“像素构建 → Scale(scale) 映射”的两步模式

坐标系	                            原点位置	                                                                        X 范围	            Y 范围	                                                    单位
像素坐标系（我们构建图形时用的）	屏幕左上角或中心？注意我们用了 Square({w, h}, true)，图形中心在原点 (0,0)，但尺寸是像素值。	[-width/2, width/2]	[-height/2, height/2]	                                    像素
NDC（Vulkan 期望的）	        屏幕中心	                                                                             [-1, 1]	         [-1, 1]（Y 向下，但 Manifold 输出 Y 向上，我们最后会翻转）	    无单位比例
*/
    // 1. Translate —— 阵列平移
    auto buildMeshTranslate = [&](std::vector<Vertex> &outVerts,
                                  std::vector<uint32_t> &outIndices,
                                  VkExtent2D screenSize) {
        outVerts.clear();
        outIndices.clear();
        auto [width, height] = screenSize;
        auto scale = 2.0f / manifold::vec2(width, height); // 像素 → NDC 映射

        // 基础单元：一个小正方形
        auto base = manifold::CrossSection::Square({80.0f, 80.0f}, true);
        // 平移生成 3x2 阵列
        auto cs = base;
        cs += base.Translate({100.0f, 0.0f});
        cs += base.Translate({200.0f, 0.0f});
        cs += base.Translate({0.0f, 100.0f});
        cs += base.Translate({100.0f, 100.0f});
        cs += base.Translate({200.0f, 100.0f});

        cs = cs.Scale(scale); // 映射到 NDC

        std::cout << "=== Translate Array ===\n";
        std::cout << "  Area: " << cs.Area() << ", NumContour: " << cs.NumContour()
                  << "\n";

        //================================后续类似=================================
        {
            // ---------- 信息提取 Lambda ----------
            auto infoExtractor = [](const manifold::CrossSection &cs,
                                    const std::string &name) {
                std::cout << "=== CrossSection Info: " << name << " ===\n";
                std::cout << "  IsEmpty    : " << (cs.IsEmpty() ? "true" : "false")
                          << "\n";
                if (cs.IsEmpty())
                    return;

                auto bounds = cs.Bounds();
                std::cout << "  Area       : " << cs.Area() << "\n";
                std::cout << "  Bounds     : min(" << bounds.min.x << ", " << bounds.min.y
                          << "), max(" << bounds.max.x << ", " << bounds.max.y << ")\n";
                std::cout << "  NumVert    : " << cs.NumVert() << "\n";
                std::cout << "  NumContour : " << cs.NumContour() << "\n";
            };

            // 分别提取外矩形和最终截面的信息
            infoExtractor(cs, "INFO");

            // ---------- 拉伸为 3D 网格 ----------
            auto mesh = manifold::Manifold::Extrude(cs.ToPolygons(), 1e-4).GetMeshGL();
            uint32_t numProp = mesh.numProp;
            uint32_t vCount = mesh.vertProperties.size() / numProp;

            // 2. 提取所有原始顶点坐标（Manifold 输出）
            std::vector<glm::vec3> positions(vCount);
            for (uint32_t i = 0; i < vCount; ++i)
            {
                size_t off = i * numProp;
                positions[i] = {mesh.vertProperties[off], mesh.vertProperties[off + 1],
                                mesh.vertProperties[off + 2]};
            }

            // 3. 筛选顶面三角形（z ≈ thickness），收集原始索引
            const double eps = 1e-6;
            const double thickness = 1e-4;
            const auto &triVerts = mesh.triVerts;
            std::vector<uint32_t> topFaceIndices;

            for (size_t i = 0; i < triVerts.size(); i += 3)
            {
                uint32_t i0 = triVerts[i];
                uint32_t i1 = triVerts[i + 1];
                uint32_t i2 = triVerts[i + 2];
                float z0 = positions[i0].z;
                float z1 = positions[i1].z;
                float z2 = positions[i2].z;
                if (std::abs(z0 - thickness) < eps && std::abs(z1 - thickness) < eps &&
                    std::abs(z2 - thickness) < eps)
                {
                    topFaceIndices.insert(topFaceIndices.end(), {i0, i1, i2});
                }
            }

            // 4. 顶点去重并重新映射索引，同时翻转 Y 坐标（适配 Vulkan NDC）
            std::unordered_map<uint32_t, uint32_t> oldToNew;
            for (uint32_t oldIdx : topFaceIndices)
            {
                if (oldToNew.find(oldIdx) == oldToNew.end())
                {
                    oldToNew[oldIdx] = static_cast<uint32_t>(outVerts.size());
                    // Y 翻转：适配 Vulkan NDC
                    outVerts.push_back({{positions[oldIdx].x, -positions[oldIdx].y},
                                        glm::vec3(1.0f, 0.5f, 0.2f)});
                }
                outIndices.push_back(oldToNew[oldIdx]);
            }

            std::cout << "Manifold mesh: " << outVerts.size() << " vertices, "
                      << outIndices.size() / 3 << " triangles\n";
        }
    };
    // 2. Rotate —— 旋转矩形（风扇叶片效果）
    auto buildMeshRotate = [&](std::vector<Vertex> &outVerts,
                               std::vector<uint32_t> &outIndices, VkExtent2D screenSize) {
        outVerts.clear();
        outIndices.clear();
        auto [width, height] = screenSize;
        auto scale = 2.0f / manifold::vec2(width, height);

        auto blade = manifold::CrossSection::Square({200.0f, 60.0f}, true);
        auto cs = blade;
        for (int i = 1; i < 6; ++i)
            cs += blade.Rotate(i * 60.0);

        cs = cs.Scale(scale);
        std::cout << "=== Rotate (6 blades) ===\n";
        std::cout << "  Area: " << cs.Area() << ", NumVert: " << cs.NumVert() << "\n";

        // Extrude + 顶面提取...
        //================================后续类似=================================
        {
            // ---------- 信息提取 Lambda ----------
            auto infoExtractor = [](const manifold::CrossSection &cs,
                                    const std::string &name) {
                std::cout << "=== CrossSection Info: " << name << " ===\n";
                std::cout << "  IsEmpty    : " << (cs.IsEmpty() ? "true" : "false")
                          << "\n";
                if (cs.IsEmpty())
                    return;

                auto bounds = cs.Bounds();
                std::cout << "  Area       : " << cs.Area() << "\n";
                std::cout << "  Bounds     : min(" << bounds.min.x << ", " << bounds.min.y
                          << "), max(" << bounds.max.x << ", " << bounds.max.y << ")\n";
                std::cout << "  NumVert    : " << cs.NumVert() << "\n";
                std::cout << "  NumContour : " << cs.NumContour() << "\n";
            };

            // 分别提取外矩形和最终截面的信息
            infoExtractor(cs, "INFO");

            // ---------- 拉伸为 3D 网格 ----------
            auto mesh = manifold::Manifold::Extrude(cs.ToPolygons(), 1e-4).GetMeshGL();
            uint32_t numProp = mesh.numProp;
            uint32_t vCount = mesh.vertProperties.size() / numProp;

            // 2. 提取所有原始顶点坐标（Manifold 输出）
            std::vector<glm::vec3> positions(vCount);
            for (uint32_t i = 0; i < vCount; ++i)
            {
                size_t off = i * numProp;
                positions[i] = {mesh.vertProperties[off], mesh.vertProperties[off + 1],
                                mesh.vertProperties[off + 2]};
            }

            // 3. 筛选顶面三角形（z ≈ thickness），收集原始索引
            const double eps = 1e-6;
            const double thickness = 1e-4;
            const auto &triVerts = mesh.triVerts;
            std::vector<uint32_t> topFaceIndices;

            for (size_t i = 0; i < triVerts.size(); i += 3)
            {
                uint32_t i0 = triVerts[i];
                uint32_t i1 = triVerts[i + 1];
                uint32_t i2 = triVerts[i + 2];
                float z0 = positions[i0].z;
                float z1 = positions[i1].z;
                float z2 = positions[i2].z;
                if (std::abs(z0 - thickness) < eps && std::abs(z1 - thickness) < eps &&
                    std::abs(z2 - thickness) < eps)
                {
                    topFaceIndices.insert(topFaceIndices.end(), {i0, i1, i2});
                }
            }

            // 4. 顶点去重并重新映射索引，同时翻转 Y 坐标（适配 Vulkan NDC）
            std::unordered_map<uint32_t, uint32_t> oldToNew;
            for (uint32_t oldIdx : topFaceIndices)
            {
                if (oldToNew.find(oldIdx) == oldToNew.end())
                {
                    oldToNew[oldIdx] = static_cast<uint32_t>(outVerts.size());
                    // Y 翻转：适配 Vulkan NDC
                    outVerts.push_back({{positions[oldIdx].x, -positions[oldIdx].y},
                                        glm::vec3(1.0f, 0.5f, 0.2f)});
                }
                outIndices.push_back(oldToNew[oldIdx]);
            }

            std::cout << "Manifold mesh: " << outVerts.size() << " vertices, "
                      << outIndices.size() / 3 << " triangles\n";
        }
    };
    // 3. Scale —— 公差配合（过盈与松动）
    auto buildMeshScale = [&](std::vector<Vertex> &outVerts,
                              std::vector<uint32_t> &outIndices, VkExtent2D screenSize) {
        outVerts.clear();
        outIndices.clear();
        auto [width, height] = screenSize;
        auto scale = 2.0f / manifold::vec2(width, height);

        auto outer = manifold::CrossSection::Circle(150.0f, 64);
        auto inner = outer.Scale({0.8f, 0.8f}); // 内缩20%
        auto cs = outer - inner;

        cs = cs.Scale(scale);
        std::cout << "=== Scale (ring) ===\n";
        std::cout << "  Area: " << cs.Area() << ", NumContour: " << cs.NumContour()
                  << "\n";
        // Extrude...
        //================================后续类似=================================
        {
            // ---------- 信息提取 Lambda ----------
            auto infoExtractor = [](const manifold::CrossSection &cs,
                                    const std::string &name) {
                std::cout << "=== CrossSection Info: " << name << " ===\n";
                std::cout << "  IsEmpty    : " << (cs.IsEmpty() ? "true" : "false")
                          << "\n";
                if (cs.IsEmpty())
                    return;

                auto bounds = cs.Bounds();
                std::cout << "  Area       : " << cs.Area() << "\n";
                std::cout << "  Bounds     : min(" << bounds.min.x << ", " << bounds.min.y
                          << "), max(" << bounds.max.x << ", " << bounds.max.y << ")\n";
                std::cout << "  NumVert    : " << cs.NumVert() << "\n";
                std::cout << "  NumContour : " << cs.NumContour() << "\n";
            };

            // 分别提取外矩形和最终截面的信息
            infoExtractor(cs, "INFO");

            // ---------- 拉伸为 3D 网格 ----------
            auto mesh = manifold::Manifold::Extrude(cs.ToPolygons(), 1e-4).GetMeshGL();
            uint32_t numProp = mesh.numProp;
            uint32_t vCount = mesh.vertProperties.size() / numProp;

            // 2. 提取所有原始顶点坐标（Manifold 输出）
            std::vector<glm::vec3> positions(vCount);
            for (uint32_t i = 0; i < vCount; ++i)
            {
                size_t off = i * numProp;
                positions[i] = {mesh.vertProperties[off], mesh.vertProperties[off + 1],
                                mesh.vertProperties[off + 2]};
            }

            // 3. 筛选顶面三角形（z ≈ thickness），收集原始索引
            const double eps = 1e-6;
            const double thickness = 1e-4;
            const auto &triVerts = mesh.triVerts;
            std::vector<uint32_t> topFaceIndices;

            for (size_t i = 0; i < triVerts.size(); i += 3)
            {
                uint32_t i0 = triVerts[i];
                uint32_t i1 = triVerts[i + 1];
                uint32_t i2 = triVerts[i + 2];
                float z0 = positions[i0].z;
                float z1 = positions[i1].z;
                float z2 = positions[i2].z;
                if (std::abs(z0 - thickness) < eps && std::abs(z1 - thickness) < eps &&
                    std::abs(z2 - thickness) < eps)
                {
                    topFaceIndices.insert(topFaceIndices.end(), {i0, i1, i2});
                }
            }

            // 4. 顶点去重并重新映射索引，同时翻转 Y 坐标（适配 Vulkan NDC）
            std::unordered_map<uint32_t, uint32_t> oldToNew;
            for (uint32_t oldIdx : topFaceIndices)
            {
                if (oldToNew.find(oldIdx) == oldToNew.end())
                {
                    oldToNew[oldIdx] = static_cast<uint32_t>(outVerts.size());
                    // Y 翻转：适配 Vulkan NDC
                    outVerts.push_back({{positions[oldIdx].x, -positions[oldIdx].y},
                                        glm::vec3(1.0f, 0.5f, 0.2f)});
                }
                outIndices.push_back(oldToNew[oldIdx]);
            }

            std::cout << "Manifold mesh: " << outVerts.size() << " vertices, "
                      << outIndices.size() / 3 << " triangles\n";
        }
    };
    // 4. Mirror —— 对称支架
    auto buildMeshMirror = [&](std::vector<Vertex> &outVerts,
                               std::vector<uint32_t> &outIndices, VkExtent2D screenSize) {
        outVerts.clear();
        outIndices.clear();
        auto [width, height] = screenSize;
        auto scale = 2.0f / manifold::vec2(width, height);

        // 右半部分 L 形
        manifold::SimplePolygon L = {{0.0f, 0.0f},   {100.0f, 0.0f},  {100.0f, 40.0f},
                                     {40.0f, 40.0f}, {40.0f, 120.0f}, {0.0f, 120.0f}};
        auto right = manifold::CrossSection(L);
        auto left = right.Mirror({1.0f, 0.0f}); // 沿 Y 轴镜像
        auto cs = right + left;

        cs = cs.Translate({-50.0f, -60.0f}).Scale(scale);
        std::cout << "=== Mirror Symmetric Bracket ===\n";
        std::cout << "  Area: " << cs.Area() << ", NumVert: " << cs.NumVert() << "\n";

        // Extrude...
        //================================后续类似=================================
        {
            // ---------- 信息提取 Lambda ----------
            auto infoExtractor = [](const manifold::CrossSection &cs,
                                    const std::string &name) {
                std::cout << "=== CrossSection Info: " << name << " ===\n";
                std::cout << "  IsEmpty    : " << (cs.IsEmpty() ? "true" : "false")
                          << "\n";
                if (cs.IsEmpty())
                    return;

                auto bounds = cs.Bounds();
                std::cout << "  Area       : " << cs.Area() << "\n";
                std::cout << "  Bounds     : min(" << bounds.min.x << ", " << bounds.min.y
                          << "), max(" << bounds.max.x << ", " << bounds.max.y << ")\n";
                std::cout << "  NumVert    : " << cs.NumVert() << "\n";
                std::cout << "  NumContour : " << cs.NumContour() << "\n";
            };

            // 分别提取外矩形和最终截面的信息
            infoExtractor(cs, "INFO");

            // ---------- 拉伸为 3D 网格 ----------
            auto mesh = manifold::Manifold::Extrude(cs.ToPolygons(), 1e-4).GetMeshGL();
            uint32_t numProp = mesh.numProp;
            uint32_t vCount = mesh.vertProperties.size() / numProp;

            // 2. 提取所有原始顶点坐标（Manifold 输出）
            std::vector<glm::vec3> positions(vCount);
            for (uint32_t i = 0; i < vCount; ++i)
            {
                size_t off = i * numProp;
                positions[i] = {mesh.vertProperties[off], mesh.vertProperties[off + 1],
                                mesh.vertProperties[off + 2]};
            }

            // 3. 筛选顶面三角形（z ≈ thickness），收集原始索引
            const double eps = 1e-6;
            const double thickness = 1e-4;
            const auto &triVerts = mesh.triVerts;
            std::vector<uint32_t> topFaceIndices;

            for (size_t i = 0; i < triVerts.size(); i += 3)
            {
                uint32_t i0 = triVerts[i];
                uint32_t i1 = triVerts[i + 1];
                uint32_t i2 = triVerts[i + 2];
                float z0 = positions[i0].z;
                float z1 = positions[i1].z;
                float z2 = positions[i2].z;
                if (std::abs(z0 - thickness) < eps && std::abs(z1 - thickness) < eps &&
                    std::abs(z2 - thickness) < eps)
                {
                    topFaceIndices.insert(topFaceIndices.end(), {i0, i1, i2});
                }
            }

            // 4. 顶点去重并重新映射索引，同时翻转 Y 坐标（适配 Vulkan NDC）
            std::unordered_map<uint32_t, uint32_t> oldToNew;
            for (uint32_t oldIdx : topFaceIndices)
            {
                if (oldToNew.find(oldIdx) == oldToNew.end())
                {
                    oldToNew[oldIdx] = static_cast<uint32_t>(outVerts.size());
                    // Y 翻转：适配 Vulkan NDC
                    outVerts.push_back({{positions[oldIdx].x, -positions[oldIdx].y},
                                        glm::vec3(1.0f, 0.5f, 0.2f)});
                }
                outIndices.push_back(oldToNew[oldIdx]);
            }

            std::cout << "Manifold mesh: " << outVerts.size() << " vertices, "
                      << outIndices.size() / 3 << " triangles\n";
        }
    };
    // 5. Transform —— 仿射变换（旋转+平移+错切）
    auto buildMeshTransform = [&](std::vector<Vertex> &outVerts,
                                  std::vector<uint32_t> &outIndices,
                                  VkExtent2D screenSize) {
        outVerts.clear();
        outIndices.clear();
        auto [width, height] = screenSize;
        auto scale = 2.0f / manifold::vec2(width, height);

        auto rect = manifold::CrossSection::Square({120.0f, 60.0f}, true);
        // 构建变换矩阵：旋转30度，平移(100, 50)，错切 X 轴
        double ang = 30.0 * manifold::kPi / 180.0;
        // 构建复合矩阵：先旋转，再平移，最后缩放（这里直接算好最终矩阵）
        manifold::mat2x3 mat;
        // 列主序：col0 = (cos*scaleX, sin*scaleX)
        //        col1 = (-sin*scaleY, cos*scaleY)
        //        col2 = (tx*scaleX, ty*scaleY)
        mat[0] = {std::cos(ang) * scale.x, std::sin(ang) * scale.x};
        mat[1] = {-std::sin(ang) * scale.y, std::cos(ang) * scale.y};
        mat[2] = {100.0f * scale.x, 50.0f * scale.y};
        auto cs = rect.Transform(mat);

        std::cout << "=== Transform (Rotate+Translate+Shear) ===\n";
        std::cout << "  Area: " << cs.Area() << ", NumVert: " << cs.NumVert() << "\n";

        // Extrude...
        //================================后续类似=================================
        {
            // ---------- 信息提取 Lambda ----------
            auto infoExtractor = [](const manifold::CrossSection &cs,
                                    const std::string &name) {
                std::cout << "=== CrossSection Info: " << name << " ===\n";
                std::cout << "  IsEmpty    : " << (cs.IsEmpty() ? "true" : "false")
                          << "\n";
                if (cs.IsEmpty())
                    return;

                auto bounds = cs.Bounds();
                std::cout << "  Area       : " << cs.Area() << "\n";
                std::cout << "  Bounds     : min(" << bounds.min.x << ", " << bounds.min.y
                          << "), max(" << bounds.max.x << ", " << bounds.max.y << ")\n";
                std::cout << "  NumVert    : " << cs.NumVert() << "\n";
                std::cout << "  NumContour : " << cs.NumContour() << "\n";
            };

            // 分别提取外矩形和最终截面的信息
            infoExtractor(cs, "INFO");

            // ---------- 拉伸为 3D 网格 ----------
            auto mesh = manifold::Manifold::Extrude(cs.ToPolygons(), 1e-4).GetMeshGL();
            uint32_t numProp = mesh.numProp;
            uint32_t vCount = mesh.vertProperties.size() / numProp;

            // 2. 提取所有原始顶点坐标（Manifold 输出）
            std::vector<glm::vec3> positions(vCount);
            for (uint32_t i = 0; i < vCount; ++i)
            {
                size_t off = i * numProp;
                positions[i] = {mesh.vertProperties[off], mesh.vertProperties[off + 1],
                                mesh.vertProperties[off + 2]};
            }

            // 3. 筛选顶面三角形（z ≈ thickness），收集原始索引
            const double eps = 1e-6;
            const double thickness = 1e-4;
            const auto &triVerts = mesh.triVerts;
            std::vector<uint32_t> topFaceIndices;

            for (size_t i = 0; i < triVerts.size(); i += 3)
            {
                uint32_t i0 = triVerts[i];
                uint32_t i1 = triVerts[i + 1];
                uint32_t i2 = triVerts[i + 2];
                float z0 = positions[i0].z;
                float z1 = positions[i1].z;
                float z2 = positions[i2].z;
                if (std::abs(z0 - thickness) < eps && std::abs(z1 - thickness) < eps &&
                    std::abs(z2 - thickness) < eps)
                {
                    topFaceIndices.insert(topFaceIndices.end(), {i0, i1, i2});
                }
            }

            // 4. 顶点去重并重新映射索引，同时翻转 Y 坐标（适配 Vulkan NDC）
            std::unordered_map<uint32_t, uint32_t> oldToNew;
            for (uint32_t oldIdx : topFaceIndices)
            {
                if (oldToNew.find(oldIdx) == oldToNew.end())
                {
                    oldToNew[oldIdx] = static_cast<uint32_t>(outVerts.size());
                    // Y 翻转：适配 Vulkan NDC
                    outVerts.push_back({{positions[oldIdx].x, -positions[oldIdx].y},
                                        glm::vec3(1.0f, 0.5f, 0.2f)});
                }
                outIndices.push_back(oldToNew[oldIdx]);
            }

            std::cout << "Manifold mesh: " << outVerts.size() << " vertices, "
                      << outIndices.size() / 3 << " triangles\n";
        }
    };
    // 6. Warp —— 波浪边缘艺术边框
    // 工具函数：将矩形周长均匀采样为多边形顶点
    auto sampleRectPerimeter = [](float width, float height, int samplesPerSide) {
        std::vector<manifold::vec2> points;
        float w2 = width * 0.5f, h2 = height * 0.5f;

        // 底边：左 → 右
        for (int i = 0; i < samplesPerSide; ++i)
        {
            float t = i / (float)samplesPerSide;
            points.push_back({-w2 + width * t, -h2});
        }
        // 右边：下 → 上
        for (int i = 0; i < samplesPerSide; ++i)
        {
            float t = i / (float)samplesPerSide;
            points.push_back({w2, -h2 + height * t});
        }
        // 顶边：右 → 左
        for (int i = 0; i < samplesPerSide; ++i)
        {
            float t = i / (float)samplesPerSide;
            points.push_back({w2 - width * t, h2});
        }
        // 左边：上 → 下
        for (int i = 0; i < samplesPerSide; ++i)
        {
            float t = i / (float)samplesPerSide;
            points.push_back({-w2, h2 - height * t});
        }
        return points;
    };
    auto buildMeshWarp = [&](std::vector<Vertex> &outVerts,
                             std::vector<uint32_t> &outIndices, VkExtent2D screenSize) {
        outVerts.clear();
        outIndices.clear();
        auto [width, height] = screenSize;
        auto scale = 2.0f / manifold::vec2(width, height);

        auto sampledPoints = sampleRectPerimeter(300.0f, 200.0f, 50);
        auto cs =
            manifold::CrossSection(sampledPoints)
                .Warp([](manifold::vec2 &v) { v.y += 15.0f * std::sin(v.x * 0.05f); })
                .Scale(scale);
        std::cout << "=== Warp (Sine Wave Edge) ===\n";
        std::cout << "  Area: " << cs.Area() << ", NumVert: " << cs.NumVert() << "\n";

        // Extrude...
        //================================后续类似=================================
        {
            // ---------- 信息提取 Lambda ----------
            auto infoExtractor = [](const manifold::CrossSection &cs,
                                    const std::string &name) {
                std::cout << "=== CrossSection Info: " << name << " ===\n";
                std::cout << "  IsEmpty    : " << (cs.IsEmpty() ? "true" : "false")
                          << "\n";
                if (cs.IsEmpty())
                    return;

                auto bounds = cs.Bounds();
                std::cout << "  Area       : " << cs.Area() << "\n";
                std::cout << "  Bounds     : min(" << bounds.min.x << ", " << bounds.min.y
                          << "), max(" << bounds.max.x << ", " << bounds.max.y << ")\n";
                std::cout << "  NumVert    : " << cs.NumVert() << "\n";
                std::cout << "  NumContour : " << cs.NumContour() << "\n";
            };

            // 分别提取外矩形和最终截面的信息
            infoExtractor(cs, "INFO");

            // ---------- 拉伸为 3D 网格 ----------
            auto mesh = manifold::Manifold::Extrude(cs.ToPolygons(), 1e-4).GetMeshGL();
            uint32_t numProp = mesh.numProp;
            uint32_t vCount = mesh.vertProperties.size() / numProp;

            // 2. 提取所有原始顶点坐标（Manifold 输出）
            std::vector<glm::vec3> positions(vCount);
            for (uint32_t i = 0; i < vCount; ++i)
            {
                size_t off = i * numProp;
                positions[i] = {mesh.vertProperties[off], mesh.vertProperties[off + 1],
                                mesh.vertProperties[off + 2]};
            }

            // 3. 筛选顶面三角形（z ≈ thickness），收集原始索引
            const double eps = 1e-6;
            const double thickness = 1e-4;
            const auto &triVerts = mesh.triVerts;
            std::vector<uint32_t> topFaceIndices;

            for (size_t i = 0; i < triVerts.size(); i += 3)
            {
                uint32_t i0 = triVerts[i];
                uint32_t i1 = triVerts[i + 1];
                uint32_t i2 = triVerts[i + 2];
                float z0 = positions[i0].z;
                float z1 = positions[i1].z;
                float z2 = positions[i2].z;
                if (std::abs(z0 - thickness) < eps && std::abs(z1 - thickness) < eps &&
                    std::abs(z2 - thickness) < eps)
                {
                    topFaceIndices.insert(topFaceIndices.end(), {i0, i1, i2});
                }
            }

            // 4. 顶点去重并重新映射索引，同时翻转 Y 坐标（适配 Vulkan NDC）
            std::unordered_map<uint32_t, uint32_t> oldToNew;
            for (uint32_t oldIdx : topFaceIndices)
            {
                if (oldToNew.find(oldIdx) == oldToNew.end())
                {
                    oldToNew[oldIdx] = static_cast<uint32_t>(outVerts.size());
                    // Y 翻转：适配 Vulkan NDC
                    outVerts.push_back({{positions[oldIdx].x, -positions[oldIdx].y},
                                        glm::vec3(1.0f, 0.5f, 0.2f)});
                }
                outIndices.push_back(oldToNew[oldIdx]);
            }

            std::cout << "Manifold mesh: " << outVerts.size() << " vertices, "
                      << outIndices.size() / 3 << " triangles\n";
        }
    };
    auto buildMeshHardcoded = [&](std::vector<Vertex> &outVerts,
                                  std::vector<uint32_t> &outIndices,
                                  VkExtent2D screenSize) {
        // buildMeshTranslate(outVerts, outIndices, screenSize);
        // buildMeshRotate(outVerts, outIndices, screenSize);
        // buildMeshScale(outVerts, outIndices, screenSize);
        // buildMeshMirror(outVerts, outIndices, screenSize);
        // buildMeshTransform(outVerts, outIndices, screenSize);
        buildMeshWarp(outVerts, outIndices, screenSize);
    };
    // 生成初始顶点/索引
    std::vector<Vertex> outVerts;
    std::vector<uint32_t> outIndices;
    buildMeshHardcoded(outVerts, outIndices, VkExtent2D{WIDTH, HEIGHT});

    std::cout << "=== Manifold Mesh Data ===\n";
    std::cout << "Vertex count: " << outVerts.size() << "\n";
    for (size_t i = 0; i < outVerts.size(); ++i)
    {
        std::cout << "v[" << i << "]: (" << outVerts[i].pos.x << ", " << outVerts[i].pos.y
                  << ")\n";
    }

    std::cout << "Index count: " << outIndices.size() << "\n";
    std::cout << "Triangles:\n";
    for (size_t i = 0; i < outIndices.size(); i += 3)
    {
        std::cout << "  tri " << i / 3 << ": " << outIndices[i] << ", "
                  << outIndices[i + 1] << ", " << outIndices[i + 2] << "\n";
    }

    // 创建 mesh_base（替换掉原来硬编码的 vertices/indices）
    mesh_base input_mesh{
        allocator, commandPool, GRAPHICS_AND_PRESENT, {outVerts, outIndices}};
    // diff: [test_triangle] end

    auto &indexs = input_mesh.queue_data.indices;

    // diff: end // NOLINTEND
    struct record_info
    {
        uint32_t current_frame;
        uint32_t image_index;
    };
    // NOLINTNEXTLINE
    const auto recordCommandBuffer = [&](const CommandBufferView &commandBuffer,
                                         record_info info) {
        auto [currentFrame, imageIndex] = info;
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

        VkRenderingAttachmentInfo colorAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = imageView,
            .imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}}};

        commandBuffer.beginRendering(
            {.sType = sType<VkRenderingInfo>(),
             .renderArea = {.offset = {.x = 0, .y = 0}, .extent = imageExtent},
             .layerCount = 1,
             .colorAttachmentCount = 1,
             .pColorAttachments = &colorAttachment,
             .pDepthAttachment = nullptr});
        commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *graphicsPipeline);
        commandBuffer.setViewport(0, std::array<VkViewport, 1>{VkViewport{
                                         .x = 0.0F,
                                         .y = 0.0F,
                                         .width = static_cast<float>(imageExtent.width),
                                         .height = static_cast<float>(imageExtent.height),
                                         .minDepth = 0.0F,
                                         .maxDepth = 1.0F}});

        commandBuffer.setScissor(
            0, std::array<VkRect2D, 1>{
                   VkRect2D{.offset = {.x = 0, .y = 0}, .extent = imageExtent}});
        {
            // diff: [new] 即使使用设备地址，Vulkan仍需要绑定索引缓冲区.来变量顶点数组
            uint64_t vertexBufferDeviceAddress =
                input_mesh.getVertexBufferAddress(currentFrame);
            PushConstants pushConstants = {.vertexBufferAddress =
                                               vertexBufferDeviceAddress};

            // NOTE: 当前仅仅推送顶点数据
            commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(PushConstants), &pushConstants);

            commandBuffer.bindIndexBuffer(
                input_mesh.frameBuffers[currentFrame].indexBuffer.buffer(), 0,
                mesh_base::indexType());

            commandBuffer.drawIndexed(static_cast<uint32_t>(indexs.size()), 1, 0, 0, 0);
            // diff: end
        }
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
    const auto recreateSwapChain = [&]() {
        surface.waitGoodFramebufferSize();
        device.waitIdle();

        // diff: [test_triangle] start
        // 获取新尺寸（假设 window 是 GLFWwindow*，您代码中已有 surface 对象）
        // 更新 Yoga 根节点尺寸
        // 生成适应新尺寸的顶点数据
        std::vector<Vertex> newVerts;
        std::vector<uint32_t> newIndices;
        buildMeshHardcoded(newVerts, newIndices, window.getFramebufferSize());

        // 排队更新网格（下一帧 requestUpdate 会上传）
        input_mesh.queueUpdate(std::move(newVerts), std::move(newIndices));
        // diff: [test_triangle] end

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

        // diff: vkResetFences 之后更新顶点. 仅仅更新飞行的帧
        input_mesh.requestUpdate(allocator, currentFrame);
        // diff: end
        recordCommandBuffer(commandBuffer,
                            {.current_frame = currentFrame, .image_index = imageIndex});

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

        // diff: [test_triangle] start
        auto now = std::chrono::steady_clock::now();
        static auto lastUpdate = std::chrono::steady_clock::now();
        if (now - lastUpdate > std::chrono::seconds(2))
        {
            lastUpdate = now;

            // 随机修改某个子节点的 flexGrow 或 margin
            static std::mt19937 rng(
                std::chrono::steady_clock::now().time_since_epoch().count());
            static std::uniform_real_distribution<float> flexDist(0.5f, 3.0f);
            static std::uniform_real_distribution<float> marginDist(0.0f, 15.0f);
            // TODO(mcs): 简化不更新
        }
        // diff: [test_triangle] end
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