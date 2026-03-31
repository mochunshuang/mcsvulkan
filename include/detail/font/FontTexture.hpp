#pragma once

#include "../vma/texture_image_base.hpp"
#include "../vma/create_texture_image.hpp"
#include "../vma/create_image.hpp"
#include "../load/raw_stbi_image.hpp"

#include "FontAllocationContext.hpp"

namespace mcs::vulkan::font
{
    class FontTexture
    {
        using raw_stbi_image = load::raw_stbi_image;
        using texture_image_base = vma::texture_image_base;
        using create_image = vma::create_image;
        using create_texture_image = vma::create_texture_image;
        texture_image_base texture_;

      public:
        struct msdf_info // NOLINTBEGIN
        {
            raw_stbi_image image;
            FontAllocationContext allocation;
        }; // NOLINTEND
        explicit FontTexture(const msdf_info &info)
        {
            const auto &[raw_data, allocation] = info;
            const auto [allocator, device, pool, queue] = allocation;
            auto width = raw_data.width();
            auto height = raw_data.height();
            uint32_t mipLevels = 1; // 字体无需 mipmap
            auto create_texture =
                create_texture_image{allocator, *pool, *queue}.setEnableMipmapping(false);
            create_image build =
                create_image{*device, allocator}
                    .setCreateInfo(
                        {.imageType = VK_IMAGE_TYPE_2D,
                         .format = VK_FORMAT_R8G8B8A8_UNORM,
                         .extent = {static_cast<uint32_t>(width),
                                    static_cast<uint32_t>(height), 1},
                         .mipLevels = mipLevels,
                         .arrayLayers = 1,
                         .samples = VK_SAMPLE_COUNT_1_BIT,
                         .tiling = VK_IMAGE_TILING_OPTIMAL,
                         .usage =
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
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
            texture_ = create_texture.build(
                build, std::span<const unsigned char>{raw_data.data(), raw_data.size()});
        }
        [[nodiscard]] const texture_image_base &view() const noexcept
        {
            return texture_;
        }
    };
}; // namespace mcs::vulkan::font