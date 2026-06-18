#pragma once

#include "buffer_base.hpp"
#include <utility>

namespace mcs::vulkan::memory
{
    struct auto_map_buffer : buffer_base
    {
        auto_map_buffer() = default; // NOLINTNEXTLINE
        constexpr explicit auto_map_buffer(buffer_base base, size_t bufferSize,
                                           VkMemoryMapFlags flags = 0)
            : buffer_base{std::move(base)}, mapPtr_{buffer_base::map(bufferSize, flags)}
        {
        }
        constexpr ~auto_map_buffer() noexcept
        {
            if (mapPtr_ != nullptr)
                buffer_base::unmap();
        }
        auto_map_buffer(const auto_map_buffer &) = delete;
        constexpr auto_map_buffer(auto_map_buffer &&other) noexcept
            : buffer_base{std::move(other)},
              mapPtr_{std::exchange(other.mapPtr_, nullptr)}
        {
        }
        auto_map_buffer &operator=(const auto_map_buffer &) = delete;
        constexpr auto_map_buffer &operator=(auto_map_buffer &&other) noexcept
        {
            if (this != &other)
            {
                if (mapPtr_ != nullptr)
                    buffer_base::unmap();

                // 全限定函数名字
                buffer_base::operator=(std::move(other));
                mapPtr_ = std::exchange(other.mapPtr_, nullptr);
            }
            return *this;
        }

        // 隐藏基类的 map/unmap，防止手动调用 // NOLINTNEXTLINE
        [[nodiscard]] void *map(size_t size, VkMemoryMapFlags flags) const = delete;
        void unmap() const = delete;

        [[nodiscard]] void *mapPtr() const noexcept
        {
            return mapPtr_;
        }

      private:
        void *mapPtr_{};
    };
}; // namespace mcs::vulkan::memory
