#pragma once

#include "sType.hpp"
#include "pNext.hpp"
#include "Flags.hpp"
#include "../CommandPool.hpp"

namespace mcs::vulkan::tool
{
    struct create_command_pool
    {
        /*
        typedef struct VkCommandPoolCreateInfo {
            VkStructureType             sType;
            const void*                 pNext;
            VkCommandPoolCreateFlags    flags;
            uint32_t                    queueFamilyIndex;
        } VkCommandPoolCreateInfo;
        */
        struct create_info // NOLINTBEGIN
        {
            VkCommandPoolCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkCommandPoolCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .queueFamilyIndex = queueFamilyIndex};
            }
            pNext pNext;
            Flags<VkCommandPoolCreateFlagBits> flags;
            uint32_t queueFamilyIndex{};
        }; // NOLINTEND
        constexpr auto &setCreateInfo(create_info &&createInfo) noexcept
        {
            createInfo_ = std::move(createInfo);
            return *this;
        }

        [[nodiscard]] constexpr auto build(const LogicalDevice &device) const
        {
            return CommandPool{
                device, device.createCommandPool(createInfo_(), device.allocator())};
        }

      private:
        create_info createInfo_;
    };
}; // namespace mcs::vulkan::tool