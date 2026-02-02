#pragma once

#include "surface_interface.hpp"
#include "Instance.hpp"

namespace mcs::vulkan
{
    template <typename Impl>
    struct surface_impl : surface_interface
    {
        surface_impl(const surface_impl &) = delete;
        surface_impl(surface_impl &&o) = delete;
        surface_impl &operator=(const surface_impl &) = delete;
        surface_impl &operator=(surface_impl &&o) = delete;

        using surface_interface::surface_interface;
        constexpr surface_impl(const Instance &instance, const Impl &window)
            : instance_{&instance}, surface_{window.createVkSurfaceKHR(*instance)},
              window_{&window}
        {
        }
        constexpr operator bool() const noexcept // NOLINT
        {
            return surface_ != nullptr;
        }
        constexpr auto &operator*() noexcept
        {
            return surface_;
        }
        constexpr const auto &operator*() const noexcept
        {
            return surface_;
        }

        [[nodiscard]] constexpr VkExtent2D chooseSwapExtent(
            const VkSurfaceCapabilitiesKHR &capabilities) const noexcept override
        {
            return window_->chooseSwapExtent(capabilities);
        }
        constexpr void waitGoodFramebufferSize() const noexcept override
        {
            window_->waitGoodFramebufferSize();
        }
        [[nodiscard]] constexpr VkSurfaceKHR surface() const noexcept override
        {
            return surface_;
        }
        constexpr void destroy() override
        {
            if (surface_ != nullptr)
            {
                instance_->destroySurfaceKHR(surface_, instance_->allocator());
                instance_ = {};
                window_ = {};
                surface_ = {};
            }
        }
        constexpr ~surface_impl() override
        {
            destroy();
        }
        auto *instance() const noexcept
        {
            return instance_;
        }
        auto *window() const noexcept
        {
            return window_;
        }

      private:
        const Instance *instance_;
        VkSurfaceKHR surface_;
        const Impl *window_;
    };

    template <typename Impl>
    surface_impl(const Instance &instance, const Impl &window) -> surface_impl<Impl>;

}; // namespace mcs::vulkan
