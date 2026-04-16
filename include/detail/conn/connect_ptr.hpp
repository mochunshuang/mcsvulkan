#pragma once

#include "slot_interface.hpp"
#include <cassert>

namespace mcs::vulkan::conn
{
    struct connect_ptr
    {
        static constexpr int MAX_COUNT = 2;

        constexpr void release() noexcept
        {
            assert(ref_count_ > 0);
            --ref_count_;
            if (ref_count_ == 1)
            {
                delete slot_;
                return;
            }
            delete this;
        }
        [[nodiscard]] constexpr bool rcvr_hold() const noexcept // NOLINT
        {
            return ref_count_ == MAX_COUNT;
        }

        constexpr explicit connect_ptr(slot_interface *slot) noexcept : slot_{slot}
        {
            assert(slot_ != nullptr);
        }

        template <typename... Args>
        constexpr void invoke(Args &...args) const noexcept
        {
            assert(rcvr_hold());
            slot_->invoke(args...);
        }
        connect_ptr(const connect_ptr &) = delete;
        connect_ptr &operator=(const connect_ptr &) = delete;
        connect_ptr(connect_ptr &&) = delete;
        connect_ptr &operator=(connect_ptr &&) = delete;
        ~connect_ptr() = default;

      private:
        int ref_count_{MAX_COUNT}; // NOLINT
        slot_interface *slot_;
    };

}; // namespace mcs::vulkan::conn