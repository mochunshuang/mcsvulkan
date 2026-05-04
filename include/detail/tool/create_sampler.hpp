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

        // 采样器类型0：线性过滤，重复寻址
        constexpr static create_info templateLinear() noexcept
        {
            return {
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .mipLodBias = 0.0F,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1.0F,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.0F,
                .maxLod = 0.0F,
                .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                .unnormalizedCoordinates = VK_FALSE,
            };
        }

        // 采样器类型1：最近邻过滤，钳位到边缘
        constexpr static create_info templateNearest() noexcept
        {
            return {
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_NEAREST,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .mipLodBias = 0.0F,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1.0F,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.0F,
                .maxLod = 0.0F,
                .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                .unnormalizedCoordinates = VK_FALSE,
            };
        }
        decltype(auto) enableAnisotropy(this auto &&self,
                                        float maxSamplerAnisotropy) noexcept
        {
            self.createInfo_.anisotropyEnable = VK_TRUE;
            self.createInfo_.maxAnisotropy = maxSamplerAnisotropy;
            return std::forward<decltype(self)>(self);
        }
        decltype(auto) disEnableAnisotropy(this auto &&self) noexcept
        {
            self.createInfo_.anisotropyEnable = VK_FALSE;
            self.createInfo_.maxAnisotropy = 1.0F;
            return std::forward<decltype(self)>(self);
        }

        decltype(auto) updateMaxLodByMipmap(this auto &&self, uint32_t mipLevels) noexcept
        {
            self.createInfo_.maxLod =
                (mipLevels > 1) ? static_cast<float>(mipLevels - 1) : 0.0F;
            return std::forward<decltype(self)>(self);
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