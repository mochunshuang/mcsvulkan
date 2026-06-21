#pragma once
#include "create_buffer.hpp"
#include "auto_map_buffer.hpp"
#include "gen_memory_allocate_info.hpp"
#include "find_memory_type_index.hpp"

namespace mcs::vulkan::memory
{
    static constexpr auto_map_buffer create_staging_buffer(
        const LogicalDevice &device,
        VkDeviceSize buffer_size,   // NOLINT
        VkMemoryMapFlags flags = 0) // NOLINT
    {
        return auto_map_buffer{
            create_buffer{{.size = buffer_size,
                           .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                          gen_memory_allocate_info(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)}
                .build(device),
            buffer_size, flags};
    }
}; // namespace mcs::vulkan::memory