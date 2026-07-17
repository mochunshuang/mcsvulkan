#pragma once

#include <format>
#include <meta>
#include <string_view>

namespace mcs::vulkan::meta
{
    struct static_string
    {
        const char *value{}; // NOLINT

        static_string() = default;
        consteval explicit static_string(std::string_view view)
            : value{std::define_static_string(view)}
        {
        }
        consteval explicit static_string(const char *value) noexcept
            : static_string{std::string_view{value}}
        {
        }
        template <size_t N>
        consteval static_string(const char (&str)[N]) noexcept // NOLINT
            : static_string{std::string_view{str, N - 1}}
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

        constexpr bool operator<(const static_string &o) const noexcept
        {
            return view() < o.view();
        }

        friend std::ostream &operator<<(std::ostream &os, const static_string &s)
        {
            return os << s.view();
        }
    };
}; // namespace mcs::vulkan::meta

namespace std
{
    template <>
    struct formatter<mcs::vulkan::meta::static_string> : formatter<string_view>
    {
        auto format(const mcs::vulkan::meta::static_string &s, format_context &ctx) const
        {
            return formatter<string_view>::format(s.view(), ctx);
        }
    };
} // namespace std