#pragma once
#include "create_buffer.hpp"
#include "auto_map_buffer.hpp"

namespace mcs::vulkan::memory
{
    static constexpr auto_map_buffer create_staging_buffer(
        LogicalDevice &device,
        VkDeviceSize buffer_size,   // NOLINT
        VkMemoryMapFlags flags = 0) // NOLINT
    {
        using memory_allocate_info = create_buffer::memory_allocate_info;
        return auto_map_buffer{
            create_buffer{}
                .setCreateInfo({.size = buffer_size,
                                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                .sharingMode = VK_SHARING_MODE_EXCLUSIVE})
                .setGenMemoryAllocateInfo(
                    [](VkMemoryRequirements memRequirements,
                       VkPhysicalDeviceMemoryProperties memoryProperties)
                        -> memory_allocate_info {
                        return {.pNext = {},
                                .allocationSize = memRequirements.size,
                                .memoryTypeIndex = create_buffer::findMemoryTypeIndex(
                                    memRequirements.memoryTypeBits, memoryProperties,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
                    })
                .build(device),
            buffer_size, flags};
    }
}; // namespace mcs::vulkan::memory