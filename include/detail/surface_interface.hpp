#pragma once

#include "__vulkan.hpp"

namespace mcs::vulkan
{
    struct surface_interface
    {
        surface_interface() = default;
        surface_interface(const surface_interface &) = default;
        surface_interface(surface_interface &&) = default;
        surface_interface &operator=(const surface_interface &) = default;
        surface_interface &operator=(surface_interface &&) = default;
        constexpr virtual ~surface_interface() = default;

        [[nodiscard]] constexpr virtual VkSurfaceKHR surface() const noexcept = 0;
        constexpr virtual void destroy() = 0;
        // impl
        [[nodiscard]] constexpr virtual VkExtent2D chooseSwapExtent(
            const VkSurfaceCapabilitiesKHR & /*capabilities*/) const noexcept = 0;
        constexpr virtual void waitGoodFramebufferSize() const noexcept = 0;
    };

}; // namespace mcs::vulkan
