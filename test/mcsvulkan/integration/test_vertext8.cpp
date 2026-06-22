#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>
#include <utility>
#include <vector>
#include <random>
#include <list>
#include <cstring>

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

// 原有内存管理工具（本程序已用子分配器替代，保留 using 以保持兼容）
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
    SubAllocator(LogicalDevice &device, uint32_t memoryTypeIndex, VkDeviceSize blockSize,
                 pNext pnext, bool mapAll = true)
        : device_(device), typeIndex_(memoryTypeIndex), blockSize_(blockSize),
          pNext_(std::move(pnext)), mapAll_(mapAll)
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
            if (block->tryAlloc(reqSize, alignment, offset))
            {
                return {.memory = block->memory,
                        .offset = offset,
                        .size = reqSize,
                        .mappedPtr = block->mapped
                                         ? static_cast<char *>(block->mapBase) + offset
                                         : nullptr};
            }
        }
        VkDeviceSize allocSize = std::max(blockSize_, reqSize);
        std::println("[SubAlloc] Need new block! Request={} bytes, Allocating={} bytes",
                     reqSize, allocSize);
        auto newBlock = std::make_unique<Block>(device_, typeIndex_, allocSize,
                                                pNext_.value(), mapAll_);
        VkDeviceSize offset = 0;
        if (!newBlock->tryAlloc(reqSize, alignment, offset))
            throw std::runtime_error("SubAllocator: internal allocation failure");

        SubAllocation result{.memory = newBlock->memory,
                             .offset = offset,
                             .size = reqSize,
                             .mappedPtr =
                                 newBlock->mapped
                                     ? static_cast<char *>(newBlock->mapBase) + offset
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
            if (block->memory == alloc.memory)
            {
                block->dealloc(alloc.offset, alloc.size);
                return;
            }
        }
    }

  private:
    struct Block
    {
        LogicalDevice &device;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        void *mapBase = nullptr;
        bool mapped = false;
        std::vector<std::pair<VkDeviceSize, VkDeviceSize>> freeList;

        Block(LogicalDevice &dev, uint32_t typeIndex, VkDeviceSize sz, const void *pNext,
              bool doMap)
            : device(dev), size(sz), mapped(doMap)
        {
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.pNext = pNext;
            allocInfo.allocationSize = sz;
            allocInfo.memoryTypeIndex = typeIndex;

            memory = device.allocateMemory(allocInfo, device.allocator());
            if (mapped)
                device.mapMemory(memory, 0, VK_WHOLE_SIZE, 0, &mapBase);
            freeList.emplace_back(0, sz);
            std::println("[SubAlloc] Created new Block: size={} ({} MB), memory={}", sz,
                         sz / (1024 * 1024), (void *)memory);
        }

        ~Block()
        {
            if (mapped)
                device.unmapMemory(memory);
            if (memory)
            {
                std::println("[SubAlloc] Destroying Block: size={} ({} MB), memory={}",
                             size, size / (1024 * 1024), (void *)memory);
                device.freeMemory(memory, device.allocator());
            }
        }

        Block(const Block &) = delete;
        Block &operator=(const Block &) = delete;
        Block(Block &&) = delete;
        Block &operator=(Block &&) = delete;

        bool tryAlloc(VkDeviceSize reqSize, VkDeviceSize alignment,
                      VkDeviceSize &outOffset)
        {
            for (auto it = freeList.begin(); it != freeList.end(); ++it)
            {
                VkDeviceSize first = it->first;
                VkDeviceSize rangeEnd = first + it->second;
                VkDeviceSize aligned = (first + alignment - 1) & ~(alignment - 1);
                if (aligned + reqSize > rangeEnd)
                    continue;

                VkDeviceSize before = aligned - first;
                VkDeviceSize after = rangeEnd - (aligned + reqSize);

                if (after > 0)
                {
                    it->first = aligned + reqSize;
                    it->second = after;
                }
                else
                {
                    it = freeList.erase(it);
                }
                if (before > 0)
                {
                    freeList.insert(it, {first, before});
                }
                outOffset = aligned;
                return true;
            }
            return false;
        }

        void dealloc(VkDeviceSize off, VkDeviceSize sz)
        {
            VkDeviceSize origOff = off; // 保存原始值
            VkDeviceSize origSz = sz;

            bool merged = false;
            auto it = std::lower_bound(
                freeList.begin(), freeList.end(), off,
                [](const auto &a, VkDeviceSize b) { return a.first < b; });

            if (it != freeList.begin())
            {
                auto prev = std::prev(it);
                if (prev->first + prev->second == off)
                {
                    off = prev->first;
                    sz += prev->second;
                    freeList.erase(prev);
                    it = std::lower_bound(
                        freeList.begin(), freeList.end(), off,
                        [](const auto &a, VkDeviceSize b) { return a.first < b; });
                    merged = true;
                }
            }
            if (it != freeList.end() && off + sz == it->first)
            {
                sz += it->second;
                freeList.erase(it);
                it = std::lower_bound(
                    freeList.begin(), freeList.end(), off,
                    [](const auto &a, VkDeviceSize b) { return a.first < b; });
                merged = true;
            }
            freeList.insert(it, {off, sz});

            if (merged)
            {
                std::println("[SubAlloc] Free merged: released offset={}, size={} -> new "
                             "free range offset={}, size={}",
                             origOff, origSz, off, sz);
            }
        }
    };

    LogicalDevice &device_;
    uint32_t typeIndex_;
    VkDeviceSize blockSize_;
    pNext pNext_;
    bool mapAll_;
    std::vector<std::unique_ptr<Block>> blocks_;
};

// ---------- 自管理缓冲区（所有权完全转移） ----------
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
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME};
    requiredDeviceExtension.emplace_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan12Features,
                    VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        enablefeatureChain = {
            {.features = {.multiDrawIndirect = VK_TRUE, .shaderInt64 = VK_TRUE}},
            {.scalarBlockLayout = VK_TRUE, .bufferDeviceAddress = VK_TRUE},
            {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
            {.extendedDynamicState = VK_TRUE}};

    auto [id [[maybe_unused]], physical_device [[maybe_unused]]] =
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

    // ================= 顶点/实例数据 =================
    const std::vector<Vertex> vertices = {
        {{-0.01f, -0.01f}}, {{0.01f, -0.01f}}, {{0.01f, 0.01f}}, {{-0.01f, 0.01f}}};
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    const uint32_t INSTANCE_COUNT = 1000;
    uint32_t dynamicInstanceCount = INSTANCE_COUNT;
    std::vector<InstanceData> instancesDta;
    instancesDta.reserve(INSTANCE_COUNT);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(-0.4f, 0.4f);

    auto generateData = [&]() {
        instancesDta.clear();
        instancesDta.reserve(dynamicInstanceCount);
        int gridSize = static_cast<int>(std::ceil(std::sqrt(dynamicInstanceCount)));
        const float cellWidth = 2.0f / gridSize;
        const float cellHeight = 2.0f / gridSize;
        uint32_t generated = 0;
        for (int i = 0; i < gridSize && generated < dynamicInstanceCount; ++i)
            for (int j = 0; j < gridSize && generated < dynamicInstanceCount; ++j)
            {
                float xCenter = -1.0f + (i + 0.5f) * cellWidth;
                float yCenter = -1.0f + (j + 0.5f) * cellHeight;
                float offsetX = dis(gen) * cellWidth * 0.5f;
                float offsetY = dis(gen) * cellHeight * 0.5f;
                float r = static_cast<float>(i) / gridSize;
                float g = static_cast<float>(j) / gridSize;
                float b = 0.5f + 0.5f * (r + g) * 0.5f;
                instancesDta.push_back(
                    {.offset = glm::vec2{xCenter + offsetX, yCenter + offsetY},
                     .color = glm::vec3{r, g, b}});
                ++generated;
            }
    };
    generateData();

    std::vector<VkDrawIndexedIndirectCommand> indirectCmds = {
        {.indexCount = static_cast<uint32_t>(indices.size()),
         .instanceCount = static_cast<uint32_t>(instancesDta.size()),
         .firstIndex = 0,
         .vertexOffset = 0,
         .firstInstance = 0}};

    const std::vector<Vertex> quadVertices = {
        {{-0.01f, -0.01f}}, {{0.01f, -0.01f}}, {{0.01f, 0.01f}}, {{-0.01f, 0.01f}}};
    const std::vector<uint32_t> quadIndices = {0, 1, 2, 0, 2, 3};
    const std::vector<Vertex> triVertices = {
        {{-0.02f, -0.02f}}, {{0.02f, -0.02f}}, {{0.00f, 0.02f}}};
    const std::vector<uint32_t> triIndices = {0, 1, 2};
    bool isQuad = true;

    // ================= 子分配器初始化 =================
    uint32_t memoryTypeIndex = 0;
    {
        VkBufferCreateInfo tmpInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                   .size = 256,
                                   .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VkBuffer tmpBuf = device.createBuffer(tmpInfo, device.allocator());
        VkMemoryRequirements memReqs = device.getBufferMemoryRequirements(tmpBuf);
        device.destroyBuffer(tmpBuf, device.allocator());

        VkPhysicalDeviceMemoryProperties memProps = physical_device.getMemoryProperties();
        memoryTypeIndex = find_memory_type_index(
            memReqs.memoryTypeBits, memProps,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    // 块大小设置为 1MB，大于数据峰值（~400KB），以便在同一块内观察合并
    SubAllocator subAlloc(device, memoryTypeIndex, 1 * 1024 * 1024,
                          make_pNext(structure_chain<VkMemoryAllocateFlagsInfo>{
                              {.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT}}),
                          true);

    auto createSubBuffer = [&](VkDeviceSize size,
                               VkBufferUsageFlags usage) -> SubAllocatedBuffer {
        VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                      .size = size,
                                      .usage = usage,
                                      .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VkBuffer buf = device.createBuffer(bufferInfo, device.allocator());
        VkMemoryRequirements memReqs = device.getBufferMemoryRequirements(buf);
        SubAllocation alloc = subAlloc.allocate(memReqs);
        device.bindBufferMemory(buf, alloc.memory, alloc.offset);
        return SubAllocatedBuffer(buf, alloc, &device, &subAlloc);
    };

    std::array<SubAllocatedBuffer, MAX_FRAMES_IN_FLIGHT> vertexBuffers;
    std::array<SubAllocatedBuffer, MAX_FRAMES_IN_FLIGHT> indexBuffers;
    std::array<SubAllocatedBuffer, MAX_FRAMES_IN_FLIGHT> indirectBuffers;
    std::array<SubAllocatedBuffer, MAX_FRAMES_IN_FLIGHT> instanceBuffers;
    std::array<VkDeviceAddress, MAX_FRAMES_IN_FLIGHT> instanceBufferAddress{};
    std::array<bool, MAX_FRAMES_IN_FLIGHT> frameNeedsGeometryUpdate{true, true};

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vertexBuffers[i] = createSubBuffer(vertices.size() * sizeof(Vertex),
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        indexBuffers[i] = createSubBuffer(indices.size() * sizeof(uint32_t),
                                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        indirectBuffers[i] = createSubBuffer(sizeof(VkDrawIndexedIndirectCommand),
                                             VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        instanceBuffers[i] =
            createSubBuffer(instancesDta.size() * sizeof(InstanceData),
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        instanceBufferAddress[i] =
            device.getBufferDeviceAddress({.sType = sType<VkBufferDeviceAddressInfo>(),
                                           .buffer = instanceBuffers[i].buffer});
    }

    // ================= 记录命令缓冲区 =================
    const auto recordCommandBuffer = [&](const CommandBufferView &commandBuffer,
                                         uint32_t imageIndex, uint32_t frameIndex) {
        VkImage image = swapchain.image(imageIndex);
        VkImageView imageView = swapchain.imageView(imageIndex);
        auto imageExtent = swapchain.imageExtent();

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        my_render::transition_image_layout(
            commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {},
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        auto &vb = vertexBuffers[frameIndex];
        auto &ib = indexBuffers[frameIndex];
        auto &idb = indirectBuffers[frameIndex];
        auto &instb = instanceBuffers[frameIndex];
        auto instanceBufferAddres = instanceBufferAddress[frameIndex];

        if (frameNeedsGeometryUpdate[frameIndex])
        {
            const auto &newVerts = isQuad ? quadVertices : triVertices;
            const auto &newIdxs = isQuad ? quadIndices : triIndices;

            if (vb.alloc.size < newVerts.size() * sizeof(Vertex))
                vb = createSubBuffer(newVerts.size() * sizeof(Vertex),
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            if (ib.alloc.size < newIdxs.size() * sizeof(uint32_t))
                ib = createSubBuffer(newIdxs.size() * sizeof(uint32_t),
                                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            if (instb.alloc.size < instancesDta.size() * sizeof(InstanceData))
            {
                instb = createSubBuffer(instancesDta.size() * sizeof(InstanceData),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
                instanceBufferAddress[frameIndex] = device.getBufferDeviceAddress(
                    {.sType = sType<VkBufferDeviceAddressInfo>(),
                     .buffer = instb.buffer});
            }

            memcpy(vb.alloc.mappedPtr, newVerts.data(), newVerts.size() * sizeof(Vertex));
            memcpy(ib.alloc.mappedPtr, newIdxs.data(), newIdxs.size() * sizeof(uint32_t));
            memcpy(idb.alloc.mappedPtr, indirectCmds.data(),
                   sizeof(VkDrawIndexedIndirectCommand));
            memcpy(instb.alloc.mappedPtr, instancesDta.data(),
                   instancesDta.size() * sizeof(InstanceData));

            frameNeedsGeometryUpdate[frameIndex] = false;

            std::array<VkBufferMemoryBarrier2, 4> barriers{{
                {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                 VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
                 VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_QUEUE_FAMILY_IGNORED,
                 VK_QUEUE_FAMILY_IGNORED, vb.buffer, 0, vb.alloc.size},
                {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                 VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT,
                 VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, ib.buffer, 0,
                 ib.alloc.size},
                {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                 VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                 VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_QUEUE_FAMILY_IGNORED,
                 VK_QUEUE_FAMILY_IGNORED, idb.buffer, 0, idb.alloc.size},
                {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, nullptr,
                 VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                 VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, instb.buffer, 0,
                 instb.alloc.size},
            }};
            commandBuffer.pipelineBarrier2(
                {.sType = sType<VkDependencyInfo>(),
                 .bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                 .pBufferMemoryBarriers = barriers.data()});
        }

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

        commandBuffer.bindVertexBuffers(0, 1, vb.buffer, 0);
        commandBuffer.bindIndexBuffer(ib.buffer, 0, VK_INDEX_TYPE_UINT32);

        PushData pushConstants = {instanceBufferAddres};
        commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                    sizeof(PushData), &pushConstants);

        commandBuffer.drawIndexedIndirect(idb.buffer, 0, 1,
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

        const auto &commandBuffer = commandBuffers[currentFrame];
        commandBuffer.reset({});
        recordCommandBuffer(commandBuffer, imageIndex, currentFrame);

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

    // ================= 锯齿波实例数序列 =================
    static int instanceTarget = 1000; // 当前目标实例数
    static int instanceStep = 500;    // 每步变化量
    static int direction = 1;         // 1: 增加, -1: 减少
    static int changeCounter = 0;     // 计数器，每秒变化一次

    while (window.shouldClose() == 0)
    {
        surface::pollEvents();
        drawFrame();
        {
            static auto lastTime = std::chrono::high_resolution_clock::now();
            static int frameCount = 0, totalFrames = 0;
            static float accumulatedFPS = 0.0f;
            static int avgSamples = 0;
            frameCount++;
            totalFrames++;
            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(now - lastTime).count();
            if (elapsed >= 1.0f)
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

                // 锯齿波序列：500 -> 1000 -> 1500 -> ... -> 19000 -> 19500 -> 19000 -> ... -> 1000 -> 500 ...
                instanceTarget += instanceStep * direction;
                if (instanceTarget >= 20000)
                {
                    instanceTarget = 20000;
                    direction = -1;
                }
                else if (instanceTarget <= 500)
                {
                    instanceTarget = 500;
                    direction = 1;
                }
                dynamicInstanceCount = static_cast<uint32_t>(instanceTarget);

                // 每隔一次变化切换几何形状，增加多样性
                changeCounter = (changeCounter + 1) % 2;
                if (changeCounter == 0)
                    isQuad = !isQuad;

                // 重新生成实例数据
                instancesDta.clear();
                generateData();

                // 更新间接命令
                const auto &newIdxs = isQuad ? quadIndices : triIndices;
                indirectCmds[0].indexCount = static_cast<uint32_t>(newIdxs.size());
                indirectCmds[0].instanceCount =
                    static_cast<uint32_t>(instancesDta.size());

                // 触发所有飞行帧更新数据
                for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
                    frameNeedsGeometryUpdate[i] = true;
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