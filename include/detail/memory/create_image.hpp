#pragma once
#include "image_base.hpp"
#include "../tool/pNext.hpp"
#include "../tool/flags.hpp"
#include "../ImageView.hpp"
#include "memory_allocate_info.hpp"
#include <functional>
#include <utility>
#include <vector>

namespace mcs::vulkan::memory
{
    struct create_image
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
            constexpr VkImageCreateInfo operator()() const noexcept
            {
                using tool::sType;
                return {.sType = sType<VkImageCreateInfo>(),
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
                        .pQueueFamilyIndices = queueFamilyIndices.data(),
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
        }; // NOLINTEND
        static_assert(std::is_default_constructible_v<create_info>);
        constexpr auto &&setCreateInfo(this auto &&self, create_info createInfo) noexcept
        {
            self.createInfo_ = std::move(createInfo);
            return std::forward<decltype(self)>(self);
        }
        using memory_allocate_info = memory_allocate_info;
        using GenMemoryAllocateInfo = std::move_only_function<memory_allocate_info(
            VkMemoryRequirements memRequirements,
            VkPhysicalDeviceMemoryProperties memoryProperties)>;
        constexpr auto &&setGenMemoryAllocateInfo(
            this auto &&self, GenMemoryAllocateInfo genMemoryAllocateInfo) noexcept
        {
            self.genMemoryAllocateInfo_ = std::move(genMemoryAllocateInfo);
            return std::forward<decltype(self)>(self);
        }

        //  VkImageViewCreateInfo
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
            constexpr VkImageViewCreateInfo operator()() const noexcept
            {
                using tool::sType;
                return {.sType = sType<VkImageViewCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .image = image,
                        .viewType = viewType,
                        .format = format,
                        .components = components,
                        .subresourceRange = subresourceRange};
            }
            tool::pNext pNext;
            tool::Flags<VkImageViewCreateFlagBits> flags;
            VkImage image;
            VkImageViewType viewType;
            VkFormat format;
            VkComponentMapping components;
            VkImageSubresourceRange subresourceRange;
        }; // NOLINTEND
        static_assert(std::is_default_constructible_v<view_create_info>);
        using GenViewCreateInfo = std::move_only_function<view_create_info(
            VkImageCreateInfo imageCreateInfo, VkImage image) noexcept>;
        constexpr auto &&setGenViewCreateInfo(
            this auto &&self, GenViewCreateInfo genViewCreateInfo) noexcept
        {
            self.genViewCreateInfo_ = std::move(genViewCreateInfo);
            return std::forward<decltype(self)>(self);
        }

        constexpr image_base build(LogicalDevice &device, VkDeviceSize memoryOffset = 0)
        {
            VkImage image = nullptr;
            VkDeviceMemory imageMemory = nullptr;
            try
            {
                image = device.createImage(createInfo_(), device.allocator());
                memory_allocate_info genAllocateInfo = genMemoryAllocateInfo_(
                    device.getImageMemoryRequirements(image),
                    device.physicalDevice()->getMemoryProperties());
                imageMemory =
                    device.allocateMemory(genAllocateInfo(), device.allocator());

                device.bindImageMemory(image, imageMemory, memoryOffset);
                return {device, image, imageMemory};
            }
            catch (...)
            {
                if (imageMemory != nullptr)
                    device.freeMemory(imageMemory, device.allocator());
                if (image != nullptr)
                    device.destroyImage(image, device.allocator());
                throw;
            }
        }
        constexpr VkImageView buildRawView(LogicalDevice &device, VkImage image)
        {
            view_create_info createInfo = genViewCreateInfo_(createInfo_(), image);
            return device.createImageView(createInfo(), device.allocator());
        }
        constexpr ImageView buildImageView(LogicalDevice &device, VkImage image)
        {
            return {device, buildRawView(device, image)};
        }
        constexpr create_image() noexcept : createInfo_{} {}
        constexpr create_image(create_info createInfo,
                               GenMemoryAllocateInfo genMemoryAllocateInfo,
                               GenViewCreateInfo genViewCreateInfo) noexcept
            : createInfo_{std::move(createInfo)},
              genMemoryAllocateInfo_{std::move(genMemoryAllocateInfo)},
              genViewCreateInfo_{std::move(genViewCreateInfo)}
        {
        }

        // info
        constexpr static bool hasStencilComponent(VkFormat format) noexcept
        {
            return format == VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT ||
                   format == VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
        }
        [[nodiscard]] constexpr bool hasStencilComponent() const noexcept
        {
            return hasStencilComponent(createInfo_.format);
        }
        constexpr static VkFormat findSupportedFormat(
            const PhysicalDevice &physicalDevice,
            const std::span<const VkFormat> &candidates, const VkImageTiling &tiling,
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

        // set
        constexpr auto &&setCreateInfoExtent(this auto &&self, VkExtent3D extent) noexcept
        {
            self.createInfo_.extent = extent;
            return std::forward<decltype(self)>(self);
        }
        // get
        constexpr VkFormat &refCreateInfoFormat() noexcept
        {
            return createInfo_.format;
        }
        auto &refCreateInfo() noexcept
        {
            return createInfo_;
        }

      private:
        create_info createInfo_;
        GenMemoryAllocateInfo genMemoryAllocateInfo_;
        GenViewCreateInfo genViewCreateInfo_;
    };
}; // namespace mcs::vulkan::memory