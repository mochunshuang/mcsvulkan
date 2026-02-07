#pragma once

#include "LogicalDevice.hpp"
#include <utility>

namespace mcs::vulkan
{
    class DescriptorSetLayout
    {
        using value_type = VkDescriptorSetLayout;
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
        constexpr DescriptorSetLayout() = default;
        DescriptorSetLayout(const DescriptorSetLayout &) = delete;
        constexpr DescriptorSetLayout(DescriptorSetLayout &&o) noexcept
            : device_{std::exchange(o.device_, {})}, value_{std::exchange(o.value_, {})}
        {
        }
        DescriptorSetLayout &operator=(const DescriptorSetLayout &) = delete;
        DescriptorSetLayout &operator=(DescriptorSetLayout &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                device_ = std::exchange(o.device_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        constexpr DescriptorSetLayout(const LogicalDevice &device,
                                      value_type value) noexcept
            : device_{&device}, value_{value}
        {
        }
        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                device_->destroyDescriptorSetLayout(value_, device_->allocator());
                device_ = {};
                value_ = {};
            }
        }
        constexpr ~DescriptorSetLayout() noexcept
        {
            destroy();
        }
    };
}; // namespace mcs::vulkan