#pragma once

#include "create_image.hpp"
#include "resource.hpp"
#include "../CommandBuffer.hpp"
#include "../Queue.hpp"
#include "create_staging_buffer.hpp"
#include "../load/raw_stbi_image.hpp"

namespace mcs::vulkan::memory
{
    struct create_texture
    {
        using create_info = create_image::create_info;
        using view_create_info = create_image::view_create_info;

        using memory_allocate_info = memory_allocate_info;
        using GenMemoryAllocateInfo = std::move_only_function<memory_allocate_info(
            VkMemoryRequirements memRequirements,
            VkPhysicalDeviceMemoryProperties memoryProperties) const>;
        constexpr auto &&setGenMemoryAllocateInfo(
            this auto &&self, GenMemoryAllocateInfo genMemoryAllocateInfo) noexcept
        {
            self.genMemoryAllocateInfo_ = std::move(genMemoryAllocateInfo);
            return std::forward<decltype(self)>(self);
        }

        using GenViewCreateInfo = std::move_only_function<view_create_info(
            VkImageCreateInfo imageCreateInfo, VkImage image) const>;
        constexpr auto &&setGenViewCreateInfo(
            this auto &&self, GenViewCreateInfo genViewCreateInfo) noexcept
        {
            self.genViewCreateInfo_ = std::move(genViewCreateInfo);
            return std::forward<decltype(self)>(self);
        }
        struct image_info
        {
            VkExtent2D extent;
            std::span<const uint8_t> pixels;
            uint32_t mipLevels = 1; // NOLINT
        };
        using GenCreateInfo =
            std::move_only_function<create_info(image_info imageInfo) const>;
        constexpr auto &&setGenCreateInfo(this auto &&self,
                                          GenCreateInfo genCreateInfo) noexcept
        {
            self.genCreateInfo_ = std::move(genCreateInfo);
            return std::forward<decltype(self)>(self);
        }

        constexpr static CommandBuffer beginSingleTimeCommand(
            const CommandPool &commandpool)
        {
            using tool::sType;
            CommandBuffer commandCopyBuffer = commandpool.allocateOneCommandBuffer(
                {.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY});

            commandCopyBuffer.begin(
                {.sType = sType<VkCommandBufferBeginInfo>(),
                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
            return commandCopyBuffer;
        }
        constexpr static void endSingleTimeCommand(const LogicalDevice &logicalDevice,
                                                   const Queue &queue,
                                                   const CommandBuffer &commandBuffer)
        {
            using tool::sType;
            commandBuffer.end();

            Fence fence{logicalDevice, {.sType = sType<VkFenceCreateInfo>(), .flags = 0}};
            queue.submit(1,
                         {.sType = sType<VkSubmitInfo>(),
                          .commandBufferCount = 1,
                          .pCommandBuffers = &*commandBuffer},
                         *fence);
            mcs::vulkan::check_vkresult(
                logicalDevice.waitForFences(1, *fence, VK_TRUE, UINT64_MAX));
        }

        constexpr static void transitionImageLayout(const LogicalDevice &device,
                                                    const CommandPool &pool,
                                                    const Queue &queue, VkImage image,
                                                    VkImageLayout oldLayout,
                                                    VkImageLayout newLayout,
                                                    uint32_t mipLevels)
        {
            using tool::sType;
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
                destinationStage =
                    VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT;
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
            endSingleTimeCommand(device, queue, commandBuffer);
        }

        constexpr static void copyBufferToImage(const LogicalDevice &device,
                                                const CommandPool &commandpool,
                                                const Queue &queue,
                                                const VkBuffer &buffer, VkImage image,
                                                VkExtent2D extent)
        {
            auto commandBuffer = beginSingleTimeCommand(commandpool);
            commandBuffer.copyBufferToImage(
                buffer, image, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                std::array<VkBufferImageCopy, 1>{VkBufferImageCopy{
                    .bufferOffset = 0,
                    .bufferRowLength = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource =
                        {.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
                         .mipLevel = 0,
                         .baseArrayLayer = 0,
                         .layerCount = 1},
                    .imageOffset = {.x = 0, .y = 0, .z = 0},
                    .imageExtent = {
                        .width = extent.width, .height = extent.height, .depth = 1}}});
            endSingleTimeCommand(device, queue, commandBuffer);
        }

        template <typename T>
        constexpr static auto getMipLevels(T w, T h) noexcept // NOLINT
        {
            return static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
        }

        struct mipmap_param
        {
            int32_t width, height;
            uint32_t mip_levels;
        };

        // 告诉采样器如何采样：通过绑定或提交命令
        constexpr static void generateMipmaps(const LogicalDevice &logicalDevice,
                                              const CommandPool &commandpool,
                                              const Queue &queue, VkImage image,
                                              VkFormat imageFormat, mipmap_param param)
        {
            using tool::sType;
            auto [texWidth, texHeight, mipLevels] = param;

            /*
        这样的内置函数生成所有mip级别非常方便，
        但遗憾的是不能保证所有平台都支持，它需要我们使用的纹理图像格式来支持线性过滤
        */
            // NOTE: 4. 要求线性滤波
            //  Check if image format supports linear blit-ing
            if (VkFormatProperties formatProperties =
                    logicalDevice.physicalDevice()->getFormatProperties(imageFormat);
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
                barrier.newLayout =
                    VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_SHADER_READ_BIT;

                commandBuffer.pipelineBarrier(
                    VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VkPipelineStageFlagBits::VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, {},
                    {}, std::span{&barrier, 1});

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

            endSingleTimeCommand(logicalDevice, queue, commandBuffer);
            // NOTE: 应该注意的是，在运行时生成mipmap级别在实践中并不常见。
            //  通常它们是预先生成的，并与基本级别一起存储在纹理文件中，以提高加载速度

            // NOTE:总之，每个mip级别都要像加载原始图像一样加载到图像中。
        }

        [[nodiscard]] constexpr image_base buildImage(const LogicalDevice &device,
                                                      VkImageCreateInfo createInfo,
                                                      VkDeviceSize memoryOffset = 0) const
        {
            VkImage image = nullptr;
            VkDeviceMemory imageMemory = nullptr;
            try
            {
                image = device.createImage(createInfo, device.allocator());
                memory_allocate_info genAllocateInfo = genMemoryAllocateInfo_(
                    device.getImageMemoryRequirements(image),
                    device.physicalDevice()->getMemoryProperties());
                imageMemory =
                    device.allocateMemory(genAllocateInfo(), device.allocator());

                device.bindImageMemory(image, imageMemory, memoryOffset);
                return {device, image, imageMemory};
            }
            catch (...)
            {
                if (imageMemory != nullptr)
                    device.freeMemory(imageMemory, device.allocator());
                if (image != nullptr)
                    device.destroyImage(image, device.allocator());
                throw;
            }
        }
        constexpr VkImageView buildRawView(const LogicalDevice &device,
                                           VkImageCreateInfo createInfo,
                                           VkImage image) const
        {
            view_create_info createViewInfo = genViewCreateInfo_(createInfo, image);
            return device.createImageView(createViewInfo(), device.allocator());
        }
        constexpr ImageView buildImageView(const LogicalDevice &device,
                                           VkImageCreateInfo createInfo,
                                           VkImage image) const
        {
            return {device, buildRawView(device, createInfo, image)};
        }

        [[nodiscard]] constexpr resource build(const LogicalDevice &device,
                                               const CommandPool &pool,
                                               const Queue &queue, image_info imageInfo,
                                               VkDeviceSize memoryOffset = 0) const
        {

            create_info createInfo = genCreateInfo_(imageInfo);
            VkImageCreateInfo createImageInfo = createInfo();

            auto &pixels = imageInfo.pixels;
            auto [width, height, depth [[maybe_unused]]] = createImageInfo.extent;
            auto mipLevels = createImageInfo.mipLevels;
            auto format = createImageInfo.format;

            auto stagingBuffer = create_staging_buffer(device, pixels.size());
            ::memcpy(stagingBuffer.mapPtr(), pixels.data(), pixels.size());

            auto textureImage = buildImage(device, createImageInfo, memoryOffset);
            transitionImageLayout(device, pool, queue, textureImage.image(),
                                  VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED,
                                  VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  mipLevels);
            copyBufferToImage(device, pool, queue, stagingBuffer.buffer(),
                              textureImage.image(),
                              VkExtent2D{.width = width, .height = height});
            if (mipLevels == 1)
                transitionImageLayout(
                    device, pool, queue, textureImage.image(),
                    VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels);
            else
                generateMipmaps(device, pool, queue, textureImage.image(), format,
                                mipmap_param{.width = static_cast<int32_t>(width),
                                             .height = static_cast<int32_t>(height),
                                             .mip_levels = mipLevels});

            VkImageView textureImageView =
                buildRawView(device, createImageInfo, textureImage.image());
            return {std::move(textureImage), textureImageView};
        }

        [[nodiscard]] auto templateForImage2d(const LogicalDevice &device,
                                              const CommandPool &pool, const Queue &queue,
                                              load::raw_stbi_image img,
                                              bool mipmap = false,
                                              VkDeviceSize memoryOffset = 0) const
        {
            return build(
                device, pool, queue,
                image_info{.extent = {.width = static_cast<uint32_t>(img.width()),
                                      .height = static_cast<uint32_t>(img.height())},
                           .pixels = std::span<const uint8_t>{img.data(), img.size()},
                           .mipLevels =
                               mipmap ? getMipLevels(img.width(), img.height()) : 1},
                memoryOffset);
        }
        create_texture() = default;

        constexpr create_texture(GenCreateInfo genCreateInfo,
                                 GenMemoryAllocateInfo genMemoryAllocateInfo,
                                 GenViewCreateInfo genViewCreateInfo) noexcept
            : genCreateInfo_{std::move(genCreateInfo)},
              genMemoryAllocateInfo_{std::move(genMemoryAllocateInfo)},
              genViewCreateInfo_{std::move(genViewCreateInfo)}
        {
        }

      private:
        GenCreateInfo genCreateInfo_;
        GenMemoryAllocateInfo genMemoryAllocateInfo_;
        GenViewCreateInfo genViewCreateInfo_;
    };
}; // namespace mcs::vulkan::memory