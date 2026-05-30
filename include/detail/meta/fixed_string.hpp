#pragma once

#include <meta>
#include <string_view>
#include <algorithm>

namespace mcs::vulkan::meta
{
    template <size_t N>
    struct fixed_string
    {
        char data[N]{};                              // NOLINT
        constexpr fixed_string(const char (&str)[N]) // NOLINT
        {
            std::copy_n(str, N, data);
        }

        [[nodiscard]] constexpr std::string_view view() const noexcept
        {
            return std::string_view{data, N - 1};
        }

        constexpr bool operator==(const std::string_view &o) const noexcept
        {
            return view() == o;
        }

        template <size_t M>
        constexpr bool operator==(const fixed_string<M> &o) const noexcept
        {
            return M == N && view() == o.view();
        }

        template <size_t M>
        consteval bool operator==(const char (&str)[M]) const noexcept // NOLINT
        {
            return view() == std::string_view{str, M - 1};
        }

        template <size_t M>
        friend consteval auto operator+(const fixed_string<N> &a,
                                        const fixed_string<M> &b) noexcept
        {
            char str[M + N - 1]; // NOLINT
            std::copy_n(a.data, N - 1, str);
            std::copy_n(b.data, M - 1, str + N - 1);
            str[N + M - 2] = '\0';
            return fixed_string<M + N - 1>{str};
        }
        template <size_t L>
        friend consteval auto operator+(const fixed_string &a,
                                        const char (&str)[L]) noexcept // NOLINT
        {
            return a + fixed_string<L>{str};
        }

        template <size_t L>
        friend consteval auto operator+(const char (&str)[L], // NOLINT
                                        const fixed_string &b) noexcept
        {
            return fixed_string<L>{str} + b;
        }
    };
}; // namespace mcs::vulkan::meta