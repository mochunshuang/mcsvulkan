#pragma once

#include "../LogicalDevice.hpp"
#include "vma_image.hpp"
#include "../ImageView.hpp"
#include <utility>

namespace mcs::vulkan::vma
{
    struct resource
    {
        resource() = default;
        constexpr resource(const LogicalDevice *device, VkImage image,
                           VmaAllocator allocator, VmaAllocation allocation,
                           VkImageView imageView) noexcept
            : vmaImage_{image, allocator, allocation}, imageView_{*device, imageView}
        {
        }
        constexpr resource(vma_image vmaImage, ImageView imageView) noexcept
            : vmaImage_{std::move(vmaImage)}, imageView_{std::move(imageView)}
        {
        }
        constexpr void destroy() noexcept
        {
            vmaImage_.destroy();
            imageView_.destroy();
        }

        [[nodiscard]] VkImage image() const noexcept
        {
            return *vmaImage_;
        }
        [[nodiscard]] VkImageView imageView() const noexcept
        {
            return *imageView_;
        }

      private:
        vma_image vmaImage_;
        ImageView imageView_;
    };
}; // namespace mcs::vulkan::vma