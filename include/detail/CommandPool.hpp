#pragma once

#include "LogicalDevice.hpp"
#include "tool/sType.hpp"
#include "tool/pNext.hpp"

namespace mcs::vulkan
{
    class CommandBuffer;
    class CommandBuffers;
    class CommandPool
    {
        using value_type = VkCommandPool;
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

        CommandPool() = default;
        constexpr CommandPool(const LogicalDevice &device, value_type value) noexcept
            : device_{&device}, value_{value}
        {
        }
        constexpr ~CommandPool() noexcept
        {
            destroy();
        }
        constexpr CommandPool(CommandPool &&o) noexcept
            : device_(std::exchange(o.device_, {})), value_{std::exchange(o.value_, {})}
        {
        }
        constexpr CommandPool &operator=(CommandPool &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                device_ = std::exchange(o.device_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        CommandPool(const CommandPool &) = delete;
        CommandPool &operator=(const CommandPool &) = delete;

        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                device_->destroyCommandPool(value_, device_->allocator());
                value_ = {};
                device_ = {};
            }
        }

        /*
         typedef struct VkCommandBufferAllocateInfo {
           VkStructureType         sType;
           const void*             pNext;
           VkCommandPool           commandPool;
           VkCommandBufferLevel    level;
           uint32_t                commandBufferCount;
       } VkCommandBufferAllocateInfo;
       */
        struct allocate_info
        {
            [[nodiscard]] VkCommandBufferAllocateInfo operator()(
                VkCommandPool commandPool) const noexcept
            {
                using tool::sType;
                return {.sType = sType<VkCommandBufferAllocateInfo>(),
                        .pNext = pNext.value(),
                        .commandPool = commandPool,
                        .level = level,
                        .commandBufferCount = commandBufferCount};
            } // NOLINTBEGIN
            tool::pNext pNext{};
            VkCommandBufferLevel level{};
            uint32_t commandBufferCount{}; // NOLINTEND
        };

        [[nodiscard]] constexpr CommandBuffers allocateCommandBuffers(
            const allocate_info &info) const;

        struct allocate_one_info
        {
            [[nodiscard]] VkCommandBufferAllocateInfo operator()(
                VkCommandPool commandPool) const noexcept
            {
                using tool::sType;
                return {.sType = sType<VkCommandBufferAllocateInfo>(),
                        .pNext = pNext.value(),
                        .commandPool = commandPool,
                        .level = level,
                        .commandBufferCount = 1};
            } // NOLINTBEGIN
            tool::pNext pNext{};
            VkCommandBufferLevel level{};
            // NOLINTEND
        };
        [[nodiscard]] constexpr CommandBuffer allocateOneCommandBuffer(
            const allocate_one_info &info) const;
    };

}; // namespace mcs::vulkan
