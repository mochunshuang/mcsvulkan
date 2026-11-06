#pragma once
#include "buffer_resource.hpp"
#include "../tool/sType.hpp"

namespace mcs::vulkan::memory
{
    struct buffer_resource_with_address : buffer_resource
    {
        VkDeviceAddress address{};

        buffer_resource_with_address() = default;
        constexpr buffer_resource_with_address(const LogicalDevice &device,
                                               VkDeviceSize capacity,
                                               VkBufferUsageFlags usage,
                                               VkSharingMode sharingMode,
                                               VkMemoryPropertyFlags properties)
            : buffer_resource(device, capacity, usage, sharingMode, properties),
              address{device.getBufferDeviceAddress(
                  {.sType = tool::sType<VkBufferDeviceAddressInfo>(),
                   .buffer = buffer.buffer()})}
        {
        }
    };

}; // namespace mcs::vulkan::memory