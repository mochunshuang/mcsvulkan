#pragma once

#include "surface_base.hpp"

namespace mcs::vulkan
{
    template <typename Impl>
    struct surface_impl : surface_base
    {
        using base_type = surface_base;

        surface_impl(const surface_impl &) = delete;
        surface_impl &operator=(const surface_impl &) = delete;

        constexpr surface_impl(surface_impl &&other) noexcept
            : surface_base(std::move(other)), window_{std::exchange(other.window_, {})}
        {
        }

        constexpr surface_impl &operator=(surface_impl &&other) noexcept
        {
            if (this != &other)
            {
                destroy();
                static_cast<surface_base &>(*this) = std::move(other);
                window_ = std::exchange(other.window_, {});
            }
            return *this;
        }

        using base_type::base_type;
        constexpr surface_impl(const PhysicalDevice &physicalDevice, const Impl &window)
            : base_type(physicalDevice,
                        window.createVkSurfaceKHR(**physicalDevice.instance())),
              window_{&window}
        {
        }

        constexpr void destroy() noexcept
        {
            if (window_ != nullptr)
            {
                base_type::destroy();
                window_ = nullptr;
            }
        }

        constexpr ~surface_impl() noexcept
        {
            destroy();
        }

        [[nodiscard]] constexpr VkExtent2D chooseSwapExtent(
            const VkSurfaceCapabilitiesKHR &capabilities) const noexcept
        {
            return window_->chooseSwapExtent(capabilities);
        }

        constexpr void waitGoodFramebufferSize() const noexcept
        {
            window_->waitGoodFramebufferSize();
        }

        constexpr auto *window() const noexcept
        {
            return window_;
        }

      private:
        const Impl *window_;
    };

    template <typename Impl>
    surface_impl(const PhysicalDevice &physicalDevice, const Impl &window)
        -> surface_impl<Impl>;

}; // namespace mcs::vulkan