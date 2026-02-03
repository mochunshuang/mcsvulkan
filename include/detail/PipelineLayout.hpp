#pragma once

#include "LogicalDevice.hpp"
#include <utility>

namespace mcs::vulkan
{
    class PipelineLayout
    {
        using value_type = VkPipelineLayout;
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
        constexpr PipelineLayout() = default;
        PipelineLayout(const PipelineLayout &) = delete;
        constexpr PipelineLayout(PipelineLayout &&o) noexcept
            : device_{std::exchange(o.device_, {})}, value_{std::exchange(o.value_, {})}
        {
        }
        PipelineLayout &operator=(const PipelineLayout &) = delete;
        PipelineLayout &operator=(PipelineLayout &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                device_ = std::exchange(o.device_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        constexpr PipelineLayout(const LogicalDevice &device, value_type value) noexcept
            : device_{&device}, value_{value}
        {
        }
        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                device_->destroyPipelineLayout(value_, device_->allocator());
                device_ = {};
                value_ = {};
            }
        }
        constexpr ~PipelineLayout() noexcept
        {
            destroy();
        }
    };
}; // namespace mcs::vulkan