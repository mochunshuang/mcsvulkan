#pragma once

#include "LogicalDevice.hpp"
#include <cstddef>

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
        [[nodiscard]] constexpr auto *device() const noexcept
        {
            return device_;
        }
        [[nodiscard]] constexpr auto &swapChainImages() const noexcept
        {
            return swapChainImages_;
        }
        [[nodiscard]] constexpr auto &swapChainImageViews() const noexcept
        {
            return swapChainImageViews_;
        }
        [[nodiscard]] constexpr const VkExtent2D &refImageExtent() const noexcept
        {
            return imageExtent_;
        }
        [[nodiscard]] constexpr VkExtent2D imageExtent() const noexcept
        {
            return imageExtent_;
        }
        [[nodiscard]] constexpr auto imagesSize() const noexcept
        {
            return swapChainImages_.size();
        }
        [[nodiscard]] constexpr auto image(size_t idx) const noexcept
        {
            return swapChainImages_[idx];
        }
        [[nodiscard]] constexpr auto imageView(size_t idx) const noexcept
        {
            return swapChainImageViews_[idx];
        }
        auto acquireNextImage(uint64_t timeout, VkSemaphore semaphore,
                              VkFence fence) const noexcept
        {
            return device_->acquireNextImageKHR(swapchain_, timeout, semaphore, fence);
        }

        Swapchain(const Swapchain &) = delete;
        constexpr Swapchain(Swapchain &&o) noexcept
            : device_{std::exchange(o.device_, {})},
              swapchain_{std::exchange(o.swapchain_, {})},
              swapChainImages_{std::exchange(o.swapChainImages_, {})},
              swapChainImageViews_{std::exchange(o.swapChainImageViews_, {})},
              imageExtent_{std::exchange(o.imageExtent_, {})}
        {
        }
        Swapchain &operator=(const Swapchain &) = delete;
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