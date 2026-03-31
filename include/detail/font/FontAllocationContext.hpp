#pragma once

#include "../Queue.hpp"
#include "../CommandPool.hpp"

namespace mcs::vulkan::font
{
    class FontAllocationContext
    {
      public:
        VmaAllocator allocator;
        const LogicalDevice *device;
        const CommandPool *pool;
        const Queue *queue;
    };
}; // namespace mcs::vulkan::font