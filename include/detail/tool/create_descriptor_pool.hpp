#pragma once

#include "sType.hpp"
#include "pNext.hpp"
#include "Flags.hpp"
#include "../DescriptorPool.hpp"

#include <vector>

namespace mcs::vulkan::tool
{

    struct create_descriptor_pool
    {
        /*
        typedef struct VkDescriptorPoolCreateInfo {
            VkStructureType                sType;
            const void*                    pNext;
            VkDescriptorPoolCreateFlags    flags;
            uint32_t                       maxSets;
            uint32_t                       poolSizeCount;
            const VkDescriptorPoolSize*    pPoolSizes;
        } VkDescriptorPoolCreateInfo;
        */
        struct create_info // NOLINTBEGIN
        {
            [[nodiscard]] constexpr VkDescriptorPoolCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkDescriptorPoolCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .maxSets = maxSets,
                        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                        .pPoolSizes = poolSizes.empty() ? nullptr : poolSizes.data()};
            }
            pNext pNext;
            Flags<VkDescriptorPoolCreateFlagBits> flags;
            uint32_t maxSets{};
            std::vector<VkDescriptorPoolSize> poolSizes;
        }; // NOLINTEND
        constexpr auto &setCreateInfo(create_info &&createInfo) noexcept
        {
            createInfo_ = std::move(createInfo);
            return *this;
        }
        [[nodiscard]] constexpr auto build(const LogicalDevice &device) const
        {
            return DescriptorPool{
                device, device.createDescriptorPool(createInfo_(), device.allocator())};
        }

      private:
        create_info createInfo_;
    };
}; // namespace mcs::vulkan::tool