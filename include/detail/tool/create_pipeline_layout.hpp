#pragma once

#include "sType.hpp"
#include "pNext.hpp"
#include "Flags.hpp"
#include "../PipelineLayout.hpp"

#include <vector>

namespace mcs::vulkan::tool
{
    struct create_pipeline_layout
    {
        /*
       typedef struct VkPipelineLayoutCreateInfo {
           VkStructureType                 sType;
           const void*                     pNext;
           VkPipelineLayoutCreateFlags     flags;
           uint32_t                        setLayoutCount;
           const VkDescriptorSetLayout*    pSetLayouts;
           uint32_t                        pushConstantRangeCount;
           const VkPushConstantRange*      pPushConstantRanges;
       } VkPipelineLayoutCreateInfo;
       */
        struct create_info // NOLINTBEGIN
        {
            VkPipelineLayoutCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkPipelineLayoutCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
                        .pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data(),
                        .pushConstantRangeCount =
                            static_cast<uint32_t>(pushConstantRanges.size()),
                        .pPushConstantRanges = pushConstantRanges.empty()
                                                   ? nullptr
                                                   : pushConstantRanges.data()};
            }
            pNext pNext;
            Flags<VkPipelineLayoutCreateFlagBits> flags;
            std::vector<VkDescriptorSetLayout> setLayouts;
            std::vector<VkPushConstantRange> pushConstantRanges;
        }; // NOLINTEND

        [[nodiscard]] auto build(const LogicalDevice &device) const
        {
            return PipelineLayout(
                device, device.createPipelineLayout(createInfo_(), device.allocator()));
        }

        auto &setCreateInfo(create_info &&createInfo) noexcept
        {
            createInfo_ = std::move(createInfo);
            return *this;
        }

      private:
        create_info createInfo_;
    };
}; // namespace mcs::vulkan::tool