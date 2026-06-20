#pragma once

#include "../LogicalDevice.hpp"

namespace mcs::vulkan::memory
{
    /*
        typedef struct VkMemoryAllocateInfo {
            VkStructureType    sType;
            const void*        pNext;
            VkDeviceSize       allocationSize;
            uint32_t           memoryTypeIndex;
        } VkMemoryAllocateInfo;
        */
    struct memory_allocate_info // NOLINTBEGIN
    {
        constexpr VkMemoryAllocateInfo operator()() const noexcept
        {
            using tool::sType;
            return {.sType = sType<VkMemoryAllocateInfo>(),
                    .pNext = pNext.value(),
                    .allocationSize = allocationSize,
                    .memoryTypeIndex = memoryTypeIndex};
        }
        tool::pNext pNext;
        VkDeviceSize allocationSize;
        uint32_t memoryTypeIndex;
    }; // NOLINTEND
    static_assert(std::is_default_constructible_v<memory_allocate_info>);

}; // namespace mcs::vulkan::memory