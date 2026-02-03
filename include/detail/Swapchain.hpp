#pragma once

#include "LogicalDevice.hpp"

namespace mcs::vulkan
{
    class Swapchain
    {
        const LogicalDevice *device_{};
        VkSwapchainKHR swapchain_{};
        std::vector<VkImage> swapChainImages_;
        std::vector<VkImageView> swapChainImageViews_;
        VkExtent2D imageExtent_{};

      public:
        constexpr operator bool() const noexcept // NOLINT
        {
            return swapchain_ != nullptr;
        }
        constexpr auto &operator*() noexcept
        {
            return swapchain_;
        }
        constexpr const auto &operator*() const noexcept
        {
            return swapchain_;
        }
        [[nodiscard]] auto *device() const noexcept
        {
            return device_;
        }
        [[nodiscard]] auto &swapChainImages() const noexcept
        {
            return swapChainImages_;
        }
        [[nodiscard]] auto &swapChainImageViews() const noexcept
        {
            return swapChainImageViews_;
        }
        [[nodiscard]] VkExtent2D imageExtent() const noexcept
        {
            return imageExtent_;
        }

        Swapchain(const Swapchain &) = default;
        constexpr Swapchain(Swapchain &&o) noexcept
            : device_{std::exchange(o.device_, {})},
              swapchain_{std::exchange(o.swapchain_, {})},
              swapChainImages_{std::exchange(o.swapChainImages_, {})},
              swapChainImageViews_{std::exchange(o.swapChainImageViews_, {})},
              imageExtent_{std::exchange(o.imageExtent_, {})}
        {
        }
        Swapchain &operator=(const Swapchain &) = default;
        constexpr Swapchain &operator=(Swapchain &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                device_ = std::exchange(o.device_, {});
                swapchain_ = std::exchange(o.swapchain_, {});
                swapChainImages_ = std::exchange(o.swapChainImages_, {});
                swapChainImageViews_ = std::exchange(o.swapChainImageViews_, {});
                imageExtent_ = std::exchange(o.imageExtent_, {});
            }
            return *this;
        }
        constexpr Swapchain(const LogicalDevice *device, VkSwapchainKHR swapchain,
                            std::vector<VkImage> swapChainImages,
                            std::vector<VkImageView> swapChainImageViews,
                            VkExtent2D imageExtent) noexcept
            : device_{device}, swapchain_{swapchain},
              swapChainImages_{std::move(swapChainImages)},
              swapChainImageViews_{std::move(swapChainImageViews)},
              imageExtent_{imageExtent}
        {
        }
        constexpr void destroy() noexcept
        {
            if (swapchain_ != nullptr)
            {
                for (auto *imageView : swapChainImageViews_)
                    device_->destroyImageView(imageView, device_->allocator());
                swapChainImageViews_.clear();
                device_->destroySwapchainKHR(swapchain_, device_->allocator());
                swapchain_ = nullptr;

                device_ = nullptr;
                imageExtent_ = {};
            }
        }
        constexpr ~Swapchain() noexcept
        {
            destroy();
        }
    };

}; // namespace mcs::vulkan