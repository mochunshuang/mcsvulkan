#pragma once

#include "LogicalDevice.hpp"

namespace mcs::vulkan
{
    class Pipeline
    {
        using value_type = VkPipeline;
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

        Pipeline() = default;
        constexpr Pipeline(const LogicalDevice &device, value_type value) noexcept
            : device_{&device}, value_{value}
        {
        }
        constexpr ~Pipeline() noexcept
        {
            destroy();
        }
        constexpr Pipeline(Pipeline &&o) noexcept
            : device_(std::exchange(o.device_, {})), value_{std::exchange(o.value_, {})}
        {
        }
        constexpr Pipeline &operator=(Pipeline &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                device_ = std::exchange(o.device_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        Pipeline(const Pipeline &) = delete;
        Pipeline &operator=(const Pipeline &) = delete;

        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                device_->destroyPipeline(value_, device_->allocator());
                value_ = {};
                device_ = {};
            }
        }
    };

}; // namespace mcs::vulkan
