#pragma once

#include "LogicalDevice.hpp"
#include <utility>

namespace mcs::vulkan
{
    class Fence
    {
        const LogicalDevice *device_{};
        VkFence value_{};

      public:
        constexpr operator bool() const noexcept // NOLINT
        {
            return value_ != nullptr;
        }
        constexpr auto &operator*() noexcept
        {
            return value_;
        }
        constexpr const auto &operator*() const noexcept
        {
            return value_;
        }

        constexpr Fence() = default;
        Fence(const Fence &) = delete;
        constexpr Fence(Fence &&o) noexcept
            : device_{std::exchange(o.device_, {})},
              value_{std::exchange(o.value_, {})} {};
        Fence &operator=(const Fence &) = delete;
        constexpr Fence &operator=(Fence &&o) noexcept
        {
            if (&o != this)
            {
                this->clear();
                device_ = std::exchange(o.device_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        constexpr Fence(const LogicalDevice &device, const VkFenceCreateInfo &createInfo)
            : device_{&device}, value_{device.createFence(createInfo, device.allocator())}
        {
        }
        constexpr ~Fence() noexcept
        {
            clear();
        }
        constexpr void clear() noexcept
        {
            if (value_ != nullptr)
            {
                device_->destroyFence(value_, device_->allocator());
                device_ = {};
                value_ = {};
            }
        }
    };

}; // namespace mcs::vulkan
