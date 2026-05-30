#pragma once

#include <meta>
#include <string_view>

namespace mcs::vulkan::meta
{
    struct static_string
    {
        const char *value{}; // NOLINT

        template <size_t N>
        consteval static_string(const char (&str)[N]) noexcept // NOLINT
            : value{std::define_static_string(str)}
        {
        }
        [[nodiscard]] constexpr std::string_view view() const noexcept
        {
            return std::string_view{value};
        }
        constexpr bool operator==(const static_string &o) const noexcept
        {
            return view() == o;
        }
        constexpr bool operator==(const std::string_view &o) const noexcept
        {
            return view() == o;
        }
        template <size_t N>
        consteval bool operator==(const char (&str)[N]) const noexcept // NOLINT
        {
            return view() == std::string_view{str, N - 1};
        }
    };
}; // namespace mcs::vulkan::meta