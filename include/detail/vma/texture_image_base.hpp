#pragma once

#include "vma_image.hpp"
#include "../ImageView.hpp"
#include <utility>

namespace mcs::vulkan::vma
{
    struct texture_image_base
    {
        constexpr texture_image_base(vma_image textureImage, ImageView imageView) noexcept
            : textureImage_{std::move(textureImage)}, imageView_{std::move(imageView)}
        {
        }
        [[nodiscard]] auto &imageView() const noexcept
        {
            return imageView_;
        }
        [[nodiscard]] auto &image() const noexcept
        {
            return textureImage_;
        }

      private:
        vma_image textureImage_;
        ImageView imageView_;
    };

}; // namespace mcs::vulkan::vma