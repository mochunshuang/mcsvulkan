#pragma once

#include "Instance.hpp"
#include <utility>

namespace mcs::vulkan
{
    class Debuger
    {
        using value_type = VkDebugUtilsMessengerEXT;
        const Instance *instance_{};
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
        [[nodiscard]] constexpr auto *instance() const noexcept
        {
            return instance_;
        }

        constexpr Debuger() = default;
        Debuger(const Debuger &) = delete;
        constexpr Debuger(Debuger &&o) noexcept
            : instance_{std::exchange(o.instance_, {})},
              value_{std::exchange(o.value_, {})}
        {
        }
        Debuger &operator=(const Debuger &) = delete;
        constexpr Debuger &operator=(Debuger &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                instance_ = std::exchange(o.instance_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        constexpr Debuger(const Instance &instance, value_type value) noexcept
            : instance_{&instance}, value_{value}
        {
        }
        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                instance_->destroyDebugUtilsMessengerEXT(value_, instance_->allocator());
                instance_ = {};
                value_ = {};
            }
        }
        constexpr ~Debuger() noexcept
        {
            destroy();
        }
    };

}; // namespace mcs::vulkan