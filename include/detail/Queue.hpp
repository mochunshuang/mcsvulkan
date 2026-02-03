#pragma once

#include "LogicalDevice.hpp"

namespace mcs::vulkan
{
    class Queue
    {
      public:
        using value_type = VkQueue;
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

        struct create_info
        {
            uint32_t queue_family_index;
            uint32_t queue_index;
        };
        constexpr Queue(const LogicalDevice &device, create_info config) noexcept
            : device_{&device},
              value_{device.getDeviceQueue(config.queue_family_index, config.queue_index)}
        {
        }
        constexpr void submit(uint32_t submitCount, const VkSubmitInfo &submits,
                              VkFence fence) const
        {
            device_->queueSubmit(value_, submitCount, submits, fence);
        }
        [[nodiscard]] constexpr VkResult presentKHR(
            const VkPresentInfoKHR &presentInfo) const
        {
            return device_->queuePresentKHR(value_, presentInfo);
        }
        constexpr void waitIdle() const noexcept
        {
            device_->queueWaitIdle(value_);
        }

      private:
        const LogicalDevice *device_;
        value_type value_;
    };
}; // namespace mcs::vulkan