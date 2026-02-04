#pragma once

#include "LogicalDevice.hpp"

namespace mcs::vulkan
{
    class ShaderModule
    {
        using value_type = VkShaderModule;
        const LogicalDevice *device_{};
        value_type value_{};

      public:
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
        [[nodiscard]] auto *device() const noexcept
        {
            return device_;
        }

        ShaderModule() = default;
        constexpr ShaderModule(const LogicalDevice &device, value_type value) noexcept
            : device_{&device}, value_{value}
        {
        }
        constexpr ~ShaderModule() noexcept
        {
            destroy();
        }
        constexpr ShaderModule(ShaderModule &&o) noexcept
            : device_(std::exchange(o.device_, {})), value_{std::exchange(o.value_, {})}
        {
        }
        constexpr ShaderModule &operator=(ShaderModule &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                device_ = std::exchange(o.device_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        ShaderModule(const ShaderModule &) = delete;
        ShaderModule &operator=(const ShaderModule &) = delete;

        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                device_->destroyShaderModule(value_, device_->allocator());
                value_ = {};
                device_ = {};
            }
        }
    };

}; // namespace mcs::vulkan
