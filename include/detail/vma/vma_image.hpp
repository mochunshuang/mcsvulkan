#pragma once

#include "../__vulkan.hpp"

namespace mcs::vulkan::vma
{
    struct vma_image
    {
        vma_image() = default;
        vma_image(const vma_image &) = delete;
        constexpr vma_image(vma_image &&o) noexcept
            : image_{std::exchange(o.image_, {})},
              allocator_{std::exchange(o.allocator_, {})},
              allocation_{std::exchange(o.allocation_, {})}
        {
        }
        vma_image &operator=(const vma_image &) = delete;
        vma_image &operator=(vma_image &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                image_ = std::exchange(o.image_, {});
                allocator_ = std::exchange(o.allocator_, {});
                allocation_ = std::exchange(o.allocation_, {});
            }
            return *this;
        }
        constexpr vma_image(VkImage image, VmaAllocator allocator,
                            VmaAllocation allocation) noexcept
            : image_{image}, allocator_{allocator}, allocation_{allocation}
        {
        }
        constexpr ~vma_image() noexcept
        {
            destroy();
        }
        constexpr void destroy() noexcept
        {
            if (image_ != nullptr)
            {
                ::vmaDestroyImage(allocator_, image_, allocation_);
                image_ = {};
                allocator_ = {};
                allocation_ = {};
            }
        }
        constexpr operator bool() const noexcept // NOLINT
        {
            return image_ != nullptr;
        }
        constexpr VkImage &operator*() noexcept
        {
            return image_;
        }
        constexpr const VkImage &operator*() const noexcept
        {
            return image_;
        }

      private:
        VkImage image_{};
        VmaAllocator allocator_{};
        VmaAllocation allocation_{};
    };
}; // namespace mcs::vulkan::vma