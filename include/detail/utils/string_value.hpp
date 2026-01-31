#pragma once

#include <algorithm>
#include <string_view>

namespace mcs::vulkan
{
    template <size_t N>
    struct string_value
    {
        static constexpr size_t size = N; // NOLINT
        char value[N];                    // NOLINT

        string_value() = delete;
        constexpr string_value(const char (&str)[N]) noexcept // NOLINT
        {
            std::copy_n(str, N, value);
        }

        template <size_t M>
        constexpr bool operator==(const string_value<M> &other) const noexcept
        {
            if constexpr (N != M)
                return false;
            else
                return other.view() == view();
        }

        constexpr bool operator==(const std::string_view &other) const noexcept
        {
            return other == view();
        }

        [[nodiscard]] constexpr std::string_view view() const noexcept
        {
            return std::string_view{value};
        }
    };
}; // namespace mcs::vulkan
