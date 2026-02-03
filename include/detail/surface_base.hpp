#pragma once

#include "PhysicalDevice.hpp"

namespace mcs::vulkan
{
    struct surface_base
    {
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

        surface_base(const surface_base &) = delete;
        surface_base &operator=(const surface_base &) = delete;
        surface_base() = default;
        constexpr surface_base(const PhysicalDevice &instance,
                               VkSurfaceKHR surface) noexcept
            : physicalDevice_{&instance}, surface_{surface}
        {
        }
        constexpr surface_base(surface_base &&other) noexcept
            : physicalDevice_{std::exchange(other.physicalDevice_, {})},
              surface_{std::exchange(other.surface_, {})}
        {
        }
        constexpr surface_base &operator=(surface_base &&other) noexcept
        {
            if (this != &other)
            {
                surface_base::destroy();
                physicalDevice_ = std::exchange(other.physicalDevice_, {});
                surface_ = std::exchange(other.surface_, {});
            }
            return *this;
        }
        constexpr ~surface_base() noexcept
        {
            destroy();
        }

        [[nodiscard]] constexpr VkSurfaceKHR surface() const noexcept
        {
            return surface_;
        }
        [[nodiscard]] auto surfaceCapabilities() const
        {
            return physicalDevice_->getSurfaceCapabilitiesKHR(surface_);
        }
        [[nodiscard]] auto surfaceFormats() const
        {
            return physicalDevice_->getSurfaceFormatsKHR(surface_);
        }
        [[nodiscard]] auto surfacePresentModes() const
        {
            return physicalDevice_->getSurfacePresentModesKHR(surface_);
        }

        constexpr void destroy() noexcept
        {
            if (surface_ != nullptr)
            {
                physicalDevice_->destroySurfaceKHR(surface_,
                                                   physicalDevice_->allocator());
                physicalDevice_ = nullptr;
                surface_ = nullptr;
            }
        }

        [[nodiscard]] constexpr auto physicalDevice() const noexcept
        {
            return physicalDevice_;
        }

      private:
        const PhysicalDevice *physicalDevice_{};
        VkSurfaceKHR surface_{};
    };

}; // namespace mcs::vulkan