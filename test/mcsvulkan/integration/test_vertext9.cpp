#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>
#include <string_view>
#include <utility>
#include <vector>
#include <random>
#include <list>
#include <cstring>
#include <chrono>
#include <unordered_map>

#include "../head.hpp"

// ======================== 原有 using 声明（全部保留） ========================
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

using mcs::vulkan::memory::create_buffer;
using mcs::vulkan::memory::create_staging_buffer;
using mcs::vulkan::memory::auto_map_buffer;
using mcs::vulkan::memory::find_memory_type_index;
using mcs::vulkan::memory::buffer_base;
using mcs::vulkan::tool::simple_copy_buffer;
using mcs::vulkan::tool::pNext;

// ---------- 子分配信息（纯数据） ----------
struct SubAllocation
{
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void *mappedPtr = nullptr;
};

// ---------- 子分配器 ----------
class SubAllocator
{
  public:
    SubAllocator(const LogicalDevice &device, uint32_t memoryTypeIndex,
                 VkDeviceSize blockSize, pNext pnext, bool mapAll = true)
        : device_(&device), typeIndex_(memoryTypeIndex), blockSize_(blockSize),
          pNext_(std::move(pnext)), mapAll_(mapAll)
    {
    }
    template <typename GenIdx>
    SubAllocator(const LogicalDevice &device, GenIdx gen, VkDeviceSize blockSize,
                 pNext pnext, bool mapAll = true)
        : SubAllocator(device, gen(device), blockSize, std::move(pnext), mapAll)
    {
    }

    SubAllocator(const SubAllocator &) = delete;
    SubAllocator &operator=(const SubAllocator &) = delete;

    SubAllocation allocate(const VkMemoryRequirements &memReqs)
    {
        const VkDeviceSize reqSize = memReqs.size;
        const VkDeviceSize alignment = memReqs.alignment;

        for (auto &block : blocks_)
        {
            VkDeviceSize offset = 0;
            if (block.tryAlloc(reqSize, alignment, offset))
            {
                return {.memory = block.memory,
                        .offset = offset,
                        .size = reqSize,
                        .mappedPtr = block.mapped
                                         ? static_cast<char *>(block.mapBase) + offset
                                         : nullptr};
            }
        }
        VkDeviceSize allocSize = std::max(blockSize_, reqSize);
        std::println("[SubAlloc] Need new block! Request={} bytes, Allocating={} bytes",
                     reqSize, allocSize);
        auto newBlock = Block(device_, typeIndex_, allocSize, pNext_.value(), mapAll_);
        VkDeviceSize offset = 0;
        if (!newBlock.tryAlloc(reqSize, alignment, offset))
            throw std::runtime_error("SubAllocator: internal allocation failure");

        SubAllocation result{
            .memory = newBlock.memory,
            .offset = offset,
            .size = reqSize,
            .mappedPtr = newBlock.mapped ? static_cast<char *>(newBlock.mapBase) + offset
                                         : nullptr};
        blocks_.push_back(std::move(newBlock));
        return result;
    }

    void free(const SubAllocation &alloc)
    {
        if (alloc.size == 0)
            return;
        for (auto &block : blocks_)
        {
            if (block.memory == alloc.memory)
            {
                block.dealloc(alloc.offset, alloc.size);
                return;
            }
        }
        std::cerr << "[SubAlloc] Warning: free unknown memory " << alloc.memory << "\n";
    }

  private:
    struct Block
    {
        const LogicalDevice *device;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        void *mapBase = nullptr;
        bool mapped = false;
        std::vector<std::pair<VkDeviceSize, VkDeviceSize>> freeList;

        Block(const LogicalDevice *dev, uint32_t typeIndex, VkDeviceSize sz,
              const void *pNext, bool doMap)
            : device{dev},
              memory{dev->allocateMemory(
                  VkMemoryAllocateInfo{.sType = sType<VkMemoryAllocateInfo>(),
                                       .pNext = pNext,
                                       .allocationSize = sz,
                                       .memoryTypeIndex = typeIndex},
                  dev->allocator())},
              size(sz), mapped(doMap)
        {
            assert(device != nullptr);
            if (mapped)
                dev->mapMemory(memory, 0, VK_WHOLE_SIZE, 0, &mapBase);
            freeList.emplace_back(0, sz);
            std::println("[SubAlloc] Created new Block: size={} ({} MB), memory={}", sz,
                         sz / (1024 * 1024), (void *)memory);
        }

        ~Block() noexcept
        {
            if (device != nullptr)
            {
                if (mapped)
                    device->unmapMemory(memory);
                if (memory != nullptr)
                {
                    std::println(
                        "[SubAlloc] Destroying Block: size={} ({} MB), memory={}", size,
                        size / (1024 * 1024), (void *)memory);
                    device->freeMemory(memory, device->allocator());
                }
                device = nullptr;
                memory = {};
                size = {};
                mapBase = {};
                mapped = {};
                freeList = {};
            }
        }

        Block(const Block &) = delete;
        Block &operator=(const Block &) = delete;
        Block(Block &&o) noexcept
            : device{std::exchange(o.device, {})}, memory{std::exchange(o.memory, {})},
              size{std::exchange(o.size, {})}, mapBase{std::exchange(o.mapBase, {})},
              mapped{std::exchange(o.mapped, {})}, freeList{std::exchange(o.freeList, {})}
        {
        }
        Block &operator=(Block &&o) noexcept
        {
            if (this != &o)
            {
                if (device != nullptr)
                {
                    if (mapped)
                        device->unmapMemory(memory);
                    if (memory != nullptr)
                    {
                        std::println(
                            "[SubAlloc] Destroying Block: size={} ({} MB), memory={}",
                            size, size / (1024 * 1024), (void *)memory);
                        device->freeMemory(memory, device->allocator());
                    }
                }
                device = std::exchange(o.device, {});
                memory = std::exchange(o.memory, {});
                size = std::exchange(o.size, {});
                mapBase = std::exchange(o.mapBase, {});
                mapped = std::exchange(o.mapped, {});
                freeList = std::exchange(o.freeList, {});
            }
            return *this;
        }

        static VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        bool tryAlloc(VkDeviceSize reqSize, VkDeviceSize alignment,
                      VkDeviceSize &outOffset)
        {
            for (size_t idx = 0; idx < freeList.size(); ++idx)
            {
                VkDeviceSize regionStart = freeList[idx].first;
                VkDeviceSize regionSize = freeList[idx].second;
                VkDeviceSize regionEnd = regionStart + regionSize;

                VkDeviceSize allocStart = alignUp(regionStart, alignment);
                if (allocStart + reqSize > regionEnd)
                    continue;

                VkDeviceSize beforeSize = allocStart - regionStart;
                VkDeviceSize afterSize = regionEnd - (allocStart + reqSize);

                freeList.erase(freeList.begin() + idx);
                if (beforeSize > 0)
                {
                    freeList.insert(freeList.begin() + idx, {regionStart, beforeSize});
                    if (afterSize > 0)
                        freeList.insert(freeList.begin() + idx + 1,
                                        {allocStart + reqSize, afterSize});
                }
                else if (afterSize > 0)
                {
                    freeList.insert(freeList.begin() + idx,
                                    {allocStart + reqSize, afterSize});
                }

                outOffset = allocStart;
                return true;
            }
            return false;
        }

        void dealloc(VkDeviceSize off, VkDeviceSize sz)
        {
            if (sz == 0)
                return;
            size_t idx = 0;
            while (idx < freeList.size() && freeList[idx].first < off)
                ++idx;

            if (idx > 0)
            {
                auto &prev = freeList[idx - 1];
                if (prev.first + prev.second == off)
                {
                    off = prev.first;
                    sz += prev.second;
                    freeList.erase(freeList.begin() + idx - 1);
                    --idx;
                }
            }
            if (idx < freeList.size() && off + sz == freeList[idx].first)
            {
                sz += freeList[idx].second;
                freeList.erase(freeList.begin() + idx);
            }
            freeList.insert(freeList.begin() + idx, {off, sz});
            std::println("[SubAlloc] Free offset={} size={} -> freeList size={}", off, sz,
                         freeList.size());
        }
    };

    const LogicalDevice *device_;
    uint32_t typeIndex_;
    VkDeviceSize blockSize_;
    pNext pNext_;
    bool mapAll_;
    std::vector<Block> blocks_;
};

// ---------- 自管理缓冲区 ----------
struct SubAllocatedBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    SubAllocation alloc;
    LogicalDevice *device = nullptr;
    SubAllocator *subAlloc = nullptr;

    SubAllocatedBuffer() = default;
    SubAllocatedBuffer(VkBuffer buf, const SubAllocation &al, LogicalDevice *dev,
                       SubAllocator *allocator)
        : buffer(buf), alloc(al), device(dev), subAlloc(allocator)
    {
    }

    ~SubAllocatedBuffer()
    {
        if (buffer && device)
            device->destroyBuffer(buffer, device->allocator());
        if (subAlloc && alloc.size > 0)
            subAlloc->free(alloc);
    }

    SubAllocatedBuffer(SubAllocatedBuffer &&other) noexcept
        : buffer(std::exchange(other.buffer, VK_NULL_HANDLE)),
          alloc(std::exchange(other.alloc, {})),
          device(std::exchange(other.device, nullptr)),
          subAlloc(std::exchange(other.subAlloc, nullptr))
    {
    }

    SubAllocatedBuffer &operator=(SubAllocatedBuffer &&other) noexcept
    {
        if (this != &other)
        {
            if (buffer && device)
                device->destroyBuffer(buffer, device->allocator());
            if (subAlloc && alloc.size > 0)
                subAlloc->free(alloc);
            buffer = std::exchange(other.buffer, VK_NULL_HANDLE);
            alloc = std::exchange(other.alloc, {});
            device = std::exchange(other.device, nullptr);
            subAlloc = std::exchange(other.subAlloc, nullptr);
        }
        return *this;
    }

    SubAllocatedBuffer(const SubAllocatedBuffer &) = delete;
    SubAllocatedBuffer &operator=(const SubAllocatedBuffer &) = delete;

    VkDeviceAddress getDeviceAddress() const
    {
        if (!device || !buffer)
            return 0;
        VkBufferDeviceAddressInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = buffer;
        return device->getBufferDeviceAddress(info);
    }
};

struct Vertex
{
    glm::vec2 pos;
    static constexpr auto vertex_input_state() noexcept
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

struct InstanceData
{
    glm::vec2 offset;
    glm::vec3 color;
};

struct PushData
{
    uint64_t instance_address;
};

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
        VkDependencyInfo depInfo = {.sType = sType<VkDependencyInfo>(),
                                    .imageMemoryBarrierCount = 1,
                                    .pImageMemoryBarriers = &barrier};
        commandBuffer.pipelineBarrier2(depInfo);
    }
};

struct Mesh
{
    uint32_t vertexOffset;
    uint32_t indexOffset;
    uint32_t indexCount;

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

// ================== 显式绑定命令与数据的结构体 ==================
struct DrawCall
{
    const Mesh *mesh;                    // 绘制指令（哪个网格）
    uint32_t instanceCount;              // 实例数量
    std::vector<InstanceData> instances; // 实例数据（CPU 端存储）
    uint32_t firstInstance = 0;          // 在全局 buffer 中的起始索引（自动计算）
};

// ------------------------- main 函数开始 -------------------------
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
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};

    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan12Features,
                    VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        enablefeatureChain = {
            {.features = {.multiDrawIndirect = VK_TRUE, .shaderInt64 = VK_TRUE}},
            {.scalarBlockLayout = VK_TRUE, .bufferDeviceAddress = VK_TRUE},
            {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
            {.extendedDynamicState = VK_TRUE}};

    auto [id, physical_device] =
        physical_device_selector{instance}
            .requiredDeviceExtension(requiredDeviceExtension)
            .requiredProperties(
                [](const VkPhysicalDeviceProperties &props) constexpr noexcept {
                    return props.apiVersion >= VK_API_VERSION_1_3;
                })
            .requiredQueueFamily(
                [](const VkQueueFamilyProperties &qfp) constexpr noexcept {
                    return !!(qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT);
                })
            .requiredFeatures([](const PhysicalDevice &pd) constexpr noexcept -> bool {
                auto query =
                    structure_chain<VkPhysicalDeviceFeatures2,
                                    VkPhysicalDeviceVulkan13Features,
                                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>{
                        {}, {}, {}};
                pd.getFeatures2(&query.head());
                auto &v13 = query.template get<VkPhysicalDeviceVulkan13Features>();
                auto &ext =
                    query.template get<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>();
                return v13.dynamicRendering && v13.synchronization2 &&
                       ext.extendedDynamicState;
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
                {.pNext = make_pNext(enablefeatureChain),
                 .queueCreateInfos = create_logical_device::makeQueueCreateInfos(
                     create_logical_device::queue_create_info{
                         .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX,
                         .queueCount = 1,
                         .queuePrioritie = 1.0}),
                 .enabledExtensions = requiredDeviceExtension})
            .build(physical_device);
    requiredDeviceExtension.clear();

    const auto GRAPHICS_AND_PRESENT = Queue(
        device, {.queue_family_index = GRAPHICS_QUEUE_FAMILY_IDX, .queue_index = 0});

    auto swapchainBuild =
        create_swap_chain{surface, device}
            .setCreateInfo(
                {.changeMinImageCount = [](uint32_t m) noexcept { return m + 1; },
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
                {.setLayouts = {},
                 .pushConstantRanges = {{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                         .offset = 0,
                                         .size = sizeof(PushData)}}})
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
                 .vertexInputState = Vertex::vertex_input_state(),
                 .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                        .primitiveRestartEnable = VK_FALSE},
                 .tessellationState = {},
                 .viewportState = {.viewports = {VkViewport{}}, .scissors = {VkRect2D{}}},
                 .rasterizationState = {.depthClampEnable = VK_FALSE,
                                        .rasterizerDiscardEnable = VK_FALSE,
                                        .polygonMode = VK_POLYGON_MODE_FILL,
                                        .cullMode = VK_CULL_MODE_BACK_BIT,
                                        .frontFace = VK_FRONT_FACE_CLOCKWISE,
                                        .depthBiasEnable = VK_FALSE,
                                        .lineWidth = 1.0F},
                 .multisampleState = {.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                                      .sampleShadingEnable = VK_FALSE},
                 .depthStencilState = {},
                 .colorBlendState = {.logicOpEnable = VK_FALSE,
                                     .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                                     .attachments = {{.blendEnable = VK_FALSE,
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

    // ====================== 几何数据准备 ======================
    static constexpr float scaling_factor = 000.1;
    static constexpr uint32_t INSTANCE_COUNT = 10000;
    const std::vector<Vertex> quadVertices = {{glm::vec2{-0.1f, -0.1f} * scaling_factor},
                                              {glm::vec2{0.1f, -0.1f} * scaling_factor},
                                              {glm::vec2{0.1f, 0.1f} * scaling_factor},
                                              {glm::vec2{-0.1f, 0.1f} * scaling_factor}};
    const std::vector<uint32_t> quadIndices = {0, 1, 2, 0, 2, 3};

    const std::vector<Vertex> triVertices = {{glm::vec2{-0.1f, -0.1f} * scaling_factor},
                                             {glm::vec2{0.1f, -0.1f} * scaling_factor},
                                             {glm::vec2{0.0f, 0.1f} * scaling_factor}};
    const std::vector<uint32_t> triIndices = {0, 1, 2};

    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;
    struct StringHash
    {
        using is_transparent = std::true_type; // 启用透明查找

        size_t operator()(std::string_view sv) const noexcept
        {
            return std::hash<std::string_view>{}(sv);
        }
        size_t operator()(const std::string &s) const noexcept
        {
            return std::hash<std::string_view>{}(s);
        }
    };

    struct StringEqual
    {
        using is_transparent = std::true_type;

        bool operator()(std::string_view lhs, std::string_view rhs) const noexcept
        {
            return lhs == rhs;
        }
    };
    // 使用自定义透明哈希/比较的 unordered_map
    std::unordered_map<std::string, Mesh, StringHash, StringEqual> meshMap;

    uint32_t quadVOff = static_cast<uint32_t>(allVertices.size());
    uint32_t quadIOff = static_cast<uint32_t>(allIndices.size());
    allVertices.insert(allVertices.end(), quadVertices.begin(), quadVertices.end());
    allIndices.insert(allIndices.end(), quadIndices.begin(), quadIndices.end());
    meshMap["quad"] = {quadVOff, quadIOff, static_cast<uint32_t>(quadIndices.size())};

    uint32_t triVOff = static_cast<uint32_t>(allVertices.size());
    uint32_t triIOff = static_cast<uint32_t>(allIndices.size());
    allVertices.insert(allVertices.end(), triVertices.begin(), triVertices.end());
    allIndices.insert(allIndices.end(), triIndices.begin(), triIndices.end());
    meshMap["triangle"] = {triVOff, triIOff, static_cast<uint32_t>(triIndices.size())};

    // ====================== 子分配器初始化 ======================
    auto getMemoryTypeIndex = [&](VkBufferUsageFlags usage) -> uint32_t {
        VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                      .size = 256,
                                      .usage = usage,
                                      .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VkDeviceBufferMemoryRequirements deviceReq{
            .sType = sType<VkDeviceBufferMemoryRequirements>(),
            .pCreateInfo = &bufferInfo};
        VkMemoryRequirements2 memReqs2{.sType = sType<VkMemoryRequirements2>()};
        device.getDeviceBufferMemoryRequirements(&deviceReq, &memReqs2);

        VkPhysicalDeviceMemoryProperties memProps = physical_device.getMemoryProperties();
        uint32_t idx = find_memory_type_index(
            memReqs2.memoryRequirements.memoryTypeBits, memProps,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (idx == UINT32_MAX)
            idx = find_memory_type_index(memReqs2.memoryRequirements.memoryTypeBits,
                                         memProps,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        assert(idx != UINT32_MAX);
        return idx;
    };

    SubAllocator vertexAlloc(device,
                             getMemoryTypeIndex(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
                             1 * 1024 * 1024, nullptr, true);
    SubAllocator indexAlloc(device, getMemoryTypeIndex(VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
                            1 * 1024 * 1024, nullptr, true);
    SubAllocator indirectAlloc(device,
                               getMemoryTypeIndex(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT),
                               1 * 1024 * 1024, nullptr, true);
    SubAllocator instanceAlloc(
        device,
        getMemoryTypeIndex(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT),
        2 * 1024 * 1024,
        make_pNext(structure_chain<VkMemoryAllocateFlagsInfo>{
            {.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT}}),
        true);

    auto createVertexBuffer = [&](VkDeviceSize size) -> SubAllocatedBuffer {
        VkBufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                .size = size,
                                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VkBuffer buf = device.createBuffer(info, device.allocator());
        VkMemoryRequirements memReqs = device.getBufferMemoryRequirements(buf);
        SubAllocation alloc = vertexAlloc.allocate(memReqs);
        device.bindBufferMemory(buf, alloc.memory, alloc.offset);
        return SubAllocatedBuffer(buf, alloc, &device, &vertexAlloc);
    };

    auto createIndexBuffer = [&](VkDeviceSize size) -> SubAllocatedBuffer {
        VkBufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                .size = size,
                                .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VkBuffer buf = device.createBuffer(info, device.allocator());
        VkMemoryRequirements memReqs = device.getBufferMemoryRequirements(buf);
        SubAllocation alloc = indexAlloc.allocate(memReqs);
        device.bindBufferMemory(buf, alloc.memory, alloc.offset);
        return SubAllocatedBuffer(buf, alloc, &device, &indexAlloc);
    };

    auto createIndirectBuffer = [&](VkDeviceSize size) -> SubAllocatedBuffer {
        VkBufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                .size = size,
                                .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VkBuffer buf = device.createBuffer(info, device.allocator());
        VkMemoryRequirements memReqs = device.getBufferMemoryRequirements(buf);
        SubAllocation alloc = indirectAlloc.allocate(memReqs);
        device.bindBufferMemory(buf, alloc.memory, alloc.offset);
        return SubAllocatedBuffer(buf, alloc, &device, &indirectAlloc);
    };

    auto createInstanceBuffer = [&](VkDeviceSize size) -> SubAllocatedBuffer {
        VkBufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                .size = size,
                                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VkBuffer buf = device.createBuffer(info, device.allocator());
        VkMemoryRequirements memReqs = device.getBufferMemoryRequirements(buf);
        SubAllocation alloc = instanceAlloc.allocate(memReqs);
        device.bindBufferMemory(buf, alloc.memory, alloc.offset);
        return SubAllocatedBuffer(buf, alloc, &device, &instanceAlloc);
    };

    // ====================== 创建全局只读缓冲 ======================
    SubAllocatedBuffer globalVertexBuffer =
        createVertexBuffer(allVertices.size() * sizeof(Vertex));
    memcpy(globalVertexBuffer.alloc.mappedPtr, allVertices.data(),
           allVertices.size() * sizeof(Vertex));

    SubAllocatedBuffer globalIndexBuffer =
        createIndexBuffer(allIndices.size() * sizeof(uint32_t));
    memcpy(globalIndexBuffer.alloc.mappedPtr, allIndices.data(),
           allIndices.size() * sizeof(uint32_t));

    constexpr uint32_t MAX_DRAW_CALLS = 2;
    constexpr VkDeviceSize MAX_INSTANCE_DATA_SIZE =
        MAX_DRAW_CALLS * INSTANCE_COUNT * sizeof(InstanceData);
    SubAllocatedBuffer globalInstanceBuffer =
        createInstanceBuffer(MAX_INSTANCE_DATA_SIZE);
    VkDeviceAddress globalInstanceAddress = globalInstanceBuffer.getDeviceAddress();

    // 每帧独立的间接命令缓冲
    std::array<SubAllocatedBuffer, MAX_FRAMES_IN_FLIGHT> indirectBuffers;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        indirectBuffers[i] =
            createIndirectBuffer(sizeof(VkDrawIndexedIndirectCommand) * MAX_DRAW_CALLS);

    // ====================== 命令录制 ======================
    const auto recordCommandBuffer = [&](const CommandBufferView &commandBuffer,
                                         uint32_t imageIndex, uint32_t frameIndex,
                                         uint32_t drawCount) {
        VkImage image = swapchain.image(imageIndex);
        VkImageView imageView = swapchain.imageView(imageIndex);
        auto imageExtent = swapchain.imageExtent();

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});

        my_render::transition_image_layout(
            commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {},
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkRenderingAttachmentInfo colorAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = imageView,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}}};

        commandBuffer.beginRendering(
            {.sType = sType<VkRenderingInfo>(),
             .renderArea = {.offset = {.x = 0, .y = 0}, .extent = imageExtent},
             .layerCount = 1,
             .colorAttachmentCount = 1,
             .pColorAttachments = &colorAttachment});

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

        commandBuffer.bindVertexBuffers(0, 1, globalVertexBuffer.buffer, 0);
        commandBuffer.bindIndexBuffer(globalIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        PushData pushConstants = {globalInstanceAddress};
        commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                    sizeof(PushData), &pushConstants);

        commandBuffer.drawIndexedIndirect(indirectBuffers[frameIndex].buffer, 0,
                                          drawCount,
                                          sizeof(VkDrawIndexedIndirectCommand));

        commandBuffer.endRendering();

        my_render::transition_image_layout(
            commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
        commandBuffer.end();
    };

    const auto recreateSwapChain = [&]() {
        surface.waitGoodFramebufferSize();
        device.waitIdle();
        swapchain.destroy();
        swapchain = swapchainBuild.rebuild();
    };

    // ====================== 参数化实例生成函数 ======================
    auto generateRandomInstances = [](int count, float xMin, float xMax, float yMin,
                                      float yMax) {
        std::vector<InstanceData> instances;
        instances.reserve(count);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(-0.15f * scaling_factor,
                                                  0.15f * scaling_factor);
        int gridSize = static_cast<int>(std::ceil(std::sqrt(count)));
        const float cellWidth = (xMax - xMin) / gridSize;
        const float cellHeight = (yMax - yMin) / gridSize;
        uint32_t generated = 0;
        for (int i = 0; i < gridSize && generated < count; ++i)
        {
            for (int j = 0; j < gridSize && generated < count; ++j)
            {
                float xCenter = xMin + (i + 0.5f) * cellWidth;
                float yCenter = yMin + (j + 0.5f) * cellHeight;
                float offsetX = dis(gen) * cellWidth * 0.4f;
                float offsetY = dis(gen) * cellHeight * 0.4f;
                float r = static_cast<float>(i) / gridSize;
                float g = static_cast<float>(j) / gridSize;
                float b = 0.5f + 0.5f * (r + g) * 0.5f;
                instances.push_back(
                    {.offset = glm::vec2{xCenter + offsetX, yCenter + offsetY},
                     .color = glm::vec3{r, g, b}});
                ++generated;
            }
        }
        return instances;
    };

    // ====================== 显式定义所有绘制命令（命令 = 指令 + 数据） ======================
    constexpr float INSTANCE_UPDATE_INTERVAL = 0.5f;
    std::vector<VkDrawIndexedIndirectCommand> g_cmds(2);
    std::vector<InstanceData> g_allInstances;

    auto draw_api = [&](std::string_view mesh_name,
                        const std::vector<InstanceData> &instances) {
        auto it = meshMap.find(mesh_name);
        if (it == meshMap.end())
            throw std::runtime_error(std::format("Mesh not found: {}", mesh_name));
        const Mesh &mesh = it->second;
        auto firstInstance = static_cast<uint32_t>(g_allInstances.size());
        g_allInstances.append_range(instances);
        g_cmds.emplace_back(mesh.getDrawCommand(instances.size(), firstInstance));
    };
    auto updateDrawCommands = [&](uint32_t frameIndex) {
        // NOTE: 每一帧都重新记录，可以缓存？
        g_cmds.clear();
        g_allInstances.clear();
        g_allInstances.reserve(MAX_DRAW_CALLS * INSTANCE_COUNT);

        draw_api("quad",
                 generateRandomInstances(INSTANCE_COUNT, -1.0f, 0.0f, -1.0f, 1.0f));
        draw_api("triangle",
                 generateRandomInstances(INSTANCE_COUNT, 0.0f, 1.0f, -1.0f, 1.0f));

        // 上传实例数据到全局 buffer
        void *mapped = globalInstanceBuffer.alloc.mappedPtr;
        memcpy(mapped, g_allInstances.data(),
               g_allInstances.size() * sizeof(InstanceData));

        // 上传间接命令到当前帧的 buffer
        auto &idb = indirectBuffers[frameIndex];
        memcpy(idb.alloc.mappedPtr, g_cmds.data(),
               g_cmds.size() * sizeof(VkDrawIndexedIndirectCommand));
    };

    // 初始化所有帧的间接命令缓冲区，避免首帧闪烁
    updateDrawCommands(0);
    for (uint32_t i = 1; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto &idb = indirectBuffers[i];
        memcpy(idb.alloc.mappedPtr, g_cmds.data(),
               g_cmds.size() * sizeof(VkDrawIndexedIndirectCommand));
    }
    auto lastUpdateTime = std::chrono::high_resolution_clock::now();

    // ====================== 每帧绘制 ======================
    const auto drawFrame = [&]() {
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

        // 按时间间隔更新实例数据和命令
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastUpdateTime).count();
        if (elapsed >= INSTANCE_UPDATE_INTERVAL)
        {
            updateDrawCommands(currentFrame);
            lastUpdateTime = now;
        }

        const auto &commandBuffer = commandBuffers[currentFrame];
        commandBuffer.reset({});
        recordCommandBuffer(commandBuffer, imageIndex, currentFrame,
                            static_cast<uint32_t>(g_cmds.size()));

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

    // ====================== 主循环 ======================
    while (window.shouldClose() == 0)
    {
        surface::pollEvents();
        drawFrame();

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
    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}