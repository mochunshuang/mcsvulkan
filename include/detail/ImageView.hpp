#pragma once

#include "LogicalDevice.hpp"

namespace mcs::vulkan
{
    class ImageView
    {
        using value_type = VkImageView;
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

        ImageView() = default;
        constexpr ImageView(const LogicalDevice &device, value_type value) noexcept
            : device_{&device}, value_{value}
        {
        }
        constexpr ~ImageView() noexcept
        {
            destroy();
        }
        constexpr ImageView(ImageView &&o) noexcept
            : device_(std::exchange(o.device_, {})), value_{std::exchange(o.value_, {})}
        {
        }
        constexpr ImageView &operator=(ImageView &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                device_ = std::exchange(o.device_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        ImageView(const ImageView &) = delete;
        ImageView &operator=(const ImageView &) = delete;

        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                device_->destroyImageView(value_, device_->allocator());
                value_ = {};
                device_ = {};
            }
        }
    };

}; // namespace mcs::vulkan
