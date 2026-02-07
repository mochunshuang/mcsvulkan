#pragma once

#include "sType.hpp"
#include "pNext.hpp"
#include "Flags.hpp"
#include "../DescriptorSetLayout.hpp"

#include <vector>

namespace mcs::vulkan::tool
{
    struct create_descriptor_set_layout
    {
        /*
         typedef struct VkDescriptorSetLayoutBinding {
             uint32_t              binding;
             VkDescriptorType      descriptorType;
             uint32_t              descriptorCount;
             VkShaderStageFlags    stageFlags;
             const VkSampler*      pImmutableSamplers;
         } VkDescriptorSetLayoutBinding;
         */

        /*
        typedef struct VkDescriptorSetLayoutCreateInfo {
            VkStructureType                        sType;
            const void*                            pNext;
            VkDescriptorSetLayoutCreateFlags       flags;
            uint32_t                               bindingCount;
            const VkDescriptorSetLayoutBinding*    pBindings;
        } VkDescriptorSetLayoutCreateInfo;
        */
        struct create_info // NOLINTBEGIN
        {
            [[nodiscard]] constexpr VkDescriptorSetLayoutCreateInfo operator()()
                const noexcept
            {
                return {.sType = sType<VkDescriptorSetLayoutCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .bindingCount = static_cast<uint32_t>(bindings.size()),
                        .pBindings = bindings.empty() ? nullptr : bindings.data()};
            }
            pNext pNext;
            Flags<VkDescriptorSetLayoutCreateFlagBits> flags;
            std::vector<VkDescriptorSetLayoutBinding> bindings;
        }; // NOLINTEND

        [[nodiscard]] constexpr auto build(const LogicalDevice &device) const
        {
            return DescriptorSetLayout{device, device.createDescriptorSetLayout(
                                                   createInfo_(), device.allocator())};
        }
        constexpr auto &setCreateInfo(create_info &&createInfo) noexcept
        {
            createInfo_ = std::move(createInfo);
            return *this;
        }

      private:
        create_info createInfo_;
    };
}; // namespace mcs::vulkan::tool