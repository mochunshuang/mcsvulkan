#pragma once

#include "../utils/check_vkresult.hpp"

namespace mcs::vulkan::vma
{
    struct buffer_base
    {
        constexpr operator bool() const noexcept // NOLINT
        {
            return buffer_ != nullptr;
        }
        constexpr auto &operator*() noexcept
        {
            return buffer_;
        }
        constexpr const auto &operator*() const noexcept
        {
            return buffer_;
        }

        buffer_base() = default;
        constexpr buffer_base(VkBuffer buffer, VmaAllocator allocator,
                              VmaAllocation allocation) noexcept
            : buffer_{buffer}, allocator_{allocator}, allocation_{allocation}
        {
        }
        constexpr ~buffer_base() noexcept
        {
            destroy();
        }
        buffer_base(const buffer_base &) = delete;
        buffer_base &operator=(const buffer_base &) = delete;

        constexpr buffer_base(buffer_base &&other) noexcept
            : buffer_{std::exchange(other.buffer_, {})},
              allocator_{std::exchange(other.allocator_, {})},
              allocation_{std::exchange(other.allocation_, {})}
        {
        }

        constexpr buffer_base &operator=(buffer_base &&other) noexcept
        {
            if (&other != this)
            {
                this->destroy();
                buffer_ = std::exchange(other.buffer_, {});
                allocator_ = std::exchange(other.allocator_, {});
                allocation_ = std::exchange(other.allocation_, {});
            }
            return *this;
        }

        constexpr void destroy() noexcept
        {
            if (buffer_ != nullptr)
            {
                ::vmaDestroyBuffer(allocator_, buffer_, allocation_);
                buffer_ = {};
                allocator_ = {};
                allocation_ = {};
            }
        }

        [[nodiscard]] constexpr VkBuffer buffer() const noexcept
        {
            return buffer_;
        }
        [[nodiscard]] VmaAllocator allocator() const noexcept
        {
            return allocator_;
        }
        [[nodiscard]] constexpr VmaAllocation allocation() const noexcept
        {
            return allocation_;
        }

        [[nodiscard]] constexpr void *map() const
        {
            void *data; // NOLINT
            check_vkresult(::vmaMapMemory(allocator_, allocation_, &data));
            return data;
        }

        constexpr void unmap() const noexcept
        {
            ::vmaUnmapMemory(allocator_, allocation_);
        }

        constexpr void copyDataToBuffer(const void *src, size_t size,
                                        size_t offset = 0) const
        {
#ifdef _DEBUG
            VmaAllocationInfo allocInfo;
            ::vmaGetAllocationInfo(allocator_, allocation_, &allocInfo);

            if (offset + size > allocInfo.size)
                throw make_vk_exception("Buffer copy exceeds allocation size");
#endif //_DEBUG

            void *data = map();
            ::memcpy(data, src, size);
            unmap();
        }

      private:
        VkBuffer buffer_{};
        VmaAllocator allocator_{};
        VmaAllocation allocation_{};
    };
} // namespace mcs::vulkan::vma