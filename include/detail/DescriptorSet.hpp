#pragma once

#include "DescriptorSetView.hpp"
#include <utility>

namespace mcs::vulkan
{
    class DescriptorSet final : public DescriptorSetView
    {
        using value_type = VkDescriptorSet;

      public:
        using DescriptorSetView::DescriptorSetView;
        constexpr DescriptorSet(const DescriptorPool *descriptorPool, value_type value)
            : DescriptorSetView{descriptorPool, value}
        {
        }
        constexpr ~DescriptorSet() noexcept
        {
            destroy();
        }
        DescriptorSet(const DescriptorSet &) = delete;
        constexpr DescriptorSet(DescriptorSet &&o) noexcept
            : DescriptorSetView(std::exchange(o.pool_, {}), std::exchange(o.value_, {}))
        {
        }
        DescriptorSet &operator=(const DescriptorSet &) = delete;
        constexpr DescriptorSet &operator=(DescriptorSet &&o) noexcept
        {
            if (&o != this)
            {
                this->destroy();
                pool_ = std::exchange(o.pool_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }

        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                pool_->freeDescriptorSets(1, &value_);
                pool_ = nullptr;
                value_ = nullptr;
            }
        }
    };

    [[nodiscard]] constexpr DescriptorSet DescriptorPool::allocateOneCommandBuffer(
        const allocate_one_info &info) const
    {
        return DescriptorSet{this, device_->allocateDescriptorSets(info(value_))[0]};
    }

}; // namespace mcs::vulkan