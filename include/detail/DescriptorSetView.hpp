#pragma once

#include "DescriptorPool.hpp"

namespace mcs::vulkan
{
    class DescriptorSetView
    {
        using value_type = VkDescriptorSet;
        const DescriptorPool *pool_{};
        value_type value_{};

        friend class DescriptorSet;

      public:
        constexpr explicit operator bool() const noexcept
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
        DescriptorSetView() = default;
        constexpr DescriptorSetView(const DescriptorPool *pool, value_type value)
            : pool_{pool}, value_{value}
        {
        }
        [[nodiscard]] auto *pool() const noexcept
        {
            return pool_;
        }
    };

}; // namespace mcs::vulkan