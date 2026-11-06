#pragma once

// ============================================================================
// test_intent4_shader_buffers.hpp
//
// 这五张 buffer 的布局是 **shader 的 ABI 定的**，不是绘制逻辑定的：
//   vertex        —— test_dod24.vert 的 VertexBuffer(vertexAddress)
//   heap          —— GlyphBuffer / RectBuffer / RectangleBuffer / AttrBuffer(dataAddress)
//   constants     —— CommandConstBuffer(commandConstantsAddress)，每条 draw 12B
//   indirect      —— vkCmdDrawIndexedIndirect 的命令数组
//   index         —— 全局索引池（mesh.indexOffset 是 firstIndex）
//
// 所以叫 ShaderBuffers：资源层，零逻辑，不认识 pipeline / 段 / 实例类型。
// ============================================================================

#include <cassert>
#include <cstddef>
#include <cstring>

namespace shader_data
{

    struct BufferResource
    {
        mcs::vulkan::memory::auto_map_buffer buffer{};
        VkDeviceSize capacity{};

        constexpr void write(std::size_t offset, const void *src,
                             std::size_t size) noexcept
        {
            assert(offset + size <= capacity);
            std::memcpy(static_cast<char *>(buffer.mapPtr()) + offset, src, size);
        }

        [[nodiscard]] constexpr void *data() noexcept
        {
            return buffer.mapPtr();
        }

        BufferResource() = default;
        constexpr BufferResource(const LogicalDevice &device, VkDeviceSize capacity,
                                 VkBufferUsageFlags usage, VkSharingMode sharingMode,
                                 VkMemoryPropertyFlags properties)
            : buffer{mcs::vulkan::memory::auto_map_buffer(
                  mcs::vulkan::memory::create_simple_buffer(
                      device,
                      {.size = capacity, .usage = usage, .sharingMode = sharingMode},
                      properties),
                  capacity)},
              capacity{capacity}
        {
        }
    };

    struct BufferResourceWithAddress : BufferResource
    {
        VkDeviceAddress address{};

        BufferResourceWithAddress() = default;

        constexpr BufferResourceWithAddress(const LogicalDevice &device,
                                            VkDeviceSize capacity,
                                            VkBufferUsageFlags usage,
                                            VkSharingMode sharingMode,
                                            VkMemoryPropertyFlags properties)
            : BufferResource(device, capacity, usage, sharingMode, properties),
              address{device.getBufferDeviceAddress(
                  {.sType = sType<VkBufferDeviceAddressInfo>(),
                   .buffer = buffer.buffer()})}
        {
        }
    };

    // shader 需要的数据
    struct ShaderBuffers
    {
        BufferResourceWithAddress globalVertexBuffer{};
        BufferResource globalIndexBuffer{};
        BufferResourceWithAddress globalHeapBuffer{};
        BufferResource indirectDrawBuffer{};
        BufferResourceWithAddress commandConstantsBuffer{};

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

} // namespace shader_data
