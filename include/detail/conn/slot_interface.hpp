#pragma once

#include <tuple>

namespace mcs::vulkan::conn
{
    struct slot_interface
    {
        slot_interface() = default;
        slot_interface(const slot_interface &) = default;
        slot_interface(slot_interface &&) = default;
        slot_interface &operator=(const slot_interface &) = default;
        slot_interface &operator=(slot_interface &&) = default;

        constexpr virtual void invoke_impl(void *args) noexcept = 0; // NOLINT
        constexpr virtual ~slot_interface() noexcept = default;

        template <typename... Args>
        constexpr void invoke(Args &...args) & noexcept
        {
            constexpr auto size = sizeof...(args); // NOLINT
            if constexpr (size == 0)
                invoke_impl(nullptr);
            else if (size == 1)
                invoke_impl(static_cast<void *>(&args)...);
            else
            {
                auto args_tuple = std::tuple<Args &...>(args...);
                invoke_impl(&args_tuple);
            }
        }
    };
}; // namespace mcs::vulkan::conn