#pragma once

#include "../head.hpp"

namespace shader_data
{
    using mcs::vulkan::memory::buffer_resource;
    using mcs::vulkan::memory::buffer_resource_with_address;
    using mcs::vulkan::LogicalDevice;

    // shader 需要的数据
    struct ShaderBuffers
    {
        buffer_resource_with_address globalVertexBuffer{};
        buffer_resource globalIndexBuffer{};
        buffer_resource_with_address globalHeapBuffer{};
        buffer_resource indirectDrawBuffer{};
        buffer_resource_with_address commandConstantsBuffer{};

        ShaderBuffers() = default;
        constexpr ShaderBuffers(const LogicalDevice &device, VkDeviceSize vertexCapacity,
                                VkDeviceSize indexCapacity, VkDeviceSize heapCapacity,
                                VkDeviceSize indirectDrawCapacity,
                                VkDeviceSize commandConstantsCapacity)
            : globalVertexBuffer{device, vertexCapacity,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 VK_SHARING_MODE_EXCLUSIVE,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT},
              globalIndexBuffer{device, indexCapacity, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                VK_SHARING_MODE_EXCLUSIVE,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT},
              globalHeapBuffer{device, heapCapacity,
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                               VK_SHARING_MODE_EXCLUSIVE,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT},
              indirectDrawBuffer{device, indirectDrawCapacity,
                                 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                 VK_SHARING_MODE_EXCLUSIVE,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT},
              commandConstantsBuffer{device, commandConstantsCapacity,
                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                     VK_SHARING_MODE_EXCLUSIVE,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT}
        {
        }
    };

}; // namespace shader_data
