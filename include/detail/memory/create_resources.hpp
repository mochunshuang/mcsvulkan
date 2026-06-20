#pragma once

#include "create_image.hpp"
#include "resource.hpp"
#include <utility>

namespace mcs::vulkan::memory
{
    struct create_resources : create_image
    {
        constexpr resource build(LogicalDevice &device, VkDeviceSize memoryOffset = 0)
        {
            image_base base = create_image::build(device, memoryOffset);
            VkImageView imageView = create_image::buildRawView(device, base.image());
            return {std::move(base), imageView};
        }

        constexpr create_resources() noexcept = default;
        constexpr explicit create_resources(create_image createImage) noexcept
            : create_image{std::move(createImage)}
        {
        }
        constexpr auto &&setCreateImage(this auto &&self,
                                        create_image createImage) noexcept
        {
            self = std::move(createImage);
            return std::forward<decltype(self)>(self);
        }
    };
}; // namespace mcs::vulkan::memory