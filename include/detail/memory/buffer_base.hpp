#pragma once

#include "../LogicalDevice.hpp"
#include <cassert>

namespace mcs::vulkan::memory
{
    struct buffer_base
    {
        buffer_base() = default; // NOLINTNEXTLINE
        constexpr buffer_base(const LogicalDevice &device, VkBuffer buffer,
                              VkDeviceMemory bufferMemory) noexcept
            : device_{&device}, buffer_{buffer}, bufferMemory_{bufferMemory}
        {
            assert(device_ != nullptr && buffer_ != nullptr && bufferMemory_ != nullptr);
        }
        constexpr ~buffer_base() noexcept
        {
            destroy();
        }
        buffer_base(const buffer_base &) = delete;
        buffer_base &operator=(const buffer_base &) = delete;

        constexpr buffer_base(buffer_base &&other) noexcept
            : device_{std::exchange(other.device_, nullptr)},
              buffer_{std::exchange(other.buffer_, nullptr)},
              bufferMemory_{std::exchange(other.bufferMemory_, nullptr)}
        {
        }

        constexpr buffer_base &operator=(buffer_base &&other) noexcept
        {
            if (&other != this)
            {
                if (device_ != nullptr)
                {
                    device_->freeMemory(bufferMemory_, device_->allocator());
                    device_->destroyBuffer(buffer_, device_->allocator());
                }
                device_ = std::exchange(other.device_, nullptr);
                buffer_ = std::exchange(other.buffer_, nullptr);
                bufferMemory_ = std::exchange(other.bufferMemory_, nullptr);
            }
            return *this;
        }

        [[nodiscard]] constexpr auto buffer() const noexcept
        {
            return buffer_;
        }
        [[nodiscard]] constexpr auto bufferMemory() const noexcept
        {
            return bufferMemory_;
        }

        [[nodiscard]] constexpr auto *device() const noexcept
        {
            return device_;
        }
        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return device_ != nullptr;
        }
        constexpr void destroy() noexcept
        {
            if (device_ != nullptr)
            {
                device_->freeMemory(bufferMemory_, device_->allocator());
                device_->destroyBuffer(buffer_, device_->allocator());
                device_ = nullptr;
                buffer_ = nullptr;
                bufferMemory_ = nullptr;
            }
        }
        // NOLINTNEXTLINE
        [[nodiscard]] constexpr void *map(size_t size, VkMemoryMapFlags flags) const
        {
            void *data; // NOLINT
            device_->mapMemory(bufferMemory_, 0, size, flags, &data);
            return data;
        }
        constexpr void unmap() const noexcept
        {
            device_->unmapMemory(bufferMemory_);
        }

      private:
        const LogicalDevice *device_{};
        VkBuffer buffer_{};
        VkDeviceMemory bufferMemory_{};
    };
}; // namespace mcs::vulkan::memory