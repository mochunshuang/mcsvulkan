#pragma once

#include "LogicalDevice.hpp"

namespace mcs::vulkan
{
    class Sampler
    {
        const LogicalDevice *device_ = nullptr;
        VkSampler value_ = nullptr;

      public:
        using value_type = VkSampler;
        constexpr operator bool() const noexcept // NOLINT
        {
            return value_ != nullptr;
        }
        constexpr value_type &operator*() noexcept
        {
            return value_;
        }
        constexpr const value_type &operator*() const noexcept
        {
            return value_;
        }
        constexpr Sampler() = default;
        constexpr Sampler(const LogicalDevice &device, VkSampler value) noexcept
            : device_{&device}, value_{value}
        {
        }
        constexpr Sampler(const Sampler &) = delete;
        constexpr Sampler(Sampler &&o) noexcept
            : device_{std::exchange(o.device_, {})}, value_{std::exchange(o.value_, {})}
        {
        }
        constexpr Sampler &operator=(const Sampler &) = delete;
        constexpr Sampler &operator=(Sampler &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                device_ = std::exchange(o.device_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                device_->destroySampler(value_, device_->allocator());
                value_ = nullptr;
            }
        }
        constexpr ~Sampler() noexcept
        {
            destroy();
        }
    };
}; // namespace mcs::vulkan