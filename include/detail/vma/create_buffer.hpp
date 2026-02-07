#pragma once

#include "buffer_base.hpp"
#include "../utils/check_vkresult.hpp"

namespace mcs::vulkan::vma
{
    [[nodiscard]] static constexpr buffer_base create_buffer(
        VmaAllocator allocator, const VkBufferCreateInfo &bufferInfo,
        const VmaAllocationCreateInfo &allocCreateInfo,
        VmaAllocationInfo *pAllocInfo = nullptr)
    {
        VkBuffer buffer;          // NOLINT
        VmaAllocation allocation; // NOLINT
        check_vkresult(::vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo,
                                         &buffer, &allocation, pAllocInfo));
        return {buffer, allocator, allocation};
    }
}; // namespace mcs::vulkan::vma