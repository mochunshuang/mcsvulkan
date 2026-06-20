#pragma once

#include "image_base.hpp"
#include <cassert>

namespace mcs::vulkan::memory
{
    struct resource : image_base
    {
        resource() = default; // NOLINTNEXTLINE
        constexpr resource(image_base base, VkImageView imageView)
            : image_base{std::move(base)}, imageView_{imageView}
        {
            assert(imageView_ != nullptr);
        }
        constexpr ~resource() noexcept
        {
            if (imageView_ != nullptr)
                image_base::device()->destroyImageView(imageView_,
                                                       image_base::device()->allocator());
        }
        resource(const resource &) = delete;
        constexpr resource(resource &&other) noexcept
            : image_base{std::move(other)},
              imageView_{std::exchange(other.imageView_, nullptr)}
        {
        }
        resource &operator=(const resource &) = delete;
        constexpr resource &operator=(resource &&other) noexcept
        {
            if (this != &other)
            {
                if (imageView_ != nullptr)
                    image_base::device()->destroyImageView(
                        imageView_, image_base::device()->allocator());
                image_base::operator=(std::move(other));
                imageView_ = std::exchange(other.imageView_, nullptr);
            }
            return *this;
        }
        [[nodiscard]] VkImageView imageView() const noexcept
        {
            return imageView_;
        }
        constexpr void destroy() noexcept
        {
            if (imageView_ != nullptr)
            {
                if (imageView_ != nullptr)
                    image_base::device()->destroyImageView(
                        imageView_, image_base::device()->allocator());
                image_base::destroy();
            }
        }

      private:
        VkImageView imageView_{};
    };
}; // namespace mcs::vulkan::memory