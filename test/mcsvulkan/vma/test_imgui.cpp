
#include <array>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <exception>
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

#include "../head.hpp"

// diff: [test_imgui] start Dear ImGui
// #define IMGUI_IMPL_VULKAN_USE_VOLK
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
// diff: [test_imgui] end

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

using buffer_base = mcs::vulkan::vma::buffer_base;
using mcs::vulkan::vma::create_buffer;
using mcs::vulkan::vma::staging_buffer;

using mcs::vulkan::vma::create_resources;
using mcs::vulkan::vma::create_image;

using mcs::vulkan::tool::simple_copy_buffer;

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

struct alignas(16) PushConstants
{
    uint64_t vertexBufferAddress; // 使用uint64_t而不是VkDeviceAddress

    // diff: [texture] start
    uint32_t textureIndex; // 添加纹理索引
    uint32_t samplerIndex; // 添加采样器索引
    VkDeviceAddress objectDataAddress;
    // diff: [texture] end
};
static_assert(sizeof(PushConstants) == 32);
struct Vertex // NOLINT
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord; // diff: [texture] 添加纹理坐标
};
static_assert(alignof(Vertex) == 4, "check alignof error");
static_assert(sizeof(Vertex) == 32, "check sizeof error");

struct mesh_base
{
    using index_type = uint32_t;
    // 双缓冲顶点数据：每个飞行帧都有自己的缓冲区
    struct FrameBuffers
    {
        buffer_base vertexBuffer;
        buffer_base indexBuffer;
        VkDeviceAddress vertexBufferAddress = 0;
        uint32_t indexCount = 0; // 新增 避免依赖脏数据大小

        // diff: [test_model_matrix2] start
        buffer_base objectDataBuffer;
        VkDeviceAddress objectDataAddress = 0;
        VmaAllocationInfo allocInfo;
        // diff: [test_model_matrix2] end

        // diff: [camera_model] start
        //  新增：物体左上角在局部坐标系中的坐标（相对于物体中心）
        glm::vec3 topLeftLocal = glm::vec3(0.0f);
        // diff: [camera_model] end
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
            createObjectBuffer(allocator, fb);
            getBufferDeviceAddresses(fb);
            bounding_box(fb);
        }
    }

    // buffer 内存要求
    static constexpr auto REQUIRE_BUFFER_USAGE =
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    // diff: [test_model_matrix2] start
    struct object_data
    {
        glm::mat4 matrix;
    };
    constexpr void createObjectBuffer(VmaAllocator allocator, FrameBuffers &fb)
    {
        constexpr auto BUFFER_SIZE = sizeof(object_data);
        fb.objectDataBuffer = create_buffer(
            allocator,
            {.sType = sType<VkBufferCreateInfo>(),
             .size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | REQUIRE_BUFFER_USAGE,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
            },
            &fb.allocInfo);
    }
    // diff: [test_model_matrix3] 小小调整
    static void updateObjectData(FrameBuffers &fb, const auto &get_model_matrix) noexcept
    {
        auto &allocInfo = fb.allocInfo;
        auto data = get_model_matrix();
        // Buffer is already mapped. You can access its memory.
        ::memcpy(allocInfo.pMappedData, &data, sizeof(data));
    }
    // diff: [test_model_matrix2] end

    constexpr void createVertexBufferForFrame(VmaAllocator allocator, FrameBuffers &fb)
    {
        auto &vertices = queue_data.vertices;
        const VkDeviceSize BUFFER_SIZE = sizeof(vertices[0]) * vertices.size();
        auto &destBuffer = fb.vertexBuffer;
        constexpr auto USAGE = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        // 直接使用重构后的 create_buffer 函数
        destBuffer = create_buffer(
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
        const auto STAGING_BUFFER = staging_buffer(allocator, BUFFER_SIZE);
        STAGING_BUFFER.copyDataToBuffer(vertices.data(), BUFFER_SIZE);

        simple_copy_buffer(*commandpool, *queue, STAGING_BUFFER.buffer(),
                           destBuffer.buffer(), VkBufferCopy{.size = BUFFER_SIZE});
    }

    void createIndexBufferForFrame(VmaAllocator allocator, FrameBuffers &fb)
    {
        auto &indices = queue_data.indices;
        const VkDeviceSize BUFFER_SIZE = sizeof(indices[0]) * indices.size();
        auto &destBuffer = fb.indexBuffer;
        constexpr auto USAGE = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        fb.indexCount = static_cast<uint32_t>(indices.size());

        // 直接使用重构后的 create_buffer 函数
        destBuffer = create_buffer(
            allocator,
            {.sType = sType<VkBufferCreateInfo>(),
             .size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | USAGE | REQUIRE_BUFFER_USAGE,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            {.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
             .usage = VMA_MEMORY_USAGE_AUTO});

        // 创建暂存缓冲区并复制数据
        const auto STAGING_BUFFER = staging_buffer(allocator, BUFFER_SIZE);
        STAGING_BUFFER.copyDataToBuffer(indices.data(), BUFFER_SIZE);

        simple_copy_buffer(*commandpool, *queue, STAGING_BUFFER.buffer(),
                           destBuffer.buffer(), {VkBufferCopy{.size = BUFFER_SIZE}});
    }

    void getBufferDeviceAddresses(FrameBuffers &fb) const
    {
        const auto *device = commandpool->device();
        if (fb.vertexBuffer.buffer() != VK_NULL_HANDLE)
        {
            fb.vertexBufferAddress = device->getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(),
                 .buffer = fb.vertexBuffer.buffer()});

            // diff: [test_model_matrix2] start
            fb.objectDataAddress = device->getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(),
                 .buffer = fb.objectDataBuffer.buffer()});
            // diff: [test_model_matrix2] end
        }
    }

    // diff: [camera_model] start
    void bounding_box(FrameBuffers &fb)
    { // 计算包围盒和左上角坐标
        if (!queue_data.vertices.empty())
        {
            glm::vec3 minPos = queue_data.vertices[0].pos;
            glm::vec3 maxPos = queue_data.vertices[0].pos;
            for (const auto &v : queue_data.vertices)
            {
                minPos = glm::min(minPos, v.pos);
                maxPos = glm::max(maxPos, v.pos);
            }
            // 左上角 = (minX, maxY, maxZ)
            fb.topLeftLocal = glm::vec3(minPos.x, maxPos.y, maxPos.z);
        }
    }
    auto topLeftLocal(uint32_t currentFrame)
    {
        return frameBuffers[currentFrame].topLeftLocal;
    }
    // diff: [camera_model] end

    [[nodiscard]] VkDeviceAddress getVertexBufferAddress(
        uint32_t currentFrame) const noexcept
    {
        return frameBuffers[currentFrame].vertexBufferAddress;
    }

    [[nodiscard]] VkDeviceAddress getObjectDataAddress(
        uint32_t currentFrame) const noexcept
    {
        return frameBuffers[currentFrame].objectDataAddress;
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
        bounding_box(fb);
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

class TextureImage
{
  private:
    mcs::vulkan::vma::vma_image textureImage_;
    mcs::vulkan::ImageView imageView_;

  public:
    TextureImage() = default;

    [[nodiscard]] auto &imageView() const noexcept
    {
        return imageView_;
    }
    [[nodiscard]] auto &image() const noexcept
    {
        return textureImage_;
    }

    static CommandBuffer beginSingleTimeCommand(const CommandPool &commandpool)
    {
        CommandBuffer commandCopyBuffer = commandpool.allocateOneCommandBuffer(
            {.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY});

        commandCopyBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>(),
                                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
        return commandCopyBuffer;
    }
    static void endSingleTimeCommand(const CommandPool &commandpool, const Queue &queue,
                                     CommandBuffer &commandBuffer)
    {
        commandBuffer.end();

        const auto *logicalDevice = commandpool.device();

        Fence fence{*logicalDevice, {.sType = sType<VkFenceCreateInfo>(), .flags = 0}};
        queue.submit(1,
                     {.sType = sType<VkSubmitInfo>(),
                      .commandBufferCount = 1,
                      .pCommandBuffers = &*commandBuffer},
                     *fence);
        mcs::vulkan::check_vkresult(
            logicalDevice->waitForFences(1, *fence, VK_TRUE, UINT64_MAX));
    }

    static void transitionImageLayout(const CommandPool &pool, const Queue &queue,
                                      const VkImage &image, VkImageLayout oldLayout,
                                      VkImageLayout newLayout, uint32_t mipLevels)
    {
        // 我们需要执行几个转换：
        // 1.从初始未定义的布局到针对接收数据优化的布局（传输目的地）
        // 2.从传输目的地到针对着色器读取优化的布局，因此我们的片段着色器可以从中采样
        auto commandBuffer = beginSingleTimeCommand(pool);

        VkImageMemoryBarrier barrier{
            .sType = sType<VkImageMemoryBarrier>(),
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .image = image,
            .subresourceRange = VkImageSubresourceRange{
                .aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = mipLevels,
                .baseArrayLayer = 0,
                .layerCount = 1}};

        // NOTE: 7. 过度屏障掩码？
        VkPipelineStageFlags sourceStage;      // NOLINT
        VkPipelineStageFlags destinationStage; // NOLINT

        // 未定义→传输目的地：不需要等待任何东西的传输写入
        if (oldLayout == VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            // 写入不必等待任何东西，您可以为预屏障操作指定一个空的访问掩码和最早可能的管道阶段
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT;
        } // 传输目的地→着色器读取：着色器读取应该等待传输写入，特别是着色器在片段着色器中读取，因为那是我们要使用纹理的地方
        else if (oldLayout == VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage =
                VkPipelineStageFlagBits::VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }
        // NOTE: 4. 执行布局转换的最常见方法之一是使用映像内存屏障。设置不同掩码来优化速度
        commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, {}, {},
                                      std::array<VkImageMemoryBarrier, 1>{barrier});
        endSingleTimeCommand(pool, queue, commandBuffer);
    }

    static void copyBufferToImage(const CommandPool &commandpool, const Queue &queue,
                                  const VkBuffer &buffer, const VkImage &image,
                                  VkExtent2D extent)
    {
        auto commandBuffer = beginSingleTimeCommand(commandpool);
        VkBufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
                                 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {extent.width, extent.height, 1}};
        commandBuffer.copyBufferToImage(
            buffer, image, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            std::array<VkBufferImageCopy, 1>{region});
        endSingleTimeCommand(commandpool, queue, commandBuffer);
    }

    template <typename T>
    constexpr static auto getMipLevels(T w, T h) noexcept
    {
        return static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
    }

    struct mipmap_param
    {
        int32_t width, height;
        uint32_t mip_levels;
    };

    struct image_info
    {
        std::span<const uint8_t> pixels;
        int width{};
        int height{};
        uint32_t mip_levels = 1;
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    };

    constexpr TextureImage(VmaAllocator allocator, const LogicalDevice &device,
                           const CommandPool &pool, const Queue &queue,
                           const image_info &info,
                           std::optional<create_image> build = std::nullopt)
    {
        int width_ = info.width;
        int height_ = info.height;
        auto mipLevels = info.mip_levels;

        // 创建暂存缓冲区
        auto stagingBuffer = staging_buffer(allocator, info.pixels.size());
        stagingBuffer.copyDataToBuffer(info.pixels.data(), info.pixels.size());

        if (not build)
            build = create_image{device, allocator}
                        .setCreateInfo(
                            {.imageType = VK_IMAGE_TYPE_2D,
                             .format = info.format,
                             .extent = {.width = static_cast<uint32_t>(width_),
                                        .height = static_cast<uint32_t>(height_),
                                        .depth = 1},
                             .mipLevels = mipLevels,
                             .arrayLayers = 1,
                             .samples = VK_SAMPLE_COUNT_1_BIT,
                             .tiling = VK_IMAGE_TILING_OPTIMAL,
                             .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                      VK_IMAGE_USAGE_SAMPLED_BIT,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                             // VMA 配置
                             .allocationCreateInfo =
                                 {.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                  .usage = VMA_MEMORY_USAGE_AUTO}})
                        .setViewCreateInfo(
                            {.viewType = VK_IMAGE_VIEW_TYPE_2D,
                             .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                  .baseMipLevel = 0,
                                                  .levelCount = mipLevels,
                                                  .baseArrayLayer = 0,
                                                  .layerCount = 1}});

        textureImage_ = (*build).makeImage();
        transitionImageLayout(
            pool, queue, *textureImage_, VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED,
            VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
        copyBufferToImage(pool, queue, *stagingBuffer, *textureImage_,
                          VkExtent2D{.width = static_cast<uint32_t>(width_),
                                     .height = static_cast<uint32_t>(height_)});

        if (mipLevels == 1)
            transitionImageLayout(pool, queue, *textureImage_,
                                  VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  mipLevels);
        else
            generateMipmaps(pool, queue, *textureImage_, (*build).refImageFormat(),
                            mipmap_param{.width = width_,
                                         .height = height_,
                                         .mip_levels = mipLevels});

        imageView_ = (*build).makeImageView(*textureImage_);
    }

    static TextureImage fromFile(VmaAllocator allocator, const LogicalDevice &device,
                                 const CommandPool &pool, const Queue &queue,
                                 const std::string &path, bool mipmap = true,
                                 std::optional<create_image> build = std::nullopt)
    {
        auto img = mcs::vulkan::load::raw_stbi_image(path.c_str(), STBI_rgb_alpha);
        return TextureImage(
            allocator, device, pool, queue,
            image_info{.pixels = {img.data(), img.size()},
                       .width = img.width(),
                       .height = img.height(),

                       .mip_levels =
                           mipmap ? TextureImage::getMipLevels(img.width(), img.height())
                                  : 1,
                       .format = VK_FORMAT_R8G8B8A8_SRGB},
            std::move(build));
    }

    // 告诉采样器如何采样：通过绑定或提交命令
    static void generateMipmaps(const CommandPool &commandpool, const Queue &queue,
                                VkImage &image, VkFormat imageFormat, mipmap_param param)
    {
        auto [texWidth, texHeight, mipLevels] = param;

        const auto *logicalDevice = commandpool.device();

        /*
        这样的内置函数生成所有mip级别非常方便，
        但遗憾的是不能保证所有平台都支持，它需要我们使用的纹理图像格式来支持线性过滤
        */
        // NOTE: 4. 要求线性滤波
        //  Check if image format supports linear blit-ing
        if (VkFormatProperties formatProperties =
                logicalDevice->physicalDevice()->getFormatProperties(imageFormat);
            (formatProperties.optimalTilingFeatures &
             VkFormatFeatureFlagBits::
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) == 0)
        {
            throw std::runtime_error(
                "texture image format does not support linear blitting!");
        }

        auto commandBuffer = beginSingleTimeCommand(commandpool);

        VkImageMemoryBarrier barrier = {
            .sType = sType<VkImageMemoryBarrier>(),
            .srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {.aspectMask =
                                     VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};

        int32_t mipWidth = texWidth;
        int32_t mipHeight = texHeight;

        // 我们将进行几个转换，所以我们将重用这个VkImageMemoryBarrier。上面设置的字段对于所有屏障将保持不变
        for (uint32_t i = 1; i < mipLevels; i++)
        {
            // 将第i-1级从TRANSFER_DST_OPTIMAL转换为TRANSFER_SRC_OPTIMAL
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_READ_BIT;

            commandBuffer.pipelineBarrier(
                VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT, {}, {}, {},
                std::span{&barrier, 1});

            // 设置blit操作的参数
            // NOTE: 该命令执行复制、缩放和过滤操作。
            // 我们的纹理图像现在有多个mip级别，但是暂存缓冲区只能用于填充mip级别0
            // 如果您使用专用传输队列（如顶点缓冲区中建议的），请注意：vkCmdBlitImage必须提交到具有图形功能的队列
            commandBuffer.blitImage(
                image, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                std::array<VkImageBlit, 1>{VkImageBlit{
                    .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                       .mipLevel = i - 1,
                                       .baseArrayLayer = 0,
                                       .layerCount = 1},
                    .srcOffsets = {{.x = 0, .y = 0, .z = 0},
                                   {.x = mipWidth, .y = mipHeight, .z = 1}},
                    .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                       .mipLevel = i,
                                       .baseArrayLayer = 0,
                                       .layerCount = 1},
                    .dstOffsets = {{.x = 0, .y = 0, .z = 0},
                                   {.x = mipWidth > 1 ? mipWidth / 2 : 1,
                                    .y = mipHeight > 1 ? mipHeight / 2 : 1,
                                    .z = 1}}}},
                VK_FILTER_LINEAR);

            // 将第i-1级从TRANSFER_SRC_OPTIMAL转换为SHADER_READ_ONLY_OPTIMAL
            barrier.oldLayout = VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_SHADER_READ_BIT;

            commandBuffer.pipelineBarrier(
                VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
                VkPipelineStageFlagBits::VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, {}, {},
                std::span{&barrier, 1});

            // 我们将当前mip维度除以2。我们在除法之前检查每个维度，以确保维度永远不会变成0
            if (mipWidth > 1)
                mipWidth /= 2;
            if (mipHeight > 1)
                mipHeight /= 2;
        }

        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_SHADER_READ_BIT;

        commandBuffer.pipelineBarrier(
            VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
            VkPipelineStageFlagBits::VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, {}, {},
            std::span{&barrier, 1});

        endSingleTimeCommand(commandpool, queue, commandBuffer);
        // NOTE: 应该注意的是，在运行时生成mipmap级别在实践中并不常见。
        //  通常它们是预先生成的，并与基本级别一起存储在纹理文件中，以提高加载速度

        // NOTE:总之，每个mip级别都要像加载原始图像一样加载到图像中。
    }
};
TextureImage generateGradientTexture(VmaAllocator allocator, const LogicalDevice &device,
                                     const CommandPool &pool, const Queue &queue,
                                     int textureType = 0, bool mipmap = true,
                                     std::optional<create_image> build = std::nullopt)
{
    // 生成程序纹理
    auto texWidth_ = 256;
    auto texHeight_ = 256;
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
    return TextureImage(
        allocator, device, pool, queue,
        TextureImage::image_info{
            .pixels = {pixels.get(), static_cast<uint64_t>(imageSize)},
            .width = texWidth_,
            .height = texHeight_,
            .mip_levels = mipmap ? TextureImage::getMipLevels(texWidth_, texHeight_) : 1,
            .format = VK_FORMAT_R8G8B8A8_SRGB},
        std::move(build));
}

class TextureSampler
{
  private:
    const LogicalDevice *device_ = nullptr;
    VkSampler sampler_ = VK_NULL_HANDLE;

  public:
    TextureSampler() = delete;
    TextureSampler(const TextureSampler &) = delete;
    TextureSampler(TextureSampler &&o) noexcept
        : device_{std::exchange(o.device_, {})}, sampler_{std::exchange(o.sampler_, {})}
    {
    }
    TextureSampler &operator=(const TextureSampler &) = delete;
    TextureSampler &operator=(TextureSampler &&o) noexcept
    {
        if (&o != this)
        {
            destroy();
            device_ = std::exchange(o.device_, {});
            sampler_ = std::exchange(o.sampler_, {});
        }
        return *this;
    }
    ~TextureSampler()
    {
        destroy();
    }

    explicit TextureSampler(const LogicalDevice &device, int samplerType = 0)
        : device_(&device)
    {
        VkSamplerCreateInfo samplerInfo{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        if (samplerType == 0)
        {
            // 采样器类型0：线性过滤，重复寻址，各向异性
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

            // NOTE: 各向异性器件特性启用
            samplerInfo.anisotropyEnable = VK_TRUE;

            VkPhysicalDeviceProperties properties =
                device_->physicalDevice()->getProperties();
            samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        }
        else if (samplerType == 1)
        {
            // 采样器类型1：最近邻过滤，钳位到边缘
            samplerInfo.magFilter = VK_FILTER_NEAREST;
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.maxAnisotropy = 1.0F;
        }

        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0F;
        samplerInfo.minLod = 0.0F;
        samplerInfo.maxLod = 0.0F;

        sampler_ = device_->createSampler(samplerInfo, device_->allocator());
    }

    void destroy() noexcept
    {
        if (sampler_ != VK_NULL_HANDLE)
        {
            device_->destroySampler(sampler_, device_->allocator());
            sampler_ = VK_NULL_HANDLE;
        }
    }

    [[nodiscard]] VkSampler sampler() const noexcept
    {
        return sampler_;
    }
};

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
};

struct PerFrameBatch
{
    buffer_base mergedIndexBuffer{};
    void *mergedIndexMapped = nullptr;
    VkDeviceSize mergedIndexCapacity = 0;

    buffer_base objectInfoBuffer{};
    void *objectInfoMapped = nullptr;
    VkDeviceSize objectInfoCapacity = 0;

    buffer_base indirectDrawBuffer{};
    void *indirectDrawMapped = nullptr;
    VkDeviceSize indirectCmdCapacity = 0;

    uint32_t drawCount = 0;
};
//diff: [test_indirectdraw] end

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
                     .samplerAnisotropy = VK_TRUE,
                     .multiDrawIndirect = VK_TRUE, //diff: [test_indirectdraw]
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
                         {.binding = 0,
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
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             MAX_FRAMES_IN_FLIGHT}, //diff: [test_indirectdraw] end
                     }})
            .build(device);
    auto descriptorSets = descriptorPool.allocateDescriptorSets(
        {.descriptorSets = std::vector<VkDescriptorSetLayout>{MAX_FRAMES_IN_FLIGHT,
                                                              *descriptorSetLayout}});
    constexpr VkDeviceSize BUFFER_SIZE = sizeof(UniformBufferObject);
    std::array<buffer_base, MAX_FRAMES_IN_FLIGHT> uniformBuffers;
    std::array<void *, MAX_FRAMES_IN_FLIGHT> uniformBuffersMapped{};
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        uniformBuffers[i] = create_buffer(
            allocator,
            {.sType = sType<VkBufferCreateInfo>(),
             .size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
             .usage = VMA_MEMORY_USAGE_AUTO});
        uniformBuffersMapped[i] = uniformBuffers[i].map();
    }

    std::map<uint32_t, TextureImage> textureMap; // 槽位 → 纹理
    textureMap.emplace(0, TextureImage::fromFile(allocator, device, commandPool,
                                                 GRAPHICS_AND_PRESENT,
                                                 "textures/texture.jpg"));
    activeTextureSlots.insert(0);

    textureMap.emplace(3, generateGradientTexture(allocator, device, commandPool,
                                                  GRAPHICS_AND_PRESENT,
                                                  1)); // 棋盘
    activeTextureSlots.insert(3);

    textureMap.emplace(9, generateGradientTexture(allocator, device, commandPool,
                                                  GRAPHICS_AND_PRESENT,
                                                  2)); // 渐变
    activeTextureSlots.insert(9);

    std::vector<TextureSampler> samplers;
    samplers.reserve(SAMPLER_COUNT);
    samplers.emplace_back(device, 0); // 线性采样器
    samplers.emplace_back(device, 1); // 最近邻采样器

    std::array<std::vector<VkDescriptorImageInfo>, MAX_FRAMES_IN_FLIGHT>
        textureInfosPerFrame;
    std::array<std::vector<VkDescriptorImageInfo>, MAX_FRAMES_IN_FLIGHT>
        samplerInfosPerFrame;

    // 初始化每个帧的纹理和采样器信息
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        // 1. 预分配 MAX_TEXTURES 个槽位
        textureInfosPerFrame[i].resize(MAX_TEXTURES);

        // 2. 为每个活跃槽写入有效信息
        for (uint32_t slot : activeTextureSlots)
        {
            textureInfosPerFrame[i][slot] = VkDescriptorImageInfo{
                .sampler = nullptr,
                .imageView = *textureMap[slot].imageView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        // 其余槽位保持默认值（未初始化），部分绑定标志允许这样

        samplerInfosPerFrame[i] =
            samplers | std::views::transform([](const auto &s) constexpr noexcept {
                return VkDescriptorImageInfo{.sampler = s.sampler(),
                                             .imageView = nullptr,
                                             .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
            }) |
            std::ranges::to<std::vector>();
    }

    auto updateDescriptorSets =
        [&](const LogicalDevice &device, VkDescriptorSet descriptorSet,
            VkDescriptorBufferInfo bufferInfo,
            const std::vector<VkDescriptorImageInfo> &textureInfos,
            const std::vector<VkDescriptorImageInfo> &samplerInfos) {
            std::vector<VkWriteDescriptorSet> writes;
            // 绑定 0：Uniform Buffer
            writes.push_back(VkWriteDescriptorSet{
                .sType = sType<VkWriteDescriptorSet>(),
                .dstSet = descriptorSet,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &bufferInfo,
            });

            // 绑定 1：Sampled Images —— 仅更新活跃的槽位（0, 3, 9 ...）
            for (uint32_t slot : activeTextureSlots)
            {
                writes.push_back(VkWriteDescriptorSet{
                    .sType = sType<VkWriteDescriptorSet>(),
                    .dstSet = descriptorSet,
                    .dstBinding = 1,
                    .dstArrayElement = slot,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .pImageInfo = &textureInfos[slot], // 直接使用对应槽位的信息
                });
            }

            // 绑定 2：Samplers —— 采样器槽位仍是连续的，直接写入全部
            writes.push_back(VkWriteDescriptorSet{
                .sType = sType<VkWriteDescriptorSet>(),
                .dstSet = descriptorSet,
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = SAMPLER_COUNT,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .pImageInfo = samplerInfos.data(),
            });
            device.updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                        writes.data(), 0, nullptr);
        };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = uniformBuffers[i].buffer(), .offset = 0, .range = BUFFER_SIZE};
        updateDescriptorSets(device, descriptorSets[i], bufferInfo,
                             textureInfosPerFrame[i], samplerInfosPerFrame[i]);
    }

    // diff: [test_indirectdraw] start 为每个帧初始化 SSBO（binding 3）占位缓冲，并更新描述符
    std::array<PerFrameBatch, MAX_FRAMES_IN_FLIGHT> batches;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        auto &batch = batches[i];
        // 创建 16 字节的极小 SSBO，仅用于满足描述符绑定要求
        batch.objectInfoBuffer =
            create_buffer(allocator,
                          {.sType = sType<VkBufferCreateInfo>(),
                           .size = 16, // NOLINT
                           .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                          {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                    VMA_ALLOCATION_CREATE_MAPPED_BIT,
                           .usage = VMA_MEMORY_USAGE_AUTO});
        batch.objectInfoMapped = batch.objectInfoBuffer.map();

        VkDescriptorBufferInfo objInfo = {
            .buffer = batch.objectInfoBuffer.buffer(),
            .offset = 0,
            .range = VK_WHOLE_SIZE,
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSets[i],
            .dstBinding = 3,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &objInfo,
        };
        device.updateDescriptorSets(1, &write, 0, nullptr);
    } // diff: [test_indirectdraw] end

    auto depthResourcesBuild =
        create_resources{device, allocator}
            .setCreateInfo(
                {.imageType = VK_IMAGE_TYPE_2D,
                 .format = create_resources::findSupportedFormat(
                     physical_device,
                     std::array<VkFormat, 3>{VkFormat::VK_FORMAT_D32_SFLOAT,
                                             VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT,
                                             VkFormat::VK_FORMAT_D24_UNORM_S8_UINT},
                     VkImageTiling::VK_IMAGE_TILING_OPTIMAL,
                     VkFormatFeatureFlagBits::
                         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
                 .extent = {.width = swapchain.refImageExtent().width,
                            .height = swapchain.refImageExtent().height,
                            .depth = 1},
                 .mipLevels = 1,
                 .arrayLayers = 1,
                 .samples = physical_device.getMaxUsableSampleCount(),
                 .tiling = VK_IMAGE_TILING_OPTIMAL,
                 .usage =
                     VkImageUsageFlagBits::VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 .sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE,
                 .initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED,
                 // VMA 配置
                 .allocationCreateInfo = {.flags =
                                              VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                          .usage = VMA_MEMORY_USAGE_AUTO}})
            .setViewCreateInfo(
                {.viewType = VK_IMAGE_VIEW_TYPE_2D,
                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1}});
    auto depthResource = depthResourcesBuild.build();
    const auto &depthFormat_ref = depthResourcesBuild.refImageFormat();
    std::cout << "depthResource hasStencilComponent: "
              << depthResourcesBuild.hasStencilComponent() << '\n';

    auto msaaResourcesBuild =
        create_resources{device, allocator}
            .setCreateInfo(
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
                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                 .allocationCreateInfo = {.flags =
                                              VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                          .usage = VMA_MEMORY_USAGE_AUTO}})
            .setViewCreateInfo(
                {.viewType = VK_IMAGE_VIEW_TYPE_2D,
                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1}});
    auto msaaResource = msaaResourcesBuild.build();

    auto pipelineLayout =
        create_pipeline_layout{}
            .setCreateInfo({.setLayouts = {*descriptorSetLayout},
                            // diff: [test_indirectdraw] 主绘制不再使用 push constants
                            .pushConstantRanges = {}})
            .build(device);

    using stage_info = create_graphics_pipeline::stage_info;
    auto graphicsPipeline =
        create_graphics_pipeline{}
            .setCreateInfo(
                {.pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                     {.colorAttachmentCount = 1,
                      .pColorAttachmentFormats = &swapchainBuild.refImageFormat(),
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

    constexpr auto MAX_FRAMES_IN_FLIGHT = 2;
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

    mesh_base input_mesh{
        allocator, commandPool, GRAPHICS_AND_PRESENT, {vertices, indices}};
    mesh_base input_mesh2{allocator,
                          commandPool,
                          GRAPHICS_AND_PRESENT,
                          {.vertices = vertices |
                                       std::views::transform([](const Vertex &vertex) {
                                           auto newvertex = vertex;
                                           newvertex.pos[2] = 0.5;
                                           return newvertex;
                                       }) |
                                       std::ranges::to<std::vector<Vertex>>(),
                           .indices = indices}};
    constexpr auto input_mesh_id = 0;
    constexpr auto input_mesh2_id = 1;

    // 随机生成两个纹理索引和采样器索引
    uint32_t textureIndex1 = 0;
    uint32_t textureIndex2 = 3;
    uint32_t samplerIndex1 = 0;
    uint32_t samplerIndex2 = 1;

    struct record_info
    {
        uint32_t current_frame;
        uint32_t image_index;
        VkDescriptorSet descriptor_set;
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

    auto views_matrix_update = [&](glfw_input &input) {
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

    auto views_perspective_update = [&](glfw_input &input) {
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

    mcs::vulkan::event::position2d_event lastPos{};
    bool isMiddleButtonPressed = false;
    bool isLeftButtonPressed = false;
    bool rightButtonPressedLast = false; // 新增：记录上一帧右键状态
    mcs::vulkan::event::position2d_event lastLeftPos{};
    model_matrix modelMatrix_{};
    auto model_update = [&](glfw_input &input) {
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
                        modelMatrix_.scale.x *= factor;
                    else if (altPressed)
                        modelMatrix_.scale.y *= factor;
                    else if (shiftPressed)
                        modelMatrix_.scale.z *= factor;
                }
                else
                {
                    // 无修饰键或多个修饰键：均匀缩放所有轴
                    modelMatrix_.scale *= factor;
                }

                // 可选：限制缩放范围，避免过小或过大
                // modelMatrix_.scale = glm::clamp(modelMatrix_.scale, 0.01f, 100.0f);
            }
        }

        // ========== 右键单击复位物体 ==========
        if (curRightPressed && !rightButtonPressedLast)
        {
            // 按下瞬间：平移归零，旋转归单位四元数
            modelMatrix_.translation = glm::vec3(0.0f);
            modelMatrix_.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            // scale 保持 (1,1,1) 不变
            modelMatrix_.scale = {1, 1, 1};
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
                        modelMatrix_.rotation = deltaRot * modelMatrix_.rotation;
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

            glm::vec3 currentScale = modelMatrix_.scale;
            glm::quat currentRot = modelMatrix_.rotation;
            glm::vec3 localTopLeft = input_mesh2.topLeftLocal(frameContext.currentFrame);
            glm::vec3 scaledTopLeft = currentScale * localTopLeft;
            glm::vec3 rotatedOffset =
                currentRot * scaledTopLeft; // 从平移位置指向左上角的偏移量
            glm::vec3 currentTopLeftWorld = modelMatrix_.translation + rotatedOffset;

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

            glm::vec3 newTranslation = modelMatrix_.translation; // 默认不变

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
            glm::vec3 delta = newTranslation - modelMatrix_.translation;
            if (glm::length(delta) < maxDelta)
            {
                modelMatrix_.translation = newTranslation;
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

    // ========== 拾取资源 ==========
    // 创建拾取图像（R32G32_UINT）
    auto build_pick =
        create_image{device, allocator}
            .setCreateInfo(
                {.imageType = VK_IMAGE_TYPE_2D,
                 .format = VK_FORMAT_R32G32_UINT,
                 .extent = {.width = WIDTH, .height = HEIGHT, .depth = 1},
                 .mipLevels = 1,
                 .arrayLayers = 1,
                 .samples =
                     physical_device.getMaxUsableSampleCount(), //NOTE: 和主管线一样
                 .tiling = VK_IMAGE_TILING_OPTIMAL,
                 .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                 .allocationCreateInfo = {.flags =
                                              VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                          .usage = VMA_MEMORY_USAGE_AUTO}})
            .setViewCreateInfo(
                {.viewType = VK_IMAGE_VIEW_TYPE_2D,
                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1}});

    auto pickingImage = build_pick.makeImage();

    //  创建解析图像（单样本）
    auto build_resolve =
        create_image{device, allocator}
            .setCreateInfo(
                {.imageType = build_pick.createInfo().imageType,
                 .format = build_pick.createInfo().format,
                 .extent = build_pick.createInfo().extent,
                 .mipLevels = build_pick.createInfo().mipLevels,
                 .arrayLayers = build_pick.createInfo().arrayLayers,
                 .samples = VK_SAMPLE_COUNT_1_BIT,
                 .tiling = build_pick.createInfo().tiling,
                 .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |    // 作为 resolve 目标
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT |    // 用于复制像素到缓冲区
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, // ← 必须！
                 .sharingMode = build_pick.createInfo().sharingMode,
                 .initialLayout = build_pick.createInfo().initialLayout,
                 // VMA 配置
                 .allocationCreateInfo = build_pick.createInfo().allocationCreateInfo})
            .setViewCreateInfo(
                {.viewType = build_pick.viewCreateInfo().viewType,
                 .subresourceRange = build_pick.viewCreateInfo().subresourceRange});
    auto resolveImage = build_resolve.makeImage();

    // 每个飞行帧的暂存缓冲区（8 字节 = R32G32_UINT 像素）
    struct PickingPerFrame
    {
        buffer_base stagingBuffer;
        void *mappedPtr = nullptr;
    };
    std::array<PickingPerFrame, MAX_FRAMES_IN_FLIGHT> pickingFrames;
    // 当前鼠标位置（已翻转 Y 轴，与 Vulkan 视口一致）
    glm::ivec2 mousePos{0, 0};
    bool mouseValid = false;
    for (auto &pf : pickingFrames)
    {
        pf.stagingBuffer =
            create_buffer(allocator,
                          {.sType = sType<VkBufferCreateInfo>(),
                           .size = 8,
                           .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT},
                          {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                    VMA_ALLOCATION_CREATE_MAPPED_BIT,
                           .usage = VMA_MEMORY_USAGE_AUTO});
        pf.mappedPtr = pf.stagingBuffer.map();
    }

    auto pickingImageView = build_pick.makeImageView(*pickingImage);
    auto pickingResolveImageView = build_resolve.makeImageView(*resolveImage);

    // diff: [test_indirectdraw] start 大改
    // ========== 拾取管线布局 ==========

    // 拾取描述符集布局（binding 0 = UBO, binding 1 = SSBO）
    struct PickObjectInfo
    {
        uint64_t vertexBufferAddress; // 新增：顶点缓冲的设备地址
        glm::mat4 model;
        uint32_t objectId;
    };
    auto pickingDescSetLayout =
        create_descriptor_set_layout{}
            .setCreateInfo({.bindings = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                          VK_SHADER_STAGE_VERTEX_BIT},
                                         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                          VK_SHADER_STAGE_VERTEX_BIT}}})
            .build(device);
    // 推送常量范围：mat4 + uint
    auto pickingPipelineLayout =
        create_pipeline_layout{}
            .setCreateInfo({.setLayouts = {*pickingDescSetLayout}, // 改为拾取专用
                            .pushConstantRanges = {}})             // 无推送常量
            .build(device);

    auto pickingDescPool =
        create_descriptor_pool{}
            .setCreateInfo(
                {.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                 .maxSets = MAX_FRAMES_IN_FLIGHT,
                 .poolSizes = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT},
                               {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                MAX_FRAMES_IN_FLIGHT}}})
            .build(device);

    auto pickingDescSets = pickingDescPool.allocateDescriptorSets(
        {.descriptorSets = std::vector<VkDescriptorSetLayout>{MAX_FRAMES_IN_FLIGHT,
                                                              *pickingDescSetLayout}});
    struct PickingBatch
    {
        buffer_base objectInfoBuffer{}; // SSBO: PickObjectInfo[]
        void *objectInfoMapped = nullptr;
        VkDeviceSize objectInfoSize = 0;
    };
    std::array<PickingBatch, MAX_FRAMES_IN_FLIGHT> pickingBatches;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        auto &pb = pickingBatches[i];
        pb.objectInfoBuffer =
            create_buffer(allocator,
                          {.sType = sType<VkBufferCreateInfo>(),
                           .size = 16,
                           .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                          {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                    VMA_ALLOCATION_CREATE_MAPPED_BIT,
                           .usage = VMA_MEMORY_USAGE_AUTO});
        pb.objectInfoMapped = pb.objectInfoBuffer.map();

        VkDescriptorBufferInfo uboInfo{uniformBuffers[i].buffer(), 0,
                                       sizeof(UniformBufferObject)};
        VkDescriptorBufferInfo ssboInfo{pb.objectInfoBuffer.buffer(), 0, VK_WHOLE_SIZE};
        std::array<VkWriteDescriptorSet, 2> writes = {
            VkWriteDescriptorSet{
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, pickingDescSets[i], 0, 0,
                1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboInfo, nullptr},
            VkWriteDescriptorSet{
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, pickingDescSets[i], 1, 0,
                1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &ssboInfo, nullptr}};
        // vkUpdateDescriptorSets(device, 2, writes.data(), 0, nullptr);
        device.updateDescriptorSets(2, writes.data(), 0, nullptr);
    }

    // ========== 拾取管线 ==========
    auto pickingPipeline =
        create_graphics_pipeline{}
            .setCreateInfo({
                .pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                    {.colorAttachmentCount = 1,
                     .pColorAttachmentFormats = &build_pick.refImageFormat(),
                     .depthAttachmentFormat = depthFormat_ref}}),
                .stages = create_graphics_pipeline::makeStages(
                    stage_info{.stage = VK_SHADER_STAGE_VERTEX_BIT,
                               .filePath = "shaders/picking2.vert.spv",
                               .pName = "main"},
                    stage_info{.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                               .filePath = "shaders/picking2.frag.spv",
                               .pName = "main"}),
                .vertexInputState = {}, //不需要 layout(location=0) in vec3 inPosition 了
                .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
                .viewportState = {.viewports = {{}}, .scissors = {{}}}, //至少一个
                .rasterizationState = {.depthClampEnable = VK_FALSE,
                                       .rasterizerDiscardEnable = VK_FALSE,
                                       .polygonMode = VK_POLYGON_MODE_FILL,
                                       .cullMode = VK_CULL_MODE_NONE,
                                       .frontFace = VK_FRONT_FACE_CLOCKWISE,
                                       .lineWidth = 1.0F},
                .multisampleState =
                    {
                        .rasterizationSamples = physical_device.getMaxUsableSampleCount(),
                    },
                .depthStencilState = {.depthTestEnable = VK_TRUE,
                                      .depthWriteEnable = VK_FALSE,
                                      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL},
                .colorBlendState = {.logicOp = {},
                                    .attachments = {{.blendEnable = VK_FALSE,
                                                     .colorWriteMask =
                                                         VK_COLOR_COMPONENT_R_BIT |
                                                         VK_COLOR_COMPONENT_G_BIT |
                                                         VK_COLOR_COMPONENT_B_BIT |
                                                         VK_COLOR_COMPONENT_A_BIT}}},
                .dynamicState = {.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                   VK_DYNAMIC_STATE_SCISSOR}},
                .layout = *pickingPipelineLayout,
                .renderPass = VK_NULL_HANDLE,
            })
            .build(device);
    // diff: [test_indirectdraw] end

    // diff: [test_imgui] start
    // 创建专供 ImGui 使用的描述符池
    auto imguiDescPool =
        create_descriptor_pool{}
            .setCreateInfo(
                {.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
                          VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                 .maxSets = 1,
                 .poolSizes = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE}}})
            .build(device);
    // 确保 vkGetDeviceProcAddr 全局指针有效

    assert(vkGetInstanceProcAddr != nullptr);
    assert(vkGetInstanceProcAddr != nullptr);
    assert(vkGetDeviceProcAddr != nullptr);
    volkLoadDevice(*device);
    assert(vkGetDeviceProcAddr != nullptr && "Don't forget to call volkLoadDevice!");
    // 测试动态渲染函数
    auto fn = vkGetDeviceProcAddr(*device, "vkCmdBeginRendering");
    std::cout << "vkCmdBeginRendering: " << (void *)fn << std::endl;
    assert(fn != nullptr);

    ImGui_ImplVulkan_InitInfo init_info = {
        .ApiVersion = APIVERSION,
        .Instance = *instance,
        .PhysicalDevice = *physical_device,
        .Device = *device,
        .QueueFamily = GRAPHICS_QUEUE_FAMILY_IDX,
        .Queue = *GRAPHICS_AND_PRESENT,
        .DescriptorPool = *imguiDescPool,
        .MinImageCount = 2,
        .ImageCount = static_cast<uint32_t>(swapchain.imagesSize()),
        // 动态渲染核心配置 重要：从 v1.92.7 开始，渲染配置已移到 PipelineInfoMain 结构体中
        .PipelineInfoMain =
            {
                .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
                .PipelineRenderingCreateInfo =
                    {
                        .sType = sType<VkPipelineRenderingCreateInfoKHR>(),
                        .pNext = nullptr,
                        .viewMask = 0,
                        .colorAttachmentCount = 1,
                        .pColorAttachmentFormats =
                            &swapchainBuild
                                 .refImageFormat(), // 颜色附件格式（与你的交换链相同）
                        .depthAttachmentFormat =
                            VK_FORMAT_UNDEFINED, // 深度格式：UI 不写深度，可以填 UNDEFINED
                        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED // 模板格式
                    },
            },
        .UseDynamicRendering = true,
    };
    // 执行初始化

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    assert(vkGetInstanceProcAddr != nullptr);
    // GLFW 后端初始化：若想避免与你的 glfw_input 冲突，设置 install_callbacks = false
    ImGui_ImplGlfw_InitForVulkan(window.data(), false); // 手动输入更新
    // 加载 ImGui 所需的所有 Vulkan 函数（使用你已有的 volk 全局指针）
    bool loader_ok = ImGui_ImplVulkan_LoadFunctions(
        VK_API_VERSION_1_3,
        // 加载器：ImGui 会给一个函数名字符串，我们用 vkGetInstanceProcAddr 解析
        [](const char *function_name, void *user_data) -> PFN_vkVoidFunction {
            // user_data 我们传入了 VkInstance
            return vkGetInstanceProcAddr((VkInstance)user_data, function_name);
        },
        (void *)*instance // 用户数据：instance 句柄
    );
    assert(loader_ok);

    // 现在 Init
    ImGui_ImplVulkan_Init(&init_info);

    // diff: [test_imgui] end

    // NOLINTNEXTLINE
    const auto recordCommandBuffer = [&](const CommandBufferView &commandBuffer,
                                         record_info info) {
        auto [currentFrame, imageIndex, descriptorSet] = info;
        VkImage image = swapchain.image(imageIndex);
        VkImageView imageView = swapchain.imageView(imageIndex);
        auto imageExtent = swapchain.imageExtent();
        VkImage depthImage = depthResource.image();
        VkImageView depthImageView = depthResource.imageView();

        VkImage msaaImage = msaaResource.image();
        VkImageView msaaImageView = msaaResource.imageView();

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});

        auto uploadUniformBuffers = [&]() {
            auto *uniformBuffersPtr = uniformBuffersMapped[currentFrame];

            UniformBufferObject ubo{};
            ubo.view = camera.viewsMatrix();

            ubo.proj = camera.perspectiveMatrix();
            ubo.proj[1][1] *= -1;

            // 复制数据到Uniform Buffer
            memcpy(uniformBuffersPtr, &ubo, sizeof(ubo));
        };
        uploadUniformBuffers();

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
                        .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
            my_render::image_info{
                .image = msaaImage,
                .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .access_mask = VK_ACCESS_2_NONE,
                        .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
            my_render::image_info{
                .image = depthImage,
                .aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .access_mask = VK_ACCESS_2_NONE,
                        .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                .dst = {.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                        .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT}});

        VkRenderingAttachmentInfo colorAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = msaaImageView,
            .imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
            .resolveImageView = imageView,
            .resolveImageLayout = VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}}};
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
             .colorAttachmentCount = 1,
             .pColorAttachments = &colorAttachment,
             .pDepthAttachment = &depthAttachment});

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
        commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLayout,
                                         0, 1, &(descriptorSet), 0, nullptr);

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

        commandBuffer.endRendering();

        if (mouseValid)
        {
            VkImage depthImg = depthResource.image();
            VkImageView depthView = depthResource.imageView();

            //diff: [test_picking5] 同步2要求由单个 xxxStageMask 分解成 xxxStageMask + xxxAccessMask. 注意有规律 draw前做布局转换，成环就是最好的，src dest 开源转换
            // https://docs.vulkan.org/guide/latest/extensions/VK_KHR_synchronization2.html#:~:text=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR%3B%0A%20%20.dstAccessMask%20%3D%20VK_ACCESS_2_NONE_KHR%3B-,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,-%E5%9C%A8%E7%AC%AC%E4%B8%80%E5%90%8C%E6%AD%A5

            // ① 拾取图像 → COLOR_ATTACHMENT
            // ② 深度 → 只读
            // ③ 解析图像 → COLOR_ATTACHMENT（作为 resolve 目标）
            my_render::transition_image_layout(
                commandBuffer,
                my_render::image_info{
                    .image = *pickingImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
                my_render::image_info{
                    .image = depthImg,
                    .aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                            .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                            .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT}},
                my_render::image_info{
                    .image = *resolveImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}});

            // ④ 开始拾取渲染
            VkRenderingAttachmentInfo pickColorAtt{
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = *pickingImageView,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
                .resolveImageView = *pickingResolveImageView,
                .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.uint32 = {0xFFFFFFFF, 0xFFFFFFFF, 0, 0}}}};
            VkRenderingAttachmentInfo pickDepthAtt{
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = depthView,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
            commandBuffer.beginRendering(
                {.sType = sType<VkRenderingInfo>(),
                 .renderArea = {.offset = {0, 0}, .extent = imageExtent},
                 .layerCount = 1,
                 .colorAttachmentCount = 1,
                 .pColorAttachments = &pickColorAtt,
                 .pDepthAttachment = &pickDepthAtt});

            commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *pickingPipeline);
            commandBuffer.setViewport(
                0, std::array<VkViewport, 1>{VkViewport{
                       0, 0, (float)imageExtent.width, (float)imageExtent.height, 0, 1}});
            commandBuffer.setScissor(
                0, std::array<VkRect2D, 1>{VkRect2D{{0, 0}, imageExtent}});

            //diff: [test_indirectdraw] start . picking的顶点数据 和 主管线一样？
            // 绑定主渲染的合并索引缓冲（因为拾取使用同样的顶点数据）
            commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             *pickingPipelineLayout, 0, 1,
                                             &pickingDescSets[currentFrame], 0, nullptr);

            // diff: 拾取复用主渲染的间接命令缓冲
            auto &mainBatch = batches[currentFrame];
            commandBuffer.bindIndexBuffer(mainBatch.mergedIndexBuffer.buffer(), 0,
                                          VK_INDEX_TYPE_UINT32);
            commandBuffer.drawIndexedIndirect(mainBatch.indirectDrawBuffer.buffer(), 0,
                                              mainBatch.drawCount,
                                              sizeof(VkDrawIndexedIndirectCommand));
            //diff: [test_indirectdraw] end

            commandBuffer.endRendering();

            // ⑤ 解析图像 → TRANSFER_SRC
            my_render::transition_image_layout(
                commandBuffer,
                my_render::image_info{
                    .image = *resolveImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
                            .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT}});

            // ⑥ 复制指定像素到暂存缓冲区
            commandBuffer.copyImageToBuffer(
                *resolveImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                pickingFrames[currentFrame].stagingBuffer.buffer(),
                std::array<VkBufferImageCopy, 1>{VkBufferImageCopy{
                    .bufferOffset = 0,
                    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .imageOffset = {mousePos.x, mousePos.y, 0},
                    .imageExtent = {1, 1, 1}}});

            // ⑦ 解析图像：TRANSFER_SRC → COLOR_ATTACHMENT（为下一帧 resolve 目标做好准备）
            // ⑧ 深度图像：只读 → DEPTH_ATTACHMENT_OPTIMAL（交还给主渲染）
            my_render::transition_image_layout(
                commandBuffer,
                my_render::image_info{
                    .image = *resolveImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
                my_render::image_info{
                    .image = depthImg,
                    .aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT}});
        }

        // diff: [test_imgui] start
        // ============ 绘制 ImGui（第二个动态渲染通道） ============
        // 此时 image 已经 resolve 并处于 COLOR_ATTACHMENT_OPTIMAL
        VkRenderingAttachmentInfo imguiColorAtt{
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = imageView, // swapchain 图像视图（非 MSAA）
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, // 保留主场景已绘制内容
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };
        commandBuffer.beginRendering({
            .sType = sType<VkRenderingInfo>(),
            .renderArea = {.offset = {0, 0}, .extent = imageExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &imguiColorAtt,
            .pDepthAttachment = nullptr, // UI 不写深度
        });
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *commandBuffer);
        commandBuffer.endRendering();
        // diff: [test_imgui] end

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

    // diff: [test_indirectdraw] start prepareBatch：动态分配合批资源并填充数据
    auto prepareBatch = [&](uint32_t currentFrame) {
        auto &batch = batches[currentFrame];

        // 收集当前帧所有要绘制的物体（您可以随意增减）
        struct Obj
        {
            mesh_base *mesh;
            uint32_t tex;
            uint32_t samp;
        };
        std::vector<Obj> objects = {{&input_mesh, textureIndex1, samplerIndex1},
                                    {&input_mesh2, textureIndex2, samplerIndex2}};
        uint32_t objectCount = static_cast<uint32_t>(objects.size());

        // 计算总索引数
        uint32_t totalIndexCount = 0;
        for (auto &o : objects)
            totalIndexCount += static_cast<uint32_t>(o.mesh->queue_data.indices.size());

        // 1. 保证合并索引缓冲足够大
        VkDeviceSize needIdx = totalIndexCount * sizeof(uint32_t);
        if (batch.mergedIndexCapacity < needIdx)
        {
            if (batch.mergedIndexMapped)
            {
                batch.mergedIndexBuffer.unmap();
                batch.mergedIndexBuffer.destroy();
            }
            batch.mergedIndexBuffer =
                create_buffer(allocator,
                              {.sType = sType<VkBufferCreateInfo>(),
                               .size = needIdx,
                               .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                        VMA_ALLOCATION_CREATE_MAPPED_BIT,
                               .usage = VMA_MEMORY_USAGE_AUTO});
            batch.mergedIndexMapped = batch.mergedIndexBuffer.map();
            batch.mergedIndexCapacity = needIdx;
        }

        // 2. 保证 ObjectInfo SSBO 足够大
        VkDeviceSize needObj = objectCount * sizeof(ObjectInfo);
        if (batch.objectInfoCapacity < needObj)
        {
            if (batch.objectInfoMapped)
            {
                batch.objectInfoBuffer.unmap();
                batch.objectInfoBuffer.destroy();
            }
            batch.objectInfoBuffer =
                create_buffer(allocator,
                              {.sType = sType<VkBufferCreateInfo>(),
                               .size = needObj,
                               .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                        VMA_ALLOCATION_CREATE_MAPPED_BIT,
                               .usage = VMA_MEMORY_USAGE_AUTO});
            batch.objectInfoMapped = batch.objectInfoBuffer.map();
            batch.objectInfoCapacity = needObj;

            // 更新描述符集中的 binding 3 指向新缓冲
            VkDescriptorBufferInfo bufInfo{
                .buffer = batch.objectInfoBuffer.buffer(),
                .offset = 0,
                .range = VK_WHOLE_SIZE,
            };
            VkWriteDescriptorSet write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[currentFrame],
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
            if (batch.indirectDrawMapped)
            {
                batch.indirectDrawBuffer.unmap();
                batch.indirectDrawBuffer.destroy();
            }
            batch.indirectDrawBuffer =
                create_buffer(allocator,
                              {.sType = sType<VkBufferCreateInfo>(),
                               .size = needCmd,
                               .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                        VMA_ALLOCATION_CREATE_MAPPED_BIT,
                               .usage = VMA_MEMORY_USAGE_AUTO});
            batch.indirectDrawMapped = batch.indirectDrawBuffer.map();
            batch.indirectCmdCapacity = needCmd;
        }

        // 4. 填充数据
        uint32_t idxOff = 0;
        std::vector<ObjectInfo> infos(objectCount);
        std::vector<VkDrawIndexedIndirectCommand> cmds(objectCount);
        for (uint32_t i = 0; i < objectCount; ++i)
        {
            auto &m = *objects[i].mesh;
            uint32_t cnt = static_cast<uint32_t>(m.queue_data.indices.size());
            // 拷贝索引数据到合并缓冲
            memcpy((uint32_t *)batch.mergedIndexMapped + idxOff,
                   m.queue_data.indices.data(), cnt * sizeof(uint32_t));

            cmds[i] = {
                .indexCount = cnt,
                .instanceCount = 1,
                .firstIndex = idxOff,
                .vertexOffset = 0,
                .firstInstance = i, // 关键：每个物体不同的 firstInstance
            };
            infos[i] = {.vertexBufferAddress = m.getVertexBufferAddress(currentFrame),
                        .objectDataAddress = m.getObjectDataAddress(currentFrame),
                        .textureIndex = objects[i].tex,
                        .samplerIndex = objects[i].samp};
            idxOff += cnt;
        }
        memcpy(batch.objectInfoMapped, infos.data(), needObj);
        memcpy(batch.indirectDrawMapped, cmds.data(), needCmd);
        batch.drawCount = objectCount;
    };

    auto preparePickingBatch = [&](uint32_t currentFrame, auto mesh1_model,
                                   auto mesh2_model) {
        auto &pb = pickingBatches[currentFrame];
        constexpr uint32_t objectCount = 2;

        // 重建拾取 SSBO
        VkDeviceSize needObj = objectCount * sizeof(PickObjectInfo);
        if (pb.objectInfoSize < needObj)
        {
            if (pb.objectInfoMapped)
            {
                pb.objectInfoBuffer.unmap();
                pb.objectInfoBuffer.destroy();
            }
            pb.objectInfoBuffer =
                create_buffer(allocator,
                              {.sType = sType<VkBufferCreateInfo>(),
                               .size = needObj,
                               .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                              {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                        VMA_ALLOCATION_CREATE_MAPPED_BIT,
                               .usage = VMA_MEMORY_USAGE_AUTO});
            pb.objectInfoMapped = pb.objectInfoBuffer.map();
            pb.objectInfoSize = needObj;

            // 更新描述符绑定
            VkDescriptorBufferInfo ssboInfo = {pb.objectInfoBuffer.buffer(), 0,
                                               VK_WHOLE_SIZE};
            VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                          nullptr,
                                          pickingDescSets[currentFrame],
                                          1,
                                          0,
                                          1,
                                          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                          nullptr,
                                          &ssboInfo,
                                          nullptr};
            device.updateDescriptorSets(1, &write, 0, nullptr);
        }

        // 填充数据（仅 SSBO）
        std::vector<PickObjectInfo> pickInfos(objectCount);
        pickInfos[0] = {input_mesh.getVertexBufferAddress(currentFrame),
                        glm::rotate(glm::mat4(1.0f), mesh1_model, glm::vec3(0, 0, 1)),
                        input_mesh_id};
        pickInfos[1] = {input_mesh2.getVertexBufferAddress(currentFrame), mesh2_model,
                        input_mesh2_id};
        memcpy(pb.objectInfoMapped, pickInfos.data(), needObj);
    };
    // diff: [test_indirectdraw] end

    // NOLINTNEXTLINE
    const auto recreateSwapChain = [&]() constexpr {
        surface.waitGoodFramebufferSize();
        device.waitIdle();

        swapchain = swapchainBuild.rebuild();
        auto newExtent = swapchain.refImageExtent();
        VkExtent3D imageExtent = {
            .width = newExtent.width, .height = newExtent.height, .depth = 1};

        msaaResource = msaaResourcesBuild.updateImageExtent(imageExtent).rebuild();
        depthResource = depthResourcesBuild.updateImageExtent(imageExtent).rebuild();
        camera.refPerspectiveMatrix().aspect =
            newExtent.width / static_cast<float>(newExtent.height);

        pickingImage = build_pick.updateImageExtent(imageExtent).rebuild();
        pickingImageView = build_pick.makeImageView(*pickingImage);
        resolveImage = build_resolve.updateImageExtent(imageExtent).rebuild();
        pickingResolveImageView = build_resolve.makeImageView(*resolveImage);
        mouseValid = false;

        frameContext.rebuild(swapchain.imagesSize());

        // diff: [test_imgui] start
        ImGui_ImplVulkan_SetMinImageCount(swapchain.imagesSize());
        // diff: [test_imgui] end
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

        // 等待栅栏后，正式获取图像前
        if (currentFrame > 0 && mouseValid)
        {
            uint32_t readIdx =
                (currentFrame - 1 + MAX_FRAMES_IN_FLIGHT) % MAX_FRAMES_IN_FLIGHT;
            auto *data = static_cast<uint32_t *>(pickingFrames[readIdx].mappedPtr);
            uint32_t objId = data[0];
            uint32_t primId = data[1];
            if (objId != 0xFFFFFFFF)
            {
                std::cout << "Hover: Object=" << objId << ", Triangle=" << primId << "\n";
            }
        }

        // diff: [test_indirectdraw] start
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(
                         currentTime - startTime)
                         .count();
        auto mesh1_model = time * glm::radians(90.0F);
        auto mesh2_model = modelMatrix_();
        // 写的是物体私有的 mapped 缓冲，与合批无关，仍须保留，只是移出绘制块。将其移到 uploadUniformBuffers() 调用之后、合批绘制之前
        // 更新两个物体的模型矩阵（映射内存，不涉及合批）
        mesh_base::updateObjectData(input_mesh.frameBuffers[currentFrame], [&] {
            return glm::rotate(glm::mat4(1.0F), mesh1_model, glm::vec3(0.0F, 0.0F, 1.0F));
        });
        mesh_base::updateObjectData(input_mesh2.frameBuffers[currentFrame],
                                    [&] { return mesh2_model; });
        // diff: [test_indirectdraw] end

        // diff: [test_indirectdraw] start
        input_mesh.requestUpdate(allocator, currentFrame);
        input_mesh2.requestUpdate(
            allocator, currentFrame); // 如果 input_mesh2 也可能动态更新，保持一致
        prepareBatch(currentFrame);
        preparePickingBatch(currentFrame, mesh1_model, mesh2_model); // 新增
        // diff: [test_indirectdraw] end

        auto [result, imageIndex] = swapchain.acquireNextImage(
            UINT64_MAX, presentCompleteSemaphore[semaphoreIndex], nullptr);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            recreateSwapChain();
            return;
        }
        if (result != VK_SUCCESS)
            throw std::runtime_error("failed to acquire swap chain image!");
        logicalDevice->resetFences(1, inFlightFences[currentFrame]);

        const auto &commandBuffer = commandBuffers[currentFrame];
        commandBuffer.reset({});

        recordCommandBuffer(commandBuffer,
                            {.current_frame = currentFrame,
                             .image_index = imageIndex,
                             .descriptor_set = descriptorSets[currentFrame]});

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

    constexpr float TARGET_FPS = 60.0F;                    // 目标帧率（可改144/30等）
    constexpr float TARGET_FRAME_TIME = 1.0F / TARGET_FPS; // 目标帧间隔（秒）
    auto lastFrameTime = std::chrono::high_resolution_clock::now();

    while (window.shouldClose() == 0)
    {
        // 1. 计算当前帧开始时间
        auto currentFrameStart = std::chrono::high_resolution_clock::now();

        // 2. 计算上一帧的耗时 + 帧率限制等待
        std::chrono::duration<float> frameDuration = currentFrameStart - lastFrameTime;
        float frameTime = frameDuration.count();

        // 如果当前帧耗时小于目标帧间隔，等待补足时间
        if (frameTime < TARGET_FRAME_TIME)
        {
            auto sleepTime = std::chrono::duration<float>(TARGET_FRAME_TIME - frameTime);
            std::this_thread::sleep_for(
                std::chrono::duration_cast<std::chrono::microseconds>(sleepTime));
        }

        // 3. 重新计算真实的deltaTime（用于移动逻辑）
        auto currentFrameTime = std::chrono::high_resolution_clock::now();
        float deltaTime =
            std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
        deltaTime = glm::clamp(deltaTime, 0.001f, 0.1f); // 限制最大时间差
        lastFrameTime = currentFrameTime;

        input.scroll() = {};

        surface::pollEvents();

        auto cur = input.cursorPos();
        auto ext = swapchain.refImageExtent();
        if (cur.xpos >= 0 && cur.xpos < static_cast<int>(ext.width) && cur.ypos >= 0 &&
            cur.ypos < static_cast<int>(ext.height))
        {
            mousePos = {cur.xpos, cur.ypos};
            mouseValid = true;
        }
        else
        {
            mouseValid = false;
        }

        views_matrix_update(input);
        views_perspective_update(input);
        model_update(input);

        auto now = std::chrono::steady_clock::now();
        static auto lastUpdate = std::chrono::steady_clock::now();
        if (now - lastUpdate > std::chrono::seconds(2)) // NOLINT
        {
            lastUpdate = now;

            //NOTE: 保证更新再 draw 之前 肯定是安全的
            // 选择下一个未占用的槽位（循环使用，或按需指定）
            uint32_t newSlot = 0;
            while (activeTextureSlots.count(newSlot) > 0 && newSlot < MAX_TEXTURES)
            {
                ++newSlot;
            }
            if (newSlot >= MAX_TEXTURES)
            {
                // 槽位已满，可根据需要处理（例如替换最旧的槽）
                newSlot = 0; // 简单示例
            }
            // 创建新纹理并插入映射
            textureMap[newSlot] =
                TextureImage::fromFile(allocator, device, commandPool,
                                       GRAPHICS_AND_PRESENT, "textures/viking_room.png");
            activeTextureSlots.insert(newSlot);

            // 更新所有飞行帧的描述符集（单个槽）
            for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
            {
                textureInfosPerFrame[frame][newSlot] = VkDescriptorImageInfo{
                    .sampler = nullptr,
                    .imageView = *textureMap[newSlot].imageView(),
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet write{
                    .sType = sType<VkWriteDescriptorSet>(),
                    .dstSet = descriptorSets[frame],
                    .dstBinding = 1,
                    .dstArrayElement = newSlot,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .pImageInfo = &textureInfosPerFrame[frame][newSlot]};
                device.updateDescriptorSets(1, &write, 0, nullptr);
            }
            // 将物体1的纹理索引指向这个新槽
            textureIndex1 = newSlot;

            static bool isTriangle = true;
            if (isTriangle)
            {
                static int gridSize = 5;       // 初始 1x1 网格（即2个三角形）
                gridSize = (gridSize % 8) + 1; // 循环 1,2,3,...,8 然后回到1

                std::vector<Vertex> newVertices;
                std::vector<uint32_t> newIndices;

                int N = gridSize; // 每边分段数
                int numVertices = (N + 1) * (N + 1);
                int numTriangles = N * N * 2;
                int numIndices = numTriangles * 3;

                newVertices.reserve(numVertices);
                newIndices.reserve(numIndices);

                // 生成顶点网格（从左下角开始，行优先）
                float step = 1.0f / N; // 边长1.0
                for (int j = 0; j <= N; ++j)
                {
                    float y = -0.5f + j * step; // 从下往上
                    for (int i = 0; i <= N; ++i)
                    {
                        float x = -0.5f + i * step; // 从左往右
                        newVertices.push_back(
                            {.pos = {x, y, 0.0f},
                             .color = {1.0f, 1.0f, 1.0f}, // 白色，你可以随意
                             .texCoord = {static_cast<float>(i) / N,
                                          static_cast<float>(j) / N}});
                    }
                }

                // 生成索引（每个小格子两个三角形）
                for (int j = 0; j < N; ++j)
                {
                    for (int i = 0; i < N; ++i)
                    {
                        // 当前格子的四个顶点索引
                        uint32_t topLeft = j * (N + 1) + i;
                        uint32_t topRight = j * (N + 1) + i + 1;
                        uint32_t bottomLeft = (j + 1) * (N + 1) + i;
                        uint32_t bottomRight = (j + 1) * (N + 1) + i + 1;

                        // 第一个三角形（左上-右下）
                        newIndices.push_back(topLeft);
                        newIndices.push_back(bottomLeft);
                        newIndices.push_back(bottomRight);

                        // 第二个三角形（左上-右上-右下）
                        newIndices.push_back(topLeft);
                        newIndices.push_back(bottomRight);
                        newIndices.push_back(topRight);
                    }
                }

                input_mesh.queueUpdate(newVertices, newIndices);

                std::cout << "当前网格: " << N << "x" << N
                          << "，三角形数量: " << numTriangles
                          << "，primitiveId 范围: 0 ~ " << numTriangles - 1 << "\n";
            }
            else
            {
                input_mesh.queueUpdate(vertices, indices);
            }
            isTriangle = !isTriangle;
        }
        // diff: [test_imgui] start
        // ============ 更新 ImGui 输入状态 ============
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize =
            ImVec2(static_cast<float>(ext.width), static_cast<float>(ext.height));
        io.DeltaTime = deltaTime; // 你前面计算好的帧间隔

        // 鼠标位置（使用你已有的 cursorPos）
        io.MousePos = ImVec2(static_cast<float>(cur.xpos), static_cast<float>(cur.ypos));

        // 鼠标按键（直接从 GLFW 获取，或者通过你的 glfw_input）
        io.MouseDown[0] =
            glfwGetMouseButton(window.data(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        io.MouseDown[1] =
            glfwGetMouseButton(window.data(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        io.MouseDown[2] =
            glfwGetMouseButton(window.data(), GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;

        // 键盘按键：如果你需要支持键盘输入，也需要通过类似方式填充 io.KeysDown
        // 字符输入则需要通过 GLFW 字符回调收集后调用 io.AddInputCharacter()

        // ============ 开始 ImGui 新帧 ============
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ============ 构建你的 UI ============
        // 获取当前窗口（GLFW）的客户区大小
        auto window_width = static_cast<float>(ext.width);
        auto window_height = static_cast<float>(ext.height);

        // 设置 Demo 窗口固定在右侧，高度占满，宽度为窗口宽度的 30%
        float panel_width = window_width * 0.3f;    // 右侧面板宽度
        float panel_x = window_width - panel_width; // X 坐标：窗口右沿 - 面板宽度
        float panel_y = 0.0f;                       // 贴顶
        float panel_height = window_height;         // 高度占满

        ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panel_width, panel_height), ImGuiCond_Always);

        // 防止面板被拖到屏幕外/改变大小（可选，如果想完全锁定）
        ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoCollapse;
        ImGui::Begin("My Tools", nullptr,
                     panel_flags); // 用 Begin 替代 ShowDemoWindow 更灵活
        ImGui::ShowDemoWindow();   // 你可以继续嵌在里面，或直接在这里写自己的 UI
        ImGui::End();

        // ============ 渲染（生成绘制数据） ============
        ImGui::Render();
        // diff: [test_imgui] end
        drawFrame();
    }
    device.waitIdle();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (uniformBuffersMapped[i] != nullptr)
        {
            uniformBuffers[i].unmap();
            uniformBuffersMapped[i] = nullptr;
        }
    }
    for (auto &pf : pickingFrames)
    {
        pf.stagingBuffer.unmap();
        pf.mappedPtr = nullptr;
    }

    // -------------------------------------------------------
    // diff: [test_indirectdraw] 清理间接绘制资源
    // -------------------------------------------------------
    for (auto &batch : batches)
    {
        // 合并索引缓冲
        if (batch.mergedIndexMapped)
        {
            batch.mergedIndexBuffer.unmap();
            batch.mergedIndexMapped = nullptr;
        }
        // 对象信息 SSBO
        if (batch.objectInfoMapped)
        {
            batch.objectInfoBuffer.unmap();
            batch.objectInfoMapped = nullptr;
        }
        // 间接绘制命令缓冲
        if (batch.indirectDrawMapped)
        {
            batch.indirectDrawBuffer.unmap();
            batch.indirectDrawMapped = nullptr;
        }
    }

    for (auto &pb : pickingBatches)
    {
        if (pb.objectInfoMapped)
        {
            pb.objectInfoBuffer.unmap();
            pb.objectInfoMapped = nullptr;
        }
    }
    // -------------------------------------------------------

    // diff: [test_imgui] start
    // 清理 ImGui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    // diff: [test_imgui] end

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}