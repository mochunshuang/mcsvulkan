
#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>
#include <chrono>
#include <random>
#include <thread>
#include <utility>

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

using buffer_base = mcs::vulkan::vma::buffer_base;
using mcs::vulkan::vma::create_buffer;
using mcs::vulkan::vma::staging_buffer;

using mcs::vulkan::vma::create_resources;
using mcs::vulkan::vma::create_image;

using mcs::vulkan::tool::simple_copy_buffer;

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

        // diff: [test_model_matrix2] start
        buffer_base objectDataBuffer;
        VkDeviceAddress objectDataAddress = 0;
        VmaAllocationInfo allocInfo;
        // diff: [test_model_matrix2] end
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
    // VkSampler textureSampler_ = VK_NULL_HANDLE;
    // diff: [移除] VkSampler textureSampler_ = VK_NULL_HANDLE;

    int texWidth_ = 0;
    int texHeight_ = 0;
    int texChannels_ = 0;

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
                .levelCount = mipLevels, // diff: [test_mipmapping]
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
        int32_t tex_width, tex_height;
        uint32_t mip_levels;
    };

    // diff: [修改] 移除samplerType参数，不创建采样器
    TextureImage(VmaAllocator allocator, const LogicalDevice &device,
                 const CommandPool &pool, const Queue &queue, const std::string &path,
                 bool generateTexture = false, int textureType = 0)
    {
        VkDeviceSize imageSize; // NOLINT
        uint8_t *pixels = nullptr;

        if (!generateTexture)
        {
            // 从文件加载纹理
            pixels = ::stbi_load(path.c_str(), &texWidth_, &texHeight_, &texChannels_,
                                 STBI_rgb_alpha);
            if (!pixels)
            {
                throw std::runtime_error("failed to load texture image!");
            }
            imageSize = texWidth_ * texHeight_ * STBI_rgb_alpha;
        }
        else
        {
            // 生成程序纹理
            texWidth_ = 256;
            texHeight_ = 256;
            texChannels_ = 4;
            imageSize = texWidth_ * texHeight_ * STBI_rgb_alpha;
            pixels = new uint8_t[imageSize];

            if (textureType == 1)
            {
                // 生成红蓝棋盘纹理
                uint8_t color1[4] = {255, 0, 0, 255}; // 红色
                uint8_t color2[4] = {0, 0, 255, 255}; // 蓝色
                generateCheckerboardTexture(pixels, texWidth_, texHeight_, color1,
                                            color2);
            }
            else if (textureType == 2)
            {
                // 生成绿色到黄色渐变纹理
                uint8_t topColor[4] = {0, 255, 0, 255};      // 绿色
                uint8_t bottomColor[4] = {255, 255, 0, 255}; // 黄色
                generateGradientTexture(pixels, texWidth_, texHeight_, topColor,
                                        bottomColor);
            }
        }

        // 创建暂存缓冲区
        auto stagingBuffer = staging_buffer(allocator, imageSize);
        stagingBuffer.copyDataToBuffer(pixels, imageSize);
        if (generateTexture)
            delete[] pixels;
        else
            ::stbi_image_free(pixels);

        // diff: [test_mipmapping] start
        auto mipLevels = getMipLevels(texWidth_, texHeight_);
        // diff: [test_mipmapping] end

        auto build =
            create_image{device, allocator}
                .setCreateInfo(
                    {.imageType = VK_IMAGE_TYPE_2D,
                     .format = VK_FORMAT_R8G8B8A8_SRGB,
                     .extent = {.width = static_cast<uint32_t>(texWidth_),
                                .height = static_cast<uint32_t>(texHeight_),
                                .depth = 1},
                     .mipLevels = mipLevels, // diff: [test_mipmapping]
                     .arrayLayers = 1,
                     .samples = VK_SAMPLE_COUNT_1_BIT,
                     .tiling = VK_IMAGE_TILING_OPTIMAL,
                     .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | // diff: [test_mipmapping]
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
                                          .levelCount =
                                              mipLevels, // diff: [test_mipmapping]
                                          .baseArrayLayer = 0,
                                          .layerCount = 1}});

        textureImage_ = build.makeImage();
        transitionImageLayout(pool, queue, *textureImage_,
                              VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED,
                              VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              mipLevels); // diff: [test_mipmapping]
        copyBufferToImage(pool, queue, *stagingBuffer, *textureImage_,
                          VkExtent2D{.width = static_cast<uint32_t>(texWidth_),
                                     .height = static_cast<uint32_t>(texHeight_)});

        // diff: [test_mipmapping] start
        // transitionImageLayout(pool, queue, *textureImage_,
        //                       VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        //                       VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        generateMipmaps(pool, queue, *textureImage_, build.refImageFormat(),
                        mipmap_param{.tex_width = texWidth_,
                                     .tex_height = texHeight_,
                                     .mip_levels = mipLevels});
        // diff: [test_mipmapping] end

        imageView_ = build.makeImageView(*textureImage_);
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
        VkFormatProperties formatProperties =
            logicalDevice->physicalDevice()->getFormatProperties(imageFormat);

        if ((formatProperties.optimalTilingFeatures &
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
            VkImageBlit blit = {
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
                                .z = 1}}};

            // NOTE: 该命令执行复制、缩放和过滤操作。
            // 我们的纹理图像现在有多个mip级别，但是暂存缓冲区只能用于填充mip级别0
            // 如果您使用专用传输队列（如顶点缓冲区中建议的），请注意：vkCmdBlitImage必须提交到具有图形功能的队列
            commandBuffer.blitImage(
                image, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, std::span{&blit, 1},
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
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

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

        // diff: [test_mipmapping] start
        // NOTE:
        //  请注意，采样器不会在任何地方引用VkImage。采样器是一个独特的对象，它提供了一个从纹理中提取颜色的接口。
        //  它可以应用于您想要的任何图像，无论是1D、2D还是3D。

        // 当对纹理进行采样时，采样器根据以下伪代码选择mip级别：
        /*
lod = getLodLevelFromScreenSize(); //smaller when the object is close, may be negative
lod = clamp(lod + mipLodBias, minLod, maxLod);

level = clamp(floor(lod), 0, texture.mipLevels - 1);  //clamped to the number of mip
levels in the texture

if (mipmapMode == vk::SamplerMipmapMode::eNearest) {
    color = sample(level);
} else {
    color = blend(sample(level), sample(level + 1));
}
//lod用于选择要采样的两个mip级别。这些级别被采样，结果被线性混合

if (lod <= 0) {
    color = readTexture(uv, magFilter);
} else {
    color = readTexture(uv, minFilter);
}
*/
        // diff: [test_mipmapping] end
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

// diff: [texture] end

// diff: [camera_view] start
enum class eye_member : std::uint8_t
{
    eX,
    eY,
    eZ
};
template <eye_member member>
auto update_eye(camera_interface &camera, float step) noexcept
{
    glm::vec3 target = glm::vec3(0.0F, 0.0F, step);
    if constexpr (member == eye_member::eX)
        target = glm::vec3(step, 0.0F, 0.0F);
    else if constexpr (member == eye_member::eY)
        target = glm::vec3(0.0F, step, 0.0F);
    glm::mat4 target_step = glm::translate(glm::mat4(1.0F), target);
    glm::vec4 eye_vec4 = glm::vec4(camera.refViewsMatrix().eye, 1.0F); // w=1表示点
    eye_vec4 = target_step * eye_vec4;                 // 矩阵乘向量（正确的变换方式）
    camera.refViewsMatrix().eye = glm::vec3(eye_vec4); // 转回vec3

    for (int row = 0; row < 3; row++)
    {
        for (int col = 0; col < 3; col++)
        {
            std::print("Key::eW: [{:f},{:f},{:f}] ", camera.refViewsMatrix().eye.x,
                       camera.refViewsMatrix().eye.y, camera.refViewsMatrix().eye.z);
        }
        std::print("\n");
    }
};
// diff: [camera_view] end

// diff: [camera_ortho] start
enum class ortho_member : std::uint8_t
{
    eLEFT,
    eRIGHT,
    eBOTTOM,
    eTOP,
    eZ_NEAR,
    eZ_FAR
};
// NOTE: WS 依旧有效，超过+-10就完蛋. 区别是 Z的修改 没有近大远小，畸变严重
template <ortho_member member>
auto update_ortho(camera_interface &camera, float step) noexcept
{
    using enum ortho_member;
    if (member == eLEFT)
        camera.refOrthoMatrix().left += step;
    else if (member == eRIGHT)
        camera.refOrthoMatrix().right += step;
    else if (member == eBOTTOM)
        camera.refOrthoMatrix().bottom += step;
    else if (member == eTOP)
        camera.refOrthoMatrix().top += step;
    else if (member == eZ_NEAR)
        camera.refOrthoMatrix().zNear += step;
    else
        camera.refOrthoMatrix().zFar += step;

    auto [left, right, bottom, top, zNear, zFar] = camera.refOrthoMatrix();
    std::print("Key::eW: [left: {:f},right: {:f},bottom: {:f},top: "
               "{:f},zNear: {:f},zFar: {:f}]\n",
               left, right, bottom, top, zNear, zFar);
}
// diff: [camera_ortho] end

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
            {.features =
                 {// diff: [texture] 纹理硬件各向异性过滤
                  .samplerAnisotropy = VK_TRUE,
                  .shaderInt64 = VK_TRUE}}, // diff: shader 扩展这里也要打开
            {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
            {

                // diff: [texture] start 在Vulkan 1.2特性中设置描述符索引特性
                .descriptorIndexing = VK_TRUE, // 必须启用

                .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,

                // diff: [test_update_texture] start
                .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
                .descriptorBindingPartiallyBound = VK_TRUE,
                .descriptorBindingVariableDescriptorCount = VK_TRUE,
                .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
                // diff: [test_update_texture] end

                .runtimeDescriptorArray = VK_TRUE,
                // diff: [texture] end

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
                return features2.features
                           .samplerAnisotropy && // diff: [texture] 采样特性检查
                       features2.features.shaderInt64 &&
                       query_vulkan13_features.dynamicRendering &&
                       query_vulkan13_features.synchronization2 &&

                       query_vulkan12_features
                           .bufferDeviceAddress && // diff: [new]
                                                   // 检查Vulkan 1.2中的bufferDeviceAddress
                       query_vulkan12_features
                           .scalarBlockLayout && // diff: [new]
                                                 // 检查scalarBlockLayout
                       // diff: [texture] start 检查Vulkan 1.2中的描述符索引特性
                       query_vulkan12_features.runtimeDescriptorArray &&
                       // diff: [test_update_texture] start
                       // https://docs.vulkan.net.cn/guide/latest/extensions/VK_EXT_descriptor_indexing.html
                       query_vulkan12_features.descriptorBindingPartiallyBound &&
                       query_vulkan12_features.descriptorBindingVariableDescriptorCount &&
                       // diff: [test_update_texture] end
                       query_vulkan12_features.descriptorIndexing &&
                       query_vulkan12_features
                           .shaderSampledImageArrayNonUniformIndexing &&
                       // diff: [texture] end
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

    CommandPool commandPool =
        create_command_pool{}
            .setCreateInfo({.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                            .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX})
            .build(device);

    // diff: [Uniform] start
    // diff: [texture] start
    constexpr int MAX_TEXTURES = 64; // 预分配最大纹理槽位数（足够大）
    int CURRENT_TEXTURE_COUNT = 3;
    constexpr int SAMPLER_COUNT = 2; // 创建2个不同的采样器
    // diff: [texture] end

    // diff: [test_update_texture] start
    //  在启用对绑定后更新的所需功能支持后，应用程序需要设置以下内容才能使用可以在绑定后更新的描述符
    // 首先，定义每个绑定的标志
    // https://docs.vulkan.net.cn/guide/latest/extensions/VK_EXT_descriptor_indexing.html#:~:text=VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT
    std::vector<VkDescriptorBindingFlags> bindingFlags = {
        // 绑定0：Uniform Buffer - 通常不需要绑定后更新
        0,
        // 绑定1：Sampled Images - 需要绑定后更新
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        // 绑定2：Samplers - 采样器通常不需要频繁更新
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT};
    // NOTE: 这是支持 bind 和 draw之间多次更新的。可用多次bind 和 draw 看PPT。
    /*
pBindingFlags[1] (binding 1) includes VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
but can only be on the last binding element (binding 2).

原因：可变描述符计数标志只能设置在绑定号最大的那个绑定上。
你的绑定顺序是：
*/
    /*
你的所有 updateDescriptorSets 调用都在 drawFrame() 开头，在 vkQueueSubmit
之前，因此属于常规更新。 你根本不需要 UPDATE_AFTER_BIND。
*/
    // diff: [test_update_texture] end

    auto descriptorSetLayout =
        create_descriptor_set_layout{}
            .setCreateInfo(
                {// diff: [test_update_texture] start
                 .pNext = make_pNext(
                     structure_chain<VkDescriptorSetLayoutBindingFlagsCreateInfo>{
                         {.bindingCount = static_cast<uint32_t>(bindingFlags.size()),
                          .pBindingFlags = bindingFlags.data()}}),
                 .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                 // diff: [test_update_texture] end
                 .bindings =
                     {
                         {.binding = 0,
                          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                          .descriptorCount = 1,
                          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
                         // diff: [texture] start
                         // 纹理绑定
                         VkDescriptorSetLayoutBinding{
                             .binding = 1,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .descriptorCount =
                                 MAX_TEXTURES, // diff: [test_update_texture2]
                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                             .pImmutableSamplers = nullptr,
                         },
                         // 采样器绑定
                         VkDescriptorSetLayoutBinding{
                             .binding = 2,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                             .descriptorCount = SAMPLER_COUNT,
                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                             .pImmutableSamplers = nullptr,
                         }
                         // diff: [texture] end
                     }})
            .build(device);

    auto descriptorPool =
        create_descriptor_pool{}
            .setCreateInfo(
                {.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
                          // diff: [test_update_texture] start
                          VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                 // diff: [test_update_texture] end
                 .maxSets = MAX_FRAMES_IN_FLIGHT,
                 .poolSizes =
                     {VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                           .descriptorCount = MAX_FRAMES_IN_FLIGHT},
                      // diff: [texture] start
                      VkDescriptorPoolSize{
                          .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                          .descriptorCount = // diff: [test_update_texture2]
                          MAX_TEXTURES * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                      },
                      VkDescriptorPoolSize{
                          .type = VK_DESCRIPTOR_TYPE_SAMPLER,
                          // 每个描述符集需要 SAMPLER_COUNT 个采样器，
                          .descriptorCount = (SAMPLER_COUNT) *
                                             static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                          // diff: [texture] end
                      }}})
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

    // diff: [texture] start
    std::vector<TextureImage> textures;
    textures.reserve(CURRENT_TEXTURE_COUNT);
    // 第一个纹理：从文件加载
    const std::string TEXTURE_PATH = "textures/texture.jpg";
    textures.emplace_back(allocator, device, commandPool, GRAPHICS_AND_PRESENT,
                          TEXTURE_PATH, false, 0);
    // 第二个纹理：程序生成的红蓝棋盘
    textures.emplace_back(allocator, device, commandPool, GRAPHICS_AND_PRESENT, "", true,
                          1);
    // 第三个纹理：程序生成的绿黄渐变
    textures.emplace_back(allocator, device, commandPool, GRAPHICS_AND_PRESENT, "", true,
                          2);

    std::vector<TextureImage> textures_copy;
    textures_copy.reserve(CURRENT_TEXTURE_COUNT);
    textures_copy.emplace_back(allocator, device, commandPool, GRAPHICS_AND_PRESENT,
                               TEXTURE_PATH, false, 0);
    // 第二个纹理：程序生成的红蓝棋盘
    textures_copy.emplace_back(allocator, device, commandPool, GRAPHICS_AND_PRESENT, "",
                               true, 1);
    // 第三个纹理：程序生成的绿黄渐变
    textures_copy.emplace_back(allocator, device, commandPool, GRAPHICS_AND_PRESENT, "",
                               true, 2);

    std::vector<TextureSampler> samplers;
    samplers.reserve(SAMPLER_COUNT);
    samplers.emplace_back(device, 0); // 线性采样器
    samplers.emplace_back(device, 1); // 最近邻采样器
    // diff: [texture] end

    // diff: [test_update_texture] start
    std::array<std::vector<VkDescriptorImageInfo>, MAX_FRAMES_IN_FLIGHT>
        textureInfosPerFrame;
    std::array<std::vector<VkDescriptorImageInfo>, MAX_FRAMES_IN_FLIGHT>
        samplerInfosPerFrame;

    // 初始化每个帧的纹理和采样器信息
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        // 1. 预分配 MAX_TEXTURES 个槽位
        textureInfosPerFrame[i].resize(MAX_TEXTURES);
        // 2. 只初始化前 CURRENT_TEXTURE_COUNT 个有效纹理
        for (int j = 0; j < CURRENT_TEXTURE_COUNT; ++j)
        {
            textureInfosPerFrame[i][j] = {.sampler = nullptr,
                                          .imageView = *textures[j].imageView(),
                                          .imageLayout =
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        // 其余槽位保持默认零初始化，我们**绝不写入**它们

        samplerInfosPerFrame[i] =
            samplers | std::views::transform([](const auto &s) constexpr noexcept {
                return VkDescriptorImageInfo{.sampler = s.sampler(),
                                             .imageView = nullptr,
                                             .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
            }) |
            std::ranges::to<std::vector>();
    }
    // diff: [test_update_texture] end

    // diff: [test_update_texture] start
    auto updateDescriptorSets = [&](const LogicalDevice &device,
                                    VkDescriptorSet descriptorSet,
                                    VkDescriptorBufferInfo bufferInfo,
                                    std::vector<VkDescriptorImageInfo> &textureInfos,
                                    std::vector<VkDescriptorImageInfo> &samplerInfos) {
        std::array<VkWriteDescriptorSet, 3> writes = {
            VkWriteDescriptorSet{.sType = sType<VkWriteDescriptorSet>(),
                                 .dstSet = descriptorSet,
                                 .dstBinding = 0,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                 .pBufferInfo = &bufferInfo},
            VkWriteDescriptorSet{
                .sType = sType<VkWriteDescriptorSet>(),
                .dstSet = descriptorSet,
                .dstBinding = 1,
                .dstArrayElement = 0,
                // diff: [test_update_texture2] 核心. 必须是提供有效范围的
                .descriptorCount = static_cast<uint32_t>(CURRENT_TEXTURE_COUNT),
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = textureInfos.data()},
            VkWriteDescriptorSet{.sType = sType<VkWriteDescriptorSet>(),
                                 .dstSet = descriptorSet,
                                 .dstBinding = 2,
                                 .dstArrayElement = 0,
                                 .descriptorCount = SAMPLER_COUNT,
                                 .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                 .pImageInfo = samplerInfos.data()}};
        device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(),
                                    0, nullptr);
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = uniformBuffers[i].buffer(), .offset = 0, .range = BUFFER_SIZE};
        updateDescriptorSets(device, descriptorSets[i], bufferInfo,
                             textureInfosPerFrame[i], samplerInfosPerFrame[i]);
    }
    // diff: [test_update_texture] end

    // diff: [depth] start
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
                 .samples =
                     physical_device.getMaxUsableSampleCount(), // diff: update by [depth]
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
    // diff: [depth] end

    // diff: [msaa] start
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
                 // VMA 配置
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

    // diff: [msaa] end

    auto pipelineLayout =
        create_pipeline_layout{}
            .setCreateInfo( // diff: [Uniform] 将描述符集合布局设置到管线布局
                {.setLayouts = {*descriptorSetLayout},
                 // diff: [new] 布局定义推送常量范围
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
                      .pColorAttachmentFormats = &swapchainBuild.refImageFormat(),
                      .depthAttachmentFormat =
                          depthFormat_ref}}), // diff: [depth] 深度附件格式
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
                                        .cullMode =
                                            VK_CULL_MODE_NONE, // diff:[Uniform] 关键
                                        .frontFace = VK_FRONT_FACE_CLOCKWISE,
                                        // .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                        .depthBiasEnable = VK_FALSE,
                                        .lineWidth = 1.0F},
                 .multisampleState = // diff: [msaa] start 多重采样状态使用MSAA采样计数
                 {
                     .rasterizationSamples = physical_device.getMaxUsableSampleCount(),
                     .sampleShadingEnable = VK_FALSE,
                     .minSampleShading = 1.0F,
                     .sampleMask = {},
                     .alphaToCoverageEnable = VK_FALSE,
                     .alphaToOneEnable = VK_FALSE,
                 }, // diff: [msaa] end
                    // diff: [depth] start 添加深度测试和模板测试状态
                 .depthStencilState = {.depthTestEnable = VK_TRUE,
                                       .depthWriteEnable = VK_TRUE,
                                       .depthCompareOp = VK_COMPARE_OP_LESS,
                                       .depthBoundsTestEnable = VK_FALSE,
                                       .stencilTestEnable = VK_FALSE,
                                       .front = {},
                                       .back = {},
                                       .minDepthBounds = 0.0F,
                                       .maxDepthBounds = 1.0F}, // diff: [depth] end
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

    // diff: 准备好顶点和顶点索引--------------- // NOLINTBEGIN
    // 顶点数据（四边形由两个三角形组成）//diff: [texture] start. 设置UV坐标
    const std::vector<Vertex> vertices = {
        {{-0.5f, -0.5f, 0}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}}, // 左下
        {{0.5, -0.5, 0}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},    // 右下
        {{0.5, 0.5, 0}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},     // 右上
        {{-0.5, 0.5, 0}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F}}     // 左上
    }; // diff: [texture] end

    // 索引数据
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    mesh_base input_mesh{
        allocator, commandPool, GRAPHICS_AND_PRESENT, {vertices, indices}};
    mesh_base input_mesh2{allocator,
                          commandPool,
                          GRAPHICS_AND_PRESENT,
                          {vertices | std::views::transform([](const Vertex &vertex) {
                               auto newvertex = vertex;
                               newvertex.pos[2] = 0.5;
                               return newvertex;
                           }) | std::ranges::to<std::vector<Vertex>>(),
                           indices}};
    constexpr auto input_mesh_id = 0;
    constexpr auto input_mesh2_id = 1;

    // NOTE: 这才是数据来源。这是引用语义
    auto &indexs = input_mesh.queue_data.indices;
    auto &indexs2 = input_mesh2.queue_data.indices;

    // diff: end // NOLINTEND

    // diff: [texture] start
    // 随机数生成器，分别生成纹理索引和采样器索引
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> textureDis(0, CURRENT_TEXTURE_COUNT - 1);
    std::uniform_int_distribution<> samplerDis(0, SAMPLER_COUNT - 1);

    // 随机生成两个纹理索引和采样器索引
    uint32_t textureIndex1 = 0;
    uint32_t textureIndex2 = 1;
    uint32_t samplerIndex1 = 0;
    uint32_t samplerIndex2 = 1;
    //  diff: [texture] end

    struct record_info
    {
        uint32_t current_frame;
        uint32_t image_index;
        VkDescriptorSet descriptor_set;
    };

    // diff: [camera_view] start
    camera_interface camera =
        camera_interface{}
            .setViewsMatrix({
                .eye = glm::vec3(0.0F, 0.0F, 2.0F),
                .center = glm::vec3(0.0F, 0.0F, 0.0F),
                .up = glm::vec3(0.0F, 1.0F, 0.0F) // 修正：Y轴为上方向（符合常规3D场景）
            })                                    // diff: [camera_ortho] start
            .setOrthoMatrix({.left = -1.0F,
                             .right = 1.0F,
                             .bottom = -1.0F,
                             .top = 1.0F,
                             .zNear = 0.1F,
                             .zFar = 10.0F}); // diff: [camera_ortho] end

    glfw_input input{};
    auto views_matrix_update = [&](glfw_input &input) {
        using mcs::vulkan::event::Key;

        if (input.isKeyPressedOrRepeat(Key::eQ))
        {
            constexpr auto step = 00.1;
            update_eye<eye_member::eY>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eE))
        {
            constexpr auto step = -00.1;
            update_eye<eye_member::eY>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eA))
        {
            constexpr auto step = -00.1;
            update_eye<eye_member::eX>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eD))
        {
            constexpr auto step = +00.1;
            update_eye<eye_member::eX>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eW))
        {
            constexpr auto step = -00.1;
            update_eye<eye_member::eZ>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eS))
        {
            constexpr auto step = +00.1;
            update_eye<eye_member::eZ>(camera, step);
        }
    };
    auto views_ortho_update = [&](glfw_input &input) {
        using mcs::vulkan::event::Key;

        if (input.isKeyPressedOrRepeat(Key::eR))
        {
            constexpr auto step = 00.1;
            update_ortho<ortho_member::eLEFT>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eF))
        {
            constexpr auto step = -00.1;
            update_ortho<ortho_member::eLEFT>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eT))
        {
            constexpr auto step = 00.1;
            update_ortho<ortho_member::eRIGHT>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eG))
        {
            constexpr auto step = -00.1;
            update_ortho<ortho_member::eRIGHT>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eY))
        {
            constexpr auto step = 00.1;
            update_ortho<ortho_member::eBOTTOM>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eH))
        {
            constexpr auto step = -00.1;
            update_ortho<ortho_member::eBOTTOM>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eU))
        {
            constexpr auto step = 00.1;
            update_ortho<ortho_member::eTOP>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eJ))
        {
            constexpr auto step = -00.1;
            update_ortho<ortho_member::eTOP>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eI))
        {
            constexpr auto step = 00.1;
            update_ortho<ortho_member::eZ_NEAR>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eK))
        {
            constexpr auto step = -00.1;
            update_ortho<ortho_member::eZ_NEAR>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eO))
        {
            constexpr auto step = 00.1;
            update_ortho<ortho_member::eZ_FAR>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eL))
        {
            constexpr auto step = -00.1;
            update_ortho<ortho_member::eZ_FAR>(camera, step);
        }
    };
    // diff: [camera_view] end
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

        // Before starting rendering,
        // transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
        my_render::transition_image_layout(
            commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            {}, // srcAccessMask (no need to wait for previous operations)
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, // diff: update by [msaa]
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        // Transition the multisampled color image to COLOR_ATTACHMENT_OPTIMAL
        // diff: [msaa] start
        my_render::transition_image_layout(
            commandBuffer, msaaImage, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        // diff: [msaa] end

        // Transition depth image to depth attachment optimal layout
        // diff: [depth] start
        my_render::transition_image_layout(
            commandBuffer, depthImage, VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
        // diff: [depth] end

        // diff: update by [mass] start 修改颜色附件为MSAA颜色图像，添加解析附件
        VkRenderingAttachmentInfo colorAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = msaaImageView,
            .imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT, // diff: 添加解析模式
            .resolveImageView = imageView,              // diff: 解析到交换链图像
            .resolveImageLayout =
                VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, // diff:
                                                                         // 解析图像布局
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}}};
        // diff: update by [mass] end

        // diff: [depth] start: 添加深度附件
        VkRenderingAttachmentInfo depthAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = depthImageView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.depthStencil = {.depth = 1.0F}}};
        // diff: [depth] end

        commandBuffer.beginRendering({
            .sType = sType<VkRenderingInfo>(),
            .renderArea = {.offset = {.x = 0, .y = 0}, .extent = imageExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = &depthAttachment // diff: [depth] 附件绑定到渲染中
        });
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

        // diff: [test_model_matrix] start
        auto *uniformBuffers = uniformBuffersMapped[currentFrame];
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(
                         currentTime - startTime)
                         .count();

        auto mvp = [&]() {
            UniformBufferObject ubo{};

            auto swapChainExtent = swapchain.imageExtent();

            // diff: [test_views_matrix] start
            // diff: [camera_view] start
            ubo.view = camera.viewsMatrix();
            // diff: [camera_view] end
            // diff: [test_views_matrix] end

            // diff: [camera_ortho] start
            ubo.proj = camera.orthoMatrix();
            // diff: [camera_ortho] end

            // Vulkan的Y轴是向下的，需要翻转Y轴
            ubo.proj[1][1] *= -1;

            // 复制数据到Uniform Buffer
            memcpy(uniformBuffers, &ubo, sizeof(ubo));
        };

        mvp();
        // diff: [test_model_matrix] end

        {
            // diff: [Uniform] start
            commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             *pipelineLayout, 0, 1, &(descriptorSet), 0,
                                             nullptr);
            // diff: [Uniform] end

            // diff: [new] 即使使用设备地址，Vulkan仍需要绑定索引缓冲区.来变量顶点数组
            uint64_t vertexBufferDeviceAddress =
                input_mesh.getVertexBufferAddress(currentFrame);

            // diff: [test_model_matrix2] start
            uint64_t objectDataAddress = input_mesh.getObjectDataAddress(currentFrame);
            mesh_base::updateObjectData(input_mesh.frameBuffers[currentFrame], [&] {
                return glm::rotate(glm::mat4(1.0F), time * glm::radians(90.0F),
                                   glm::vec3(0.0F, 0.0F, 1.0F));
            });
            // diff: [test_model_matrix2] end
            PushConstants pushConstants = {.vertexBufferAddress =
                                               vertexBufferDeviceAddress,
                                           // diff: [Uniform] start
                                           .textureIndex = textureIndex1,
                                           .samplerIndex = samplerIndex1,
                                           .objectDataAddress = objectDataAddress};

            // NOTE: 当前仅仅推送顶点数据
            commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(PushConstants), &pushConstants);

            // diff: [test_model_matrix]

            // diff: [Uniform] end

            commandBuffer.bindIndexBuffer(
                input_mesh.frameBuffers[currentFrame].indexBuffer.buffer(), 0,
                mesh_base::indexType());

            commandBuffer.drawIndexed(static_cast<uint32_t>(indexs.size()), 1, 0, 0, 0);
            // diff: end
        }
        {
            // diff: [Uniform] start
            commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             *pipelineLayout, 0, 1, &(descriptorSet), 0,
                                             nullptr);
            // diff: [Uniform] end

            // diff: [new] 即使使用设备地址，Vulkan仍需要绑定索引缓冲区.来变量顶点数组
            uint64_t vertexBufferDeviceAddress =
                input_mesh2.getVertexBufferAddress(currentFrame);

            // diff: [test_model_matrix2] start
            uint64_t objectDataAddress = input_mesh2.getObjectDataAddress(currentFrame);
            // diff: [test_model_matrix3] start
            mesh_base::updateObjectData(input_mesh2.frameBuffers[currentFrame], [&] {
                return glm::rotate(glm::mat4(1.0F), time * glm::radians(45.0F),
                                   glm::vec3(0.0F, 0.0F, 1.0F));
            });
            // diff: [test_model_matrix3] end
            // diff: [test_model_matrix2] end

            PushConstants pushConstants = {
                .vertexBufferAddress = vertexBufferDeviceAddress,
                // diff: [Uniform] start
                .textureIndex = textureIndex2,
                .samplerIndex = samplerIndex2,
                .objectDataAddress = objectDataAddress,
                // diff: [Uniform] end
            };

            // NOTE: 当前仅仅推送顶点数据
            commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(PushConstants), &pushConstants);

            commandBuffer.bindIndexBuffer(
                input_mesh2.frameBuffers[currentFrame].indexBuffer.buffer(), 0,
                mesh_base::indexType());

            commandBuffer.drawIndexed(static_cast<uint32_t>(indexs2.size()), 1, 0, 0, 0);
            // diff: end
        }
        commandBuffer.endRendering();

        // After rendering, transition the swapchain image to PRESENT_SRC
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

        // diff: [msaa] start
        msaaResourcesBuild.updateImageExtent({.width = swapchain.refImageExtent().width,
                                              .height = swapchain.refImageExtent().height,
                                              .depth = 1});
        msaaResource = msaaResourcesBuild.rebuild();
        // diff: [msaa] end

        // diff: [depth] start
        depthResourcesBuild.updateImageExtent(
            {.width = swapchain.refImageExtent().width,
             .height = swapchain.refImageExtent().height,
             .depth = 1});
        depthResource = depthResourcesBuild.rebuild();
        // diff: [depth] end
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

        surface::pollEvents();
        // diff: [camera_view] start
        views_matrix_update(input);
        // diff: [camera_view] end

        // diff: [camera_ortho] start
        views_ortho_update(input);
        // diff: [camera_ortho] end

        // diff:     // 检查是否需要更新形状
        auto now = std::chrono::steady_clock::now();
        static auto lastUpdate = std::chrono::steady_clock::now();
        if (now - lastUpdate > std::chrono::seconds(2)) // NOLINT
        {
            lastUpdate = now;

            // diff: [test_update_texture] start
            // NOTE: BUGBUG.不能为true.
            constexpr bool ADD_TEST = true;
            if constexpr (not ADD_TEST)
            {
                static int textureUpdateCounter = 0;
                if (textureUpdateCounter < 3) // 只更新3次
                {
                    textureUpdateCounter++;

                    // 创建新纹理
                    static int textureTypeCounter = 2; // 从2开始（0-2）
                    textureTypeCounter =
                        (textureTypeCounter + 1) % 3; // 循环使用0,1,2的纹理类型

                    // 创建新纹理（使用textureTypeCounter作为类型）
                    textures[textureIndex1] =
                        TextureImage(allocator, device, commandPool, GRAPHICS_AND_PRESENT,
                                     "", true, textureTypeCounter);
                    auto textureIndexToUpdate = textureIndex1;
                    auto &newTexture = textures[textureIndexToUpdate];
                    // 更新所有帧的描述符集
                    for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
                    {
                        textureInfosPerFrame[frame][textureIndexToUpdate] = {
                            .sampler = nullptr,
                            .imageView = *newTexture.imageView(),
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

                        VkWriteDescriptorSet write = {
                            .sType = sType<VkWriteDescriptorSet>(),
                            .dstSet = descriptorSets[frame],
                            .dstBinding = 1,
                            .dstArrayElement = textureIndexToUpdate,
                            .descriptorCount = 1,
                            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                            .pImageInfo =
                                &textureInfosPerFrame[frame][textureIndexToUpdate]};
                        device.updateDescriptorSets(1, &write, 0, nullptr);
                    }
                }
            }
            else
            {
                static bool add = false;
                if (not add)
                {
                    textures.emplace_back(allocator, device, commandPool,
                                          GRAPHICS_AND_PRESENT,
                                          "textures/viking_room.png", false, 0);
                    add = true;
                    // NOTE: 看起来用户必须维护一个有效的范围。
                    //  diff: [test_update_texture2]  ← 必须加！当前纹理数量变成4。 没用到
                    CURRENT_TEXTURE_COUNT++;
                    // NOTE: 必须一开始创建描述符布局 就分配好全部最大范围，再也不能扩容了

                    // 2. 选择要更新的纹理数组索引（确保 < TEXTURE_COUNT）
                    const uint32_t textureIndexToUpdate = textures.size() - 1;
                    auto &newTexture = textures[textureIndexToUpdate];

                    // 3. 遍历所有飞行帧，更新每个描述符集中该索引的纹理信息
                    for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
                    {
                        textureInfosPerFrame[frame][textureIndexToUpdate] = {
                            .sampler = nullptr,
                            .imageView = *newTexture.imageView(),
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

                        VkWriteDescriptorSet write = {
                            .sType = sType<VkWriteDescriptorSet>(),
                            .dstSet = descriptorSets[frame], // NOTE: 描述符集
                            .dstBinding = 1,
                            //  diff: [test_update_texture2] 是允许更新单个描述符的
                            .dstArrayElement =
                                textureIndexToUpdate, // NOTE:dstArrayElement
                                                      // 就一个更新是没毛病的
                            .descriptorCount = 1,
                            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                            .pImageInfo =
                                &textureInfosPerFrame[frame][textureIndexToUpdate]};
                        device.updateDescriptorSets(1, &write, 0, nullptr);
                    }
                    //  diff: [test_update_texture2] 指向新增slot没报错
                    textureIndex1 = textureIndexToUpdate;
                }
            }

            // diff: [test_update_texture] end

            static bool isTriangle = true;
            if (isTriangle)
            {
                // 只记录要更新的数据，不立即执行
                // 切换到三角形 // NOLINTBEGIN //diff: [texture] start. 设置UV坐标
                static const std::vector<Vertex> VERTICES = {
                    // 左下角 - 红色
                    {.pos = {-0.5f, -0.5f, 0.7f},
                     .color = {1.0F, 0.0F, 0.0F},
                     .texCoord = {0.0F, 0.0F}}, // UV: (0,0)

                    // 右下角 - 绿色
                    {.pos = {0.5f, -0.5f, 0.7f},
                     .color = {0.0F, 1.0F, 0.0F},
                     .texCoord = {1.0F, 0.0F}}, // UV: (1,0)

                    // 顶部中间 - 蓝色
                    {.pos = {0.0F, 0.5f, 0.7f},
                     .color = {0.0F, 0.0F, 1.0F},
                     .texCoord = {0.5f, 1.0F}} // UV: (0.5,1)
                }; // diff: [texture] end
                static const std::vector<uint32_t> INDICES = {0, 1, 2}; // NOLINTEND
                input_mesh.queueUpdate(VERTICES, INDICES);
            }
            else
            {
                input_mesh.queueUpdate(vertices, indices);
            }
            isTriangle = !isTriangle;
        }
        drawFrame();
    }
    device.waitIdle();
    // diff: [Uniform] start
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (uniformBuffersMapped[i] != nullptr)
        {
            uniformBuffers[i].unmap();
            uniformBuffersMapped[i] = nullptr;
        }
    } // diff: [Uniform] end

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}