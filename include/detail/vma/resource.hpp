#pragma once

#include "../LogicalDevice.hpp"

namespace mcs::vulkan::vma
{
    struct resource
    {
        resource() = default;
        resource(const resource &) = delete;
        constexpr resource(resource &&o) noexcept
            : device_{std::exchange(o.device_, {})}, image_{std::exchange(o.image_, {})},
              allocator_{std::exchange(o.allocator_, {})},
              allocation_{std::exchange(o.allocation_, {})},
              imageView_{std::exchange(o.imageView_, {})}
        {
        }
        resource &operator=(const resource &) = delete;
        resource &operator=(resource &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                device_ = std::exchange(o.device_, {});
                image_ = std::exchange(o.image_, {});
                allocator_ = std::exchange(o.allocator_, {});
                allocation_ = std::exchange(o.allocation_, {});
                imageView_ = std::exchange(o.imageView_, {});
            }
            return *this;
        }
        constexpr resource(const LogicalDevice *device, VkImage image,
                           VmaAllocator allocator, VmaAllocation allocation,
                           VkImageView imageView) noexcept
            : device_{device}, image_{image}, allocator_{allocator},
              allocation_{allocation}, imageView_{imageView}
        {
        }
        constexpr ~resource() noexcept
        {
            destroy();
        }
        constexpr void destroy() noexcept
        {
            if (image_ != nullptr)
            {
                device_->destroyImageView(imageView_, device_->allocator());
                ::vmaDestroyImage(allocator_, image_, allocation_);
                device_ = {};
                image_ = {};
                allocator_ = {};
                allocation_ = {};
                imageView_ = {};
            }
        }

        [[nodiscard]] VkImage image() const noexcept
        {
            return image_;
        }
        [[nodiscard]] VkImageView imageView() const noexcept
        {
            return imageView_;
        }

      private:
        const LogicalDevice *device_{};
        VkImage image_{};
        VmaAllocator allocator_{};
        VmaAllocation allocation_{};
        VkImageView imageView_{};
    };
}; // namespace mcs::vulkan::vma