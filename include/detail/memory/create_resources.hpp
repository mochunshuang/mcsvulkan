#pragma once

#include "create_image.hpp"
#include "resource.hpp"
#include <utility>

namespace mcs::vulkan::memory
{
    struct create_resources : create_image
    {
        using create_image::create_image;
        [[nodiscard]] constexpr resource build(const LogicalDevice &device,
                                               VkDeviceSize memoryOffset = 0) const
        {
            image_base base = create_image::build(device, memoryOffset);
            VkImageView imageView = create_image::buildRawView(device, base.image());
            return {std::move(base), imageView};
        }
    };
}; // namespace mcs::vulkan::memory