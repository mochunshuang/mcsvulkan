#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>

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
        constexpr buffer_base(const LogicalDevice &device, VkBuffer buffer,
                              VkDeviceMemory bufferMemory) noexcept
            : device_{&device}, buffer_{buffer}, bufferMemory_{bufferMemory}
        {
        }
        constexpr ~buffer_base() noexcept
        {
            destroy();
        }
        buffer_base(const buffer_base &) = delete;
        buffer_base &operator=(const buffer_base &) = delete;

        constexpr buffer_base(buffer_base &&other) noexcept
            : device_{std::exchange(other.device_, nullptr)},
              buffer_{std::exchange(other.buffer_, nullptr)},
              bufferMemory_{std::exchange(other.bufferMemory_, nullptr)}
        {
        }

        constexpr buffer_base &operator=(buffer_base &&other) noexcept
        {
            if (&other != this)
            {
                this->destroy();
                device_ = std::exchange(other.device_, nullptr);
                buffer_ = std::exchange(other.buffer_, nullptr);
                bufferMemory_ = std::exchange(other.bufferMemory_, nullptr);
            }
            return *this;
        }

        [[nodiscard]] constexpr VkBuffer buffer() const noexcept
        {
            return buffer_;
        }
        [[nodiscard]] constexpr VkDeviceMemory bufferMemory() const noexcept
        {
            return bufferMemory_;
        }

        [[nodiscard]] constexpr auto *device() const noexcept
        {
            return device_;
        }
        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return device_ != nullptr && buffer_ != nullptr && bufferMemory_ != nullptr;
        }

        [[nodiscard]] void *map(size_t size, VkMemoryMapFlags flags = 0) const
        {
            void *data; // NOLINT
            device_->mapMemory(bufferMemory_, 0, size, flags, &data);
            return data;
        }
        void unmap() const noexcept
        {
            device_->unmapMemory(bufferMemory_);
        }
        void copyDataToBuffer(const void *src, size_t size,
                              VkMemoryMapFlags flags = 0) const
        {
            void *data; // NOLINT
            device_->mapMemory(bufferMemory(), 0, size, flags, &data);
            ::memcpy(data, src, size);
            device_->unmapMemory(bufferMemory());
        }
        void clear()
        {
            destroy();
        }

      private:
        const LogicalDevice *device_{};
        VkBuffer buffer_{};
        VkDeviceMemory bufferMemory_{};

        constexpr void destroy() noexcept
        {
            if (device_ != nullptr)
            {
                if (buffer_ != nullptr)
                    device_->destroyBuffer(buffer_, device_->allocator());

                // https://docs.vulkan.org/refpages/latest/refpages/source/vkFreeMemory.html#:~:text=If%20a%20memory%20object%20is%20mapped%20at%20the%20time%20it%20is%20freed%2C%20it%20is%20implicitly%20unmapped
                //  If a memory object is mapped at the time it is freed, it is implicitly
                //  unmapped
                //   unmap(); //NOTE: 不需要
                if (bufferMemory_ != nullptr)
                    device_->freeMemory(bufferMemory_, device_->allocator());

                device_ = nullptr;
                buffer_ = nullptr;
                bufferMemory_ = nullptr;
            }
        }
    };

    [[nodiscard]] auto findMemoryType(const PhysicalDevice *physicalDevice,
                                      uint32_t typeFilter,
                                      VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties =
            physicalDevice->getMemoryProperties();

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if (((typeFilter & (1 << i)) != 0U) && // NOLINTNEXTLINE
                ((memProperties.memoryTypes[i].propertyFlags) & properties) == properties)
                return i;
        }
        throw std::runtime_error("failed to find suitable memory type!");
    }
    [[nodiscard]] static constexpr buffer_base create_buffer(
        const LogicalDevice &device, const VkBufferCreateInfo &bufferInfo,
        VkMemoryPropertyFlags properties)
    {
        VkBuffer buffer = nullptr;
        VkDeviceMemory bufferMemory = nullptr;

        try
        {
            buffer = device.createBuffer(bufferInfo, device.allocator());
            VkMemoryRequirements memRequirements =
                device.getBufferMemoryRequirements(buffer);

            VkMemoryAllocateInfo allocInfo{
                .sType = sType<VkMemoryAllocateInfo>(),
                .allocationSize = memRequirements.size,
                .memoryTypeIndex = findMemoryType(
                    device.physicalDevice(), memRequirements.memoryTypeBits, properties)};

            bufferMemory = device.allocateMemory(allocInfo, device.allocator());
            device.bindBufferMemory(buffer, bufferMemory, 0);
            return buffer_base{device, buffer, bufferMemory};
        }
        catch (...)
        {
            if (buffer != nullptr)
                device.destroyBuffer(buffer, device.allocator());
            if (bufferMemory != nullptr)
                device.freeMemory(bufferMemory, device.allocator());
            throw;
        }
    }

    template <typename... BN, typename... MN>
    [[nodiscard]] static constexpr buffer_base create_buffer(
        const LogicalDevice &device,
        structure_chain<VkBufferCreateInfo, BN...> bufferInfo,
        structure_chain<VkMemoryAllocateInfo, MN...> allocInfo,
        VkMemoryPropertyFlagBits properties)
    {
        VkBuffer buffer = nullptr;
        VkDeviceMemory bufferMemory = nullptr;
        try
        {
            buffer = device.createBuffer(bufferInfo.head(), device.allocator());
            VkMemoryRequirements memRequirements =
                device.getBufferMemoryRequirements(buffer);

            allocInfo.head().allocationSize = memRequirements.size;
            allocInfo.head().memoryTypeIndex = findMemoryType(
                device.physicalDevice(), memRequirements.memoryTypeBits, properties);

            bufferMemory = device.allocateMemory(allocInfo.head(), device.allocator());
            device.bindBufferMemory(buffer, bufferMemory, 0);
            return buffer_base{device, buffer, bufferMemory};
        }
        catch (...)
        {
            if (buffer != nullptr)
                device.destroyBuffer(buffer, device.allocator());
            if (bufferMemory != nullptr)
                device.freeMemory(bufferMemory, device.allocator());
            throw;
        }
    }

    static constexpr auto staging_buffer(const LogicalDevice &device, size_t buffer_size)
    {
        return create_buffer(device,
                             {.sType = sType<VkBufferCreateInfo>(),
                              .size = buffer_size,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    static constexpr auto simple_copy_buffer(const CommandPool &commandpool,
                                             const Queue &queue, VkBuffer srcBuffer,
                                             VkBuffer dstBuffer,
                                             const std::vector<VkBufferCopy> &regions)
    {
        const auto *logicalDevice = commandpool.device();
        CommandBuffer commandCopyBuffer = commandpool.allocateOneCommandBuffer(
            {.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY});

        commandCopyBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>(),
                                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
        std::span span(regions.begin(), regions.end());
        commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, span);
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
        VkDeviceAddress vertexBufferAddress = 0; // diff: [new] 设备地址
    };
    // NOLINTEND

    static consteval auto indexType()
    {
        return VK_INDEX_TYPE_UINT32; // 必须匹配 index_type
    }

    struct mesh_data
    {
        std::vector<Vertex> vertices;
        std::vector<index_type> indices;
        void clear()
        {
            vertices.clear(); // NOTE: 顶点大小还有用.
        }
        auto valid()
        {
            return vertices.size() > 0;
        }
    };

    const CommandPool *commandpool{};
    const Queue *queue{};

    std::array<FrameBuffers, MAX_FRAMES_IN_FLIGHT> frameBuffers;
    mesh_data queue_data;
    uint32_t count = 0;
    mesh_base(const CommandPool &pool, const Queue &queue, mesh_data init_data)
        : commandpool{&pool}, queue{&queue}, queue_data{std::move(init_data)}
    {
        //
        for (auto &fb : frameBuffers)
        {
            createVertexBufferForFrame(fb);
            createIndexBufferForFrame(fb);
            // diff: 第二步: 获取设备地址,传递给shader 不再通过 描述符传递
            getBufferDeviceAddresses(fb);
        }
    }

    // diff: 第一步: buffer 内存要求
    static constexpr auto REQUIRE_BUFFER_USAGE =
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    static constexpr auto REQUIRE_MEMORY_FLAG = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    constexpr void createVertexBufferForFrame(FrameBuffers &fb)
    {
        auto &vertices = queue_data.vertices;
        const VkDeviceSize BUFFER_SIZE = sizeof(vertices[0]) * vertices.size();
        const auto *device = commandpool->device();
        auto &destBuffer = fb.vertexBuffer;
        constexpr auto USAGE = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        destBuffer = create_buffer(
            *device,
            structure_chain<VkBufferCreateInfo>{
                {.size = BUFFER_SIZE,
                 .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | USAGE | REQUIRE_BUFFER_USAGE,
                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE}},
            structure_chain<VkMemoryAllocateInfo, VkMemoryAllocateFlagsInfo>{
                {}, {.flags = REQUIRE_MEMORY_FLAG}},
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        const auto STAGING_BUFFER = staging_buffer(*device, BUFFER_SIZE);
        STAGING_BUFFER.copyDataToBuffer(vertices.data(), BUFFER_SIZE);
        simple_copy_buffer(*commandpool, *queue, STAGING_BUFFER.buffer(),
                           destBuffer.buffer(), {VkBufferCopy{.size = BUFFER_SIZE}});
    }
    void createIndexBufferForFrame(FrameBuffers &fb)
    {
        auto &vertices = queue_data.indices;
        const VkDeviceSize BUFFER_SIZE = sizeof(vertices[0]) * vertices.size();
        const auto *device = commandpool->device();
        auto &destBuffer = fb.indexBuffer;
        constexpr auto USAGE = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        destBuffer = create_buffer(
            *device,
            structure_chain<VkBufferCreateInfo>{
                {.size = BUFFER_SIZE,
                 .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | USAGE | REQUIRE_BUFFER_USAGE,
                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE}},
            structure_chain<VkMemoryAllocateInfo, VkMemoryAllocateFlagsInfo>{
                {}, {.flags = REQUIRE_MEMORY_FLAG}},
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        const auto STAGING_BUFFER = staging_buffer(*device, BUFFER_SIZE);
        STAGING_BUFFER.copyDataToBuffer(vertices.data(), BUFFER_SIZE);
        simple_copy_buffer(*commandpool, *queue, STAGING_BUFFER.buffer(),
                           destBuffer.buffer(), {VkBufferCopy{.size = BUFFER_SIZE}});
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
        return frameBuffers[currentFrame].vertexBufferAddress; // NOLINT
    }

    // 只排队，不立即执行
    void queueUpdate(std::vector<Vertex> vertices, std::vector<index_type> indices)
    {
        queue_data.vertices = std::move(vertices);
        queue_data.indices = std::move(indices);
        count = 2;
    }
    void applyQueuedUpdate(uint32_t currentFrame)
    {
        if (count == 0) // NOTE: main 线程安全的
        {
            queue_data.clear();
            return;
        }
        --count;

        // 更新GPU缓冲区（这个帧当前没有被GPU使用）
        auto &fb = frameBuffers[currentFrame]; // NOLINT
        createVertexBufferForFrame(fb);
        createIndexBufferForFrame(fb);
        // diff: [new] 更新设备地址
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

                       query_vulkan12_features
                           .bufferDeviceAddress && // diff: [new]
                                                   // 检查Vulkan 1.2中的bufferDeviceAddress
                       query_vulkan12_features
                           .scalarBlockLayout && // diff: [new]
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

    // diff: 准备好顶点和顶点索引--------------- // NOLINTBEGIN
    // 顶点数据（四边形由两个三角形组成）
    const std::vector<Vertex> vertices = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}}, // 左下
        {{0.5, -0.5}, {0.0f, 1.0f, 0.0f}},    // 右下
        {{0.5, 0.5}, {0.0f, 0.0f, 1.0f}},     // 右上
        {{-0.5, 0.5}, {1.0f, 0.0f, 0.0f}}     // 左上
    };

    // 索引数据
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    mesh_base input_mesh{commandPool, GRAPHICS_AND_PRESENT, {vertices, indices}};

    auto &indexs = input_mesh.queue_data.indices;

    // diff: end // NOLINTEND

    // NOLINTNEXTLINE
    const auto recordCommandBuffer = [&](const CommandBufferView &commandBuffer,
                                         uint32_t currentFrame, uint32_t imageIndex) {
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

        // diff: vkResetFences 之后更新顶点. 仅仅更新飞行的帧
        input_mesh.applyQueuedUpdate(currentFrame);
        // diff: end
        recordCommandBuffer(commandBuffer, currentFrame, imageIndex);

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

        // diff:     // 检查是否需要更新形状
        auto now = std::chrono::steady_clock::now();
        static auto lastUpdate = std::chrono::steady_clock::now();
        if (now - lastUpdate > std::chrono::seconds(2))
        {
            lastUpdate = now;

            static bool isTriangle = false;
            if (isTriangle)
            {
                // 只记录要更新的数据，不立即执行
                // 切换到三角形 // NOLINTBEGIN
                static const std::vector<Vertex> VERTICES = {
                    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
                    {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
                    {{0.0f, 0.5f}, {0.0f, 0.0f, 1.0f}}};
                static const std::vector<uint32_t> INDICES = {0, 1, 2}; // NOLINTEND
                input_mesh.queueUpdate(VERTICES, INDICES);
            }
            else
            {
                // 只记录要更新的数据，不立即执行
                input_mesh.queueUpdate(vertices, indices);
            }
            isTriangle = !isTriangle;
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