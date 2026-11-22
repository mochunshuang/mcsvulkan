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

        struct memory_meta
        {
            VkBufferUsageFlags usage;
            VkSharingMode sharing_mode;
            VkMemoryPropertyFlags properties;
        };
        static constexpr auto vertex_memory_meta =
            memory_meta{.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        .sharing_mode = VK_SHARING_MODE_EXCLUSIVE,
                        .properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        static constexpr auto index_memory_meta =
            memory_meta{.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        .sharing_mode = VK_SHARING_MODE_EXCLUSIVE,
                        .properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        static constexpr auto heap_memory_meta =
            memory_meta{.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        .sharing_mode = VK_SHARING_MODE_EXCLUSIVE,
                        .properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        static constexpr auto indirect_memory_meta =
            memory_meta{.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                        .sharing_mode = VK_SHARING_MODE_EXCLUSIVE,
                        .properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        static constexpr auto constant_memory_meta =
            memory_meta{.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        .sharing_mode = VK_SHARING_MODE_EXCLUSIVE,
                        .properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

        constexpr static auto newVertexBuffer(const LogicalDevice &device,
                                              VkDeviceSize capacity)
        {
            return buffer_resource_with_address{
                device, capacity, vertex_memory_meta.usage,
                vertex_memory_meta.sharing_mode, vertex_memory_meta.properties};
        }
        constexpr static auto newIndexBuffer(const LogicalDevice &device,
                                             VkDeviceSize capacity)
        {
            return buffer_resource{device, capacity, index_memory_meta.usage,
                                   index_memory_meta.sharing_mode,
                                   index_memory_meta.properties};
        }

        constexpr static auto newHeapBuffer(const LogicalDevice &device,
                                            VkDeviceSize capacity)
        {
            return buffer_resource_with_address{device, capacity, heap_memory_meta.usage,
                                                heap_memory_meta.sharing_mode,
                                                heap_memory_meta.properties};
        }
        constexpr static auto newIndirectBuffer(const LogicalDevice &device,
                                                VkDeviceSize capacity)
        {
            return buffer_resource{device, capacity, indirect_memory_meta.usage,
                                   indirect_memory_meta.sharing_mode,
                                   indirect_memory_meta.properties};
        }
        constexpr static auto newConstantBuffer(const LogicalDevice &device,
                                                VkDeviceSize capacity)
        {
            return buffer_resource_with_address{
                device, capacity, constant_memory_meta.usage,
                constant_memory_meta.sharing_mode, constant_memory_meta.properties};
        }

        ShaderBuffers() = default;
        constexpr ShaderBuffers(const LogicalDevice &device, VkDeviceSize vertexCapacity,
                                VkDeviceSize indexCapacity, VkDeviceSize heapCapacity,
                                VkDeviceSize indirectDrawCapacity,
                                VkDeviceSize commandConstantsCapacity)
            : globalVertexBuffer{newVertexBuffer(device, vertexCapacity)},
              globalIndexBuffer{newIndexBuffer(device, indexCapacity)},
              globalHeapBuffer{newHeapBuffer(device, heapCapacity)},
              indirectDrawBuffer{newIndirectBuffer(device, indirectDrawCapacity)},
              commandConstantsBuffer{newConstantBuffer(device, commandConstantsCapacity)}
        {
        }
    };

}; // namespace shader_data
