#pragma once

#include "LogicalDevice.hpp"
#include "tool/sType.hpp"
#include "tool/pNext.hpp"
#include <optional>
#include <span>

namespace mcs::vulkan
{
    class DescriptorSet;
    class DescriptorSets;
    class DescriptorPool
    {
        using value_type = VkDescriptorPool;
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

        DescriptorPool() = default;
        constexpr DescriptorPool(const LogicalDevice &device, value_type value) noexcept
            : device_{&device}, value_{value}
        {
        }
        constexpr ~DescriptorPool() noexcept
        {
            destroy();
        }
        constexpr DescriptorPool(DescriptorPool &&o) noexcept
            : device_(std::exchange(o.device_, {})), value_{std::exchange(o.value_, {})}
        {
        }
        constexpr DescriptorPool &operator=(DescriptorPool &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                device_ = std::exchange(o.device_, {});
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        DescriptorPool(const DescriptorPool &) = delete;
        DescriptorPool &operator=(const DescriptorPool &) = delete;

        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                device_->destroyDescriptorPool(value_, device_->allocator());
                value_ = {};
                device_ = {};
            }
        }
        constexpr void freeDescriptorSets(
            uint32_t descriptorSetCount,
            const VkDescriptorSet *pDescriptorSets) const noexcept
        {
            device_->freeDescriptorSets(value_, descriptorSetCount, pDescriptorSets);
        }

        /*
        typedef struct VkDescriptorSetAllocateInfo {
            VkStructureType                 sType;
            const void*                     pNext;
            VkDescriptorPool                descriptorPool;
            uint32_t                        descriptorSetCount;
            const VkDescriptorSetLayout*    pSetLayouts;
        } VkDescriptorSetAllocateInfo;
        */
        struct allocate_info
        {
            [[nodiscard]] VkDescriptorSetAllocateInfo operator()(
                VkDescriptorPool descriptorPool) const noexcept
            {
                using tool::sType;
                return {.sType = sType<VkDescriptorSetAllocateInfo>(),
                        .pNext = pNext.value(),
                        .descriptorPool = descriptorPool,
                        .descriptorSetCount =
                            static_cast<uint32_t>(descriptorSets.size()),
                        .pSetLayouts =
                            descriptorSets.empty() ? nullptr : descriptorSets.data()};
            } // NOLINTBEGIN
            tool::pNext pNext{};
            std::span<const VkDescriptorSetLayout> descriptorSets{}; // NOLINTEND
        };

        [[nodiscard]] constexpr DescriptorSets allocateDescriptorSets(
            const allocate_info &info) const;

        struct allocate_one_info
        {
            [[nodiscard]] VkDescriptorSetAllocateInfo operator()(
                VkDescriptorPool descriptorPool) const noexcept
            {
                using tool::sType;
                return {.sType = sType<VkDescriptorSetAllocateInfo>(),
                        .pNext = pNext.value(),
                        .descriptorPool = descriptorPool,
                        .descriptorSetCount =
                            static_cast<uint32_t>(descriptorSet ? 1 : 0),
                        .pSetLayouts = descriptorSet ? &(*descriptorSet) : nullptr};
            } // NOLINTBEGIN
            tool::pNext pNext{};
            std::optional<VkDescriptorSetLayout> descriptorSet; // NOLINTEND
        };
        [[nodiscard]] constexpr DescriptorSet allocateOneCommandBuffer(
            const allocate_one_info &info) const;
    };

}; // namespace mcs::vulkan
