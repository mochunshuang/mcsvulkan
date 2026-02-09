#pragma once

#include "create_resources.hpp"
#include "../ImageView.hpp"
#include "vma_image.hpp"

namespace mcs::vulkan::vma
{
    struct create_image
    {
        using create_info = create_resources::create_info;
        using view_create_info = create_resources::view_create_info;

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

        constexpr create_image(const LogicalDevice &device,
                               VmaAllocator allocator) noexcept
            : device_{&device}, allocator_{allocator}
        {
        }

        [[nodiscard]] const VkFormat &refImageFormat() const noexcept
        {
            return imageCreateInfo_.format;
        };
        constexpr void updateImageExtent(const VkExtent3D &extent) noexcept
        {
            imageCreateInfo_.extent = extent;
        }

        vma_image makeImage()
        {
            imageCreateInfo_ = createInfo_();
            VkImage image;            // NOLINT
            VmaAllocation allocation; // NOLINT
            try
            {
                auto &allocCI = createInfo_.allocationCreateInfo;
                check_vkresult(::vmaCreateImage(allocator_, &imageCreateInfo_, &allocCI,
                                                &image, &allocation, nullptr));
                return vma_image{image, allocator_, allocation};
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
        ImageView makeImageView(VkImage image)
        {
            imageViewCreateInfo_ = viewCreateInfo_();
            imageViewCreateInfo_.format = imageCreateInfo_.format;
            imageViewCreateInfo_.image = image;
            VkImageView imageView =
                device_->createImageView(imageViewCreateInfo_, device_->allocator());
            return ImageView{*device_, imageView};
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