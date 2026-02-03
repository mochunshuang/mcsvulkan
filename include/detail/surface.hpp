#pragma once

#include "__vulkan.hpp"
#include <concepts>
#include <vector>

namespace mcs::vulkan
{
    template <typename S>
    concept surface = requires(const S &s, const VkSurfaceCapabilitiesKHR &capabilities) {
        { s.surface() } noexcept -> std::same_as<VkSurfaceKHR>;
        { s.surfaceCapabilities() } -> std::same_as<VkSurfaceCapabilitiesKHR>;
        { s.surfaceFormats() } -> std::same_as<std::vector<VkSurfaceFormatKHR>>;
        { s.surfacePresentModes() } -> std::same_as<std::vector<VkPresentModeKHR>>;
        { s.chooseSwapExtent(capabilities) } -> std::same_as<VkExtent2D>;
        { s.waitGoodFramebufferSize() } -> std::same_as<void>;
    };

}; // namespace mcs::vulkan