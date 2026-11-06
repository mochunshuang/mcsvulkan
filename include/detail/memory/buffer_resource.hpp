#pragma once
#include "auto_map_buffer.hpp"
#include "create_simple_buffer.hpp"

namespace mcs::vulkan::memory
{
    struct buffer_resource
    {
        auto_map_buffer buffer{};
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

        buffer_resource() = default;
        constexpr buffer_resource(const LogicalDevice &device, VkDeviceSize capacity,
                                  VkBufferUsageFlags usage, VkSharingMode sharingMode,
                                  VkMemoryPropertyFlags properties)
            : buffer{auto_map_buffer(
                  create_simple_buffer(
                      device,
                      {.size = capacity, .usage = usage, .sharingMode = sharingMode},
                      properties),
                  capacity)},
              capacity{capacity}
        {
        }
    };
}; // namespace mcs::vulkan::memory