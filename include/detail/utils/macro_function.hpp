#pragma once

#include "../__vulkan.hpp"

namespace mcs::vulkan
{
    // NOLINTBEGIN
    consteval uint32_t vkApiVersion(uint32_t variant, uint32_t major, uint32_t minor,
                                    uint32_t patch)
    {
        return VK_MAKE_API_VERSION(variant, major, minor, patch); // NOLINT
    }
    consteval uint32_t vkApiVersion(uint32_t major, uint32_t minor, uint32_t patch)
    {
        return vkApiVersion(0, major, minor, patch); // NOLINT
    }
    consteval uint32_t vkMakeVersion(uint32_t major, uint32_t minor, uint32_t patch)
    {
        return VK_MAKE_VERSION(major, minor, patch); // Vulkan标准宏
    }
    // NOLINTEND
}; // namespace mcs::vulkan