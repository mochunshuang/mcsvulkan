#pragma once

#include "../LogicalDevice.hpp"
#include <cassert>

namespace mcs::vulkan::memory
{
    [[nodiscard]] static constexpr auto find_memory_type_index(
        uint32_t typeFilter, // NOLINTNEXTLINE
        VkPhysicalDeviceMemoryProperties memProperties, VkMemoryPropertyFlags properties)
    {
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if (((typeFilter & (1 << i)) != 0U) && // NOLINTNEXTLINE
                ((memProperties.memoryTypes[i].propertyFlags) & properties) == properties)
                return i;
        }
        throw std::runtime_error("failed to find suitable memory type!");
    }
}; // namespace mcs::vulkan::memory