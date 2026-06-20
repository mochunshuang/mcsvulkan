#pragma once

#include "../LogicalDevice.hpp"
#include <cassert>

namespace mcs::vulkan::memory
{
    struct image_base
    {
        image_base() = default; // NOLINTNEXTLINE
        constexpr image_base(const LogicalDevice &device, VkImage image,
                             VkDeviceMemory imageMemory) noexcept
            : device_{&device}, image_{image}, imageMemory_{imageMemory}
        {
            assert(device_ != nullptr && image_ != nullptr && imageMemory_ != nullptr);
        }
        constexpr ~image_base() noexcept
        {
            destroy();
        }
        image_base(const image_base &) = delete;
        image_base &operator=(const image_base &) = delete;

        constexpr image_base(image_base &&other) noexcept
            : device_{std::exchange(other.device_, nullptr)},
              image_{std::exchange(other.image_, nullptr)},
              imageMemory_{std::exchange(other.imageMemory_, nullptr)}
        {
        }

        constexpr image_base &operator=(image_base &&other) noexcept
        {
            if (&other != this)
            {
                if (device_ != nullptr)
                {
                    device_->freeMemory(imageMemory_, device_->allocator());
                    device_->destroyImage(image_, device_->allocator());
                }
                device_ = std::exchange(other.device_, nullptr);
                image_ = std::exchange(other.image_, nullptr);
                imageMemory_ = std::exchange(other.imageMemory_, nullptr);
            }
            return *this;
        }

        [[nodiscard]] constexpr auto image() const noexcept
        {
            return image_;
        }
        [[nodiscard]] constexpr auto imageMemory() const noexcept
        {
            return imageMemory_;
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
                device_->freeMemory(imageMemory_, device_->allocator());
                device_->destroyImage(image_, device_->allocator());
                device_ = nullptr;
                image_ = nullptr;
                imageMemory_ = nullptr;
            }
        }

      private:
        const LogicalDevice *device_{};
        VkImage image_{};
        VkDeviceMemory imageMemory_{};
    };
}; // namespace mcs::vulkan::memory
