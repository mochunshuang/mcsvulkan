#pragma once

#include "../tool/sType.hpp"
#include "../tool/pNext.hpp"
#include "../tool/Flags.hpp"
#include "../utils/check_vkresult.hpp"
#include "image_base.hpp"
#include "../LogicalDevice.hpp"
#include <span>
#include <utility>
#include <vector>

namespace mcs::vulkan::vma
{
    struct create_depth_resources
    {
        /*
        typedef struct VkImageCreateInfo {
            VkStructureType          sType;
            const void*              pNext;
            VkImageCreateFlags       flags;
            VkImageType              imageType;
            VkFormat                 format;
            VkExtent3D               extent;
            uint32_t                 mipLevels;
            uint32_t                 arrayLayers;
            VkSampleCountFlagBits    samples;
            VkImageTiling            tiling;
            VkImageUsageFlags        usage;
            VkSharingMode            sharingMode;
            uint32_t                 queueFamilyIndexCount;
            const uint32_t*          pQueueFamilyIndices;
            VkImageLayout            initialLayout;
        } VkImageCreateInfo;
        */
        struct create_info // NOLINTBEGIN
        {
            [[nodiscard]] constexpr VkImageCreateInfo operator()() const noexcept
            {
                return {.sType = tool::sType<VkImageCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .imageType = imageType,
                        .format = format,
                        .extent = extent,
                        .mipLevels = mipLevels,
                        .arrayLayers = arrayLayers,
                        .samples = samples,
                        .tiling = tiling,
                        .usage = usage,
                        .sharingMode = sharingMode,
                        .queueFamilyIndexCount =
                            static_cast<uint32_t>(queueFamilyIndices.size()),
                        .pQueueFamilyIndices = queueFamilyIndices.empty()
                                                   ? nullptr
                                                   : queueFamilyIndices.data(),
                        .initialLayout = initialLayout};
            }
            tool::pNext pNext;
            tool::Flags<VkImageCreateFlagBits> flags;
            VkImageType imageType;
            VkFormat format;
            VkExtent3D extent;
            uint32_t mipLevels;
            uint32_t arrayLayers;
            VkSampleCountFlagBits samples;
            VkImageTiling tiling;
            VkImageUsageFlags usage;
            VkSharingMode sharingMode;
            std::vector<uint32_t> queueFamilyIndices;
            VkImageLayout initialLayout;

            // new
            VmaAllocationCreateInfo allocationCreateInfo;
        }; // NOLINTEND

        /*
        typedef struct VkImageViewCreateInfo {
            VkStructureType            sType;
            const void*                pNext;
            VkImageViewCreateFlags     flags;
            VkImage                    image;
            VkImageViewType            viewType;
            VkFormat                   format;
            VkComponentMapping         components;
            VkImageSubresourceRange    subresourceRange;
        } VkImageViewCreateInfo;
        */
        struct view_create_info // NOLINTBEGIN
        {
            [[nodiscard]] constexpr VkImageViewCreateInfo operator()() const noexcept
            {
                return {.sType = tool::sType<VkImageViewCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .viewType = viewType,
                        .components = components,
                        .subresourceRange = subresourceRange};
            }
            tool::pNext pNext;
            tool::Flags<VkImageViewCreateFlagBits> flags;
            // VkImage image;
            VkImageViewType viewType;
            // VkFormat format;
            VkComponentMapping components{};
            VkImageSubresourceRange subresourceRange{};

        }; // NOLINTEND

        auto &&setCreateInfo(create_info &&createInfo) noexcept
        {
            createInfo_ = std::move(createInfo);
            return std::move(*this);
        }

        auto &&setViewCreateInfo(view_create_info &&viewCreateInfo)
        {
            viewCreateInfo_ = std::move(viewCreateInfo);
            return std::move(*this);
        }
        [[nodiscard]] image_base rebuild() &
        {
            VkImage image;            // NOLINT
            VmaAllocation allocation; // NOLINT
            try
            {
                auto &allocCI = createInfo_.allocationCreateInfo;
                check_vkresult(::vmaCreateImage(allocator_, &imageCreateInfo_, &allocCI,
                                                &image, &allocation, nullptr));

                imageViewCreateInfo_.image = image;
                VkImageView imageView =
                    device_->createImageView(imageViewCreateInfo_, device_->allocator());
                return image_base{device_, image, allocator_, allocation, imageView};
            }
            catch (...)
            {
                if (image != nullptr)
                {
                    ::vmaDestroyImage(allocator_, image, allocation);
                }
                throw;
            }
        }
        [[nodiscard]] image_base build() &
        {
            imageCreateInfo_ = createInfo_();
            imageViewCreateInfo_ = viewCreateInfo_();
            imageViewCreateInfo_.format = imageCreateInfo_.format;
            return rebuild();
        }
        constexpr create_depth_resources(const LogicalDevice &device,
                                         VmaAllocator allocator) noexcept
            : device_{&device}, allocator_{allocator}
        {
        }

        static VkFormat findSupportedFormat(const PhysicalDevice &physicalDevice,
                                            const std::span<const VkFormat> &candidates,
                                            const VkImageTiling &tiling,
                                            const VkFormatFeatureFlags &features)
        {

            auto formatIt = std::ranges::find_if(candidates, [&](const auto &format) {
                VkFormatProperties props = physicalDevice.getFormatProperties(format);
                return (((tiling == VkImageTiling::VK_IMAGE_TILING_LINEAR) &&
                         ((props.linearTilingFeatures & features) == features)) ||
                        ((tiling == VkImageTiling::VK_IMAGE_TILING_OPTIMAL) &&
                         ((props.optimalTilingFeatures & features) == features)));
            });
            if (formatIt == candidates.end())
                throw make_vk_exception("failed to find supported format!");
            return *formatIt;
        }

        [[nodiscard]] const VkFormat &refImageFormat() const noexcept
        {
            return imageCreateInfo_.format;
        };
        constexpr void updateImageExtent(const VkExtent3D &extent) noexcept
        {
            imageCreateInfo_.extent = extent;
        }

        static bool hasStencilComponent(VkFormat format) noexcept
        {
            return format == VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT ||
                   format == VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
        }
        [[nodiscard]] bool hasStencilComponent() const noexcept
        {
            return hasStencilComponent(imageCreateInfo_.format);
        }

      private:
        const LogicalDevice *device_;
        VmaAllocator allocator_;
        create_info createInfo_;
        view_create_info viewCreateInfo_;
        VkImageCreateInfo imageCreateInfo_{};
        VkImageViewCreateInfo imageViewCreateInfo_{};
    };
}; // namespace mcs::vulkan::vma