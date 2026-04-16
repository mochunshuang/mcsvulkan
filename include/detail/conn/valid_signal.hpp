#pragma once

#include "../concepts/no_cvref.hpp"
#include <tuple>

namespace mcs::vulkan::conn
{
    namespace detail
    {
        using concepts::no_cvref;
        // NOLINTBEGIN
        template <typename T>
        struct valid_signal_impl
        {
            static constexpr bool value = false;
        };
        // NOTE: 不需要移动赋值安全，因为不会操作中间值
        template <no_cvref... Args>
            requires((std::is_nothrow_move_constructible_v<Args>) && ...)
        struct valid_signal_impl<void(Args...)>
        {
            static_assert(no_cvref<Args...>);
            static constexpr bool value = true;
            using args_tuple = std::tuple<Args...>;
        };
        template <no_cvref... Args>
            requires((std::is_nothrow_move_constructible_v<Args>) && ...)
        struct valid_signal_impl<void(Args...) noexcept>
        {
            static_assert(no_cvref<Args...>);
            static constexpr bool value = true;
            using args_tuple = std::tuple<Args...>;
        };

        template <typename T, typename... Args>
        struct valid_signal_args_impl
        {
            static constexpr bool value = false;
        };
        template <no_cvref... A, no_cvref... B>
        struct valid_signal_args_impl<void(A...), B...>
        {
            static constexpr bool value = (std::is_same_v<A, B> && ...);
        };
        template <no_cvref... A, no_cvref... B>
        struct valid_signal_args_impl<void(A...) noexcept, B...>
        {
            static constexpr bool value = (std::is_same_v<A, B> && ...);
        };
        // NOLINTEND
    }; // namespace detail

    template <typename T>
    concept valid_signal = std::is_function_v<T> && detail::valid_signal_impl<T>::value;

    template <typename T, typename... Args>
    concept valid_signal_args =
        valid_signal<T> && detail::valid_signal_args_impl<T, Args...>::value;

}; // namespace mcs::vulkan::conn