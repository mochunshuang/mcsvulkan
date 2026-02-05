#pragma once

#include "CommandBufferView.hpp"

namespace mcs::vulkan
{
    class CommandBuffer final : public CommandBufferView
    {
      public:
        CommandBuffer() = default;
        CommandBuffer(const CommandBuffer &) = delete;
        constexpr CommandBuffer(CommandBuffer &&other) noexcept
            : CommandBufferView(std::exchange(other.pool_, {}),
                                std::exchange(other.value_, {}))
        {
        }
        CommandBuffer &operator=(const CommandBuffer &) = delete;
        constexpr CommandBuffer &operator=(CommandBuffer &&other) noexcept
        {
            if (&other != this)
            {
                this->destroy();
                pool_ = std::exchange(other.pool_, {});
                value_ = std::exchange(other.value_, {});
            }
            return *this;
        };
        constexpr explicit CommandBuffer(const CommandPool *pool,
                                         VkCommandBuffer value) noexcept
            : CommandBufferView(pool, value) // 调用基类构造函数
        {
        }
        constexpr ~CommandBuffer() noexcept
        {
            destroy();
        }
        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                pool_->device()->freeOneCommandBuffer(*(*pool_), value_);
                value_ = nullptr;
                pool_ = nullptr;
            }
        }
    };

    [[nodiscard]] constexpr CommandBuffer CommandPool::allocateOneCommandBuffer(
        const allocate_one_info &info) const
    {
        VkCommandBuffer value; // NOLINT
        device_->allocateCommandBuffers(value, info(value_));
        return CommandBuffer{this, value};
    }

}; // namespace mcs::vulkan
