#pragma once

#include "memory_allocate_info.hpp"
#include "find_memory_type_index.hpp"

namespace mcs::vulkan::memory
{

    static constexpr auto gen_memory_allocate_info(VkMemoryPropertyFlags properties)
    {
        return [=](VkMemoryRequirements memRequirements,
                   VkPhysicalDeviceMemoryProperties memoryProperties)
                   -> memory_allocate_info {
            return {.pNext = {},
                    .allocationSize = memRequirements.size,
                    .memoryTypeIndex = find_memory_type_index(
                        memRequirements.memoryTypeBits, memoryProperties, properties)};
        };
    }

}; // namespace mcs::vulkan::memory