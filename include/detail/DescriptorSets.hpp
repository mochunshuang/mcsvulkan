

#include "DescriptorPool.hpp"
#include <cstddef>
#include <utility>
#include <vector>

namespace mcs::vulkan
{
    class DescriptorSets
    {
        using value_type = VkDescriptorSet;
        const DescriptorPool *pool_{};
        std::vector<value_type> value_;

      public:
        [[nodiscard]] auto operator[](size_t i) noexcept
        {
            return value_[i];
        };
        [[nodiscard]] auto operator[](size_t i) const noexcept
        {
            return value_[i];
        };
        [[nodiscard]] auto *pool() const noexcept
        {
            return pool_;
        }
        [[nodiscard]] auto size() const noexcept
        {
            return value_.size();
        }

        DescriptorSets() = default;
        constexpr DescriptorSets(const DescriptorPool &pool,
                                 std::vector<value_type> value) noexcept
            : pool_{&pool}, value_{std::move(value)}
        {
        }
        DescriptorSets(const DescriptorSets &) = delete;
        constexpr DescriptorSets(DescriptorSets &&o) noexcept
            : pool_{std::exchange(o.pool_, {})}, value_{std::exchange(o.value_, {})}
        {
        }
        DescriptorSets &operator=(const DescriptorSets &) = delete;
        constexpr DescriptorSets &operator=(DescriptorSets &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                pool_ = std::exchange(o.pool_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        constexpr ~DescriptorSets() noexcept
        {
            destroy();
        }
        constexpr void destroy() noexcept
        {
            if (not value_.empty())
            {
                pool_->device()->freeDescriptorSets(*(*pool_), value_.size(),
                                                    value_.data());
                pool_ = nullptr;
                value_.clear();
            }
        }
    };

    [[nodiscard]] constexpr DescriptorSets DescriptorPool::allocateDescriptorSets(
        const allocate_info &info) const
    {
        return DescriptorSets{*this, device_->allocateDescriptorSets(info(value_))};
    }

}; // namespace mcs::vulkan
