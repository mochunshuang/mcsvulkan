#pragma once

#include "../tool/sType.hpp"
#include "../Queue.hpp"
#include "../Fence.hpp"

#include "../CommandBuffer.hpp"
#include "../CommandPool.hpp"

#include "../utils/get_mip_levels.hpp"

#include <span>
#include <utility>

#include "staging_buffer.hpp"
#include "texture_image_base.hpp"
#include "create_image.hpp"

namespace mcs::vulkan::vma
{
    struct create_texture_image
    {
        using create_info = create_image::create_info;
        using view_create_info = create_image::view_create_info;

        template <typename T>
        static uint32_t mipLevels(T width, T height) noexcept
        {
            return get_mip_levels(width, height);
        }
        create_texture_image(VmaAllocator allocator, const CommandPool &pool,
                             const Queue &queue) noexcept
            : allocator_{allocator}, pool_{&pool}, queue_{&queue}
        {
        }

        texture_image_base build(create_image &build,
                                 const std::span<const unsigned char> &pixels) const
        {
            const auto &pool = *pool_;
            const auto &queue = *queue_;
            auto *allocator = allocator_;

            auto textureImage_ = build.makeImage();
            const auto MIP_LEVELS = build.createInfo().mipLevels;
            const auto [width, height] = build.extent2D();

            transitionImageLayout(
                pool, queue, *textureImage_, VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED,
                VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, MIP_LEVELS);

            // init image data
            {
                const void *data = pixels.data();
                const VkDeviceSize IMAGE_SIZE = pixels.size();
                auto stagingBuffer = staging_buffer(allocator, IMAGE_SIZE);
                stagingBuffer.copyDataToBuffer(data, IMAGE_SIZE);
                copyBufferToImage(pool, queue, *stagingBuffer, *textureImage_,
                                  VkExtent2D{.width = width, .height = height});
            }

            if (enableMipmapping())
                generateMipmaps(pool, queue, *textureImage_, build.refImageFormat(),
                                mipmap_param{.tex_width = static_cast<int32_t>(width),
                                             .tex_height = static_cast<int32_t>(height),
                                             .mip_levels = MIP_LEVELS});
            else
                transitionImageLayout(
                    pool, queue, *textureImage_,
                    VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, MIP_LEVELS);
            auto textureImageView = build.makeImageView(*textureImage_);
            return {std::move(textureImage_), std::move(textureImageView)};
        }

        [[nodiscard]] constexpr bool enableMipmapping() const noexcept
        {
            return enableMipmapping_;
        }
        constexpr auto &&setEnableMipmapping(bool enableMipmapping) noexcept
        {
            enableMipmapping_ = enableMipmapping;
            return std::move(*this);
        }

      private:
        bool enableMipmapping_{};
        VmaAllocator allocator_;
        const CommandPool *pool_;
        const Queue *queue_;

        static CommandBuffer beginSingleTimeCommand(const CommandPool &commandpool)
        {
            using tool::sType;
            CommandBuffer commandCopyBuffer = commandpool.allocateOneCommandBuffer(
                {.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY});

            commandCopyBuffer.begin(
                {.sType = sType<VkCommandBufferBeginInfo>(),
                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
            return commandCopyBuffer;
        }
        static void endSingleTimeCommand(const CommandPool &commandpool,
                                         const Queue &queue, CommandBuffer &commandBuffer)
        {
            using tool::sType;
            commandBuffer.end();

            const auto *logicalDevice = commandpool.device();

            Fence fence{*logicalDevice,
                        {.sType = sType<VkFenceCreateInfo>(), .flags = 0}};
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
            // NOTE: 4.
            // 执行布局转换的最常见方法之一是使用映像内存屏障。设置不同掩码来优化速度
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
                .imageSubresource = {VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                     0, 1},
                .imageOffset = {0, 0, 0},
                .imageExtent = {extent.width, extent.height, 1}};
            commandBuffer.copyBufferToImage(
                buffer, image, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                std::array<VkBufferImageCopy, 1>{region});
            endSingleTimeCommand(commandpool, queue, commandBuffer);
        }

        struct mipmap_param
        {
            int32_t tex_width, tex_height;
            uint32_t mip_levels;
        };

        // 告诉采样器如何采样：通过绑定或提交命令
        static void generateMipmaps(const CommandPool &commandpool, const Queue &queue,
                                    VkImage &image, VkFormat imageFormat,
                                    mipmap_param param)
        {
            using tool::sType;
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
                    VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    std::span{&blit, 1}, VK_FILTER_LINEAR);

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

            endSingleTimeCommand(commandpool, queue, commandBuffer);
            // NOTE: 应该注意的是，在运行时生成mipmap级别在实践中并不常见。
            //  通常它们是预先生成的，并与基本级别一起存储在纹理文件中，以提高加载速度

            // NOTE:总之，每个mip级别都要像加载原始图像一样加载到图像中。
        }
    };
}; // namespace mcs::vulkan::vma