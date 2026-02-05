#pragma once

#include "CommandBufferView.hpp"
#include <cstddef>
#include <utility>
#include <vector>

namespace mcs::vulkan
{
    class CommandBuffers
    {
        const CommandPool *pool_{};
        std::vector<VkCommandBuffer> value_;

      public:
        [[nodiscard]] CommandBufferView operator[](size_t i) noexcept
        {
            return CommandBufferView{pool_, value_[i]};
        };
        [[nodiscard]] CommandBufferView operator[](size_t i) const noexcept
        {
            return CommandBufferView{pool_, value_[i]};
        };

        CommandBuffers() = default;
        constexpr CommandBuffers(const CommandPool &pool,
                                 std::vector<VkCommandBuffer> value) noexcept
            : pool_{&pool}, value_{std::move(value)}
        {
        }
        CommandBuffers(const CommandBuffers &) = delete;
        constexpr CommandBuffers(CommandBuffers &&o) noexcept
            : pool_{std::exchange(o.pool_, {})}, value_{std::exchange(o.value_, {})}
        {
        }
        CommandBuffers &operator=(const CommandBuffers &) = delete;
        constexpr CommandBuffers &operator=(CommandBuffers &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                pool_ = std::exchange(o.pool_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        constexpr ~CommandBuffers() noexcept
        {
            destroy();
        }
        constexpr void destroy() noexcept
        {
            if (not value_.empty())
            {
                pool_->device()->freeCommandBuffers(*(*pool_), value_.size(),
                                                    value_.data());
                pool_ = nullptr;
                value_.clear();
            }
        }
    };

    [[nodiscard]] constexpr CommandBuffers CommandPool::allocateCommandBuffers(
        const allocate_info &info) const
    {
        return CommandBuffers{*this, device_->allocateCommandBuffers(info(value_))};
    }
}; // namespace mcs::vulkan