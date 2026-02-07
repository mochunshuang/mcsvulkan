#pragma once

#include "create_buffer.hpp"
#include "../tool/sType.hpp"

namespace mcs::vulkan::vma
{
    static constexpr auto staging_buffer(VmaAllocator allocator, size_t buffer_size,
                                         bool persistent_map = false)
    {
        VmaAllocationCreateInfo allocCreateInfo = {
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO};
        // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html#:~:text=Persistently%20mapped%20memory
        if (persistent_map)
            allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;

        using tool::sType;
        return create_buffer(allocator,
                             {.sType = sType<VkBufferCreateInfo>(),
                              .size = buffer_size,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                             allocCreateInfo, nullptr);
    }
}; // namespace mcs::vulkan::vma