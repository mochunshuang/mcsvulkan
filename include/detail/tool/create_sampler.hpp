#pragma once

#include "sType.hpp"
#include "pNext.hpp"
#include "Flags.hpp"
#include "../Sampler.hpp"
#include <utility>

namespace mcs::vulkan::tool
{
    struct create_sampler
    {
        /*
       typedef struct VkSamplerCreateInfo {
           VkStructureType         sType;
           const void*             pNext;
           VkSamplerCreateFlags    flags;
           VkFilter                magFilter;
           VkFilter                minFilter;
           VkSamplerMipmapMode     mipmapMode;
           VkSamplerAddressMode    addressModeU;
           VkSamplerAddressMode    addressModeV;
           VkSamplerAddressMode    addressModeW;
           float                   mipLodBias;
           VkBool32                anisotropyEnable;
           float                   maxAnisotropy;
           VkBool32                compareEnable;
           VkCompareOp             compareOp;
           float                   minLod;
           float                   maxLod;
           VkBorderColor           borderColor;
           VkBool32                unnormalizedCoordinates;
       } VkSamplerCreateInfo;
       */
        struct create_info
        {
            VkSamplerCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkSamplerCreateInfo>(),
                        .pNext = *pNext,
                        .flags = flags,
                        .magFilter = magFilter,
                        .minFilter = minFilter,
                        .mipmapMode = mipmapMode,
                        .addressModeU = addressModeU,
                        .addressModeV = addressModeV,
                        .addressModeW = addressModeW,
                        .mipLodBias = mipLodBias,
                        .anisotropyEnable = anisotropyEnable,
                        .maxAnisotropy = maxAnisotropy,
                        .compareEnable = compareEnable,
                        .compareOp = compareOp,
                        .minLod = minLod,
                        .maxLod = maxLod,
                        .borderColor = borderColor,
                        .unnormalizedCoordinates = unnormalizedCoordinates};
            }
            // NOLINTBEGIN
            pNext pNext;
            Flags<VkSamplerCreateFlagBits> flags;
            VkFilter magFilter{};
            VkFilter minFilter{};
            VkSamplerMipmapMode mipmapMode{};
            VkSamplerAddressMode addressModeU{};
            VkSamplerAddressMode addressModeV{};
            VkSamplerAddressMode addressModeW{};
            float mipLodBias{};
            VkBool32 anisotropyEnable{};
            float maxAnisotropy{};
            VkBool32 compareEnable{};
            VkCompareOp compareOp{};
            float minLod{};
            float maxLod{};
            VkBorderColor borderColor{};
            VkBool32 unnormalizedCoordinates{};
            // NOLINTEND
        };

        [[nodiscard]] Sampler build(const LogicalDevice &device) const
        {
            return {device, device.createSampler(createInfo_(), device.allocator())};
        }

        [[nodiscard]] auto &createInfo() const noexcept
        {
            return createInfo_;
        }
        [[nodiscard]] auto &createInfo() noexcept
        {
            return createInfo_;
        }
        auto &&setCreateInfo(create_info &&createInfo) noexcept
        {
            createInfo_ = std::move(createInfo);
            return std::move(*this);
        }

      private:
        create_info createInfo_;
    };

}; // namespace mcs::vulkan::tool