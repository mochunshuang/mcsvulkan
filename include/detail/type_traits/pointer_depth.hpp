#pragma once

#include <type_traits>

namespace mcs::vulkan::type_traits
{
    namespace detail
    {
        template <typename T>
        struct pointer_depth : std::integral_constant<size_t, 0>
        {
        };
        template <typename T>
        struct pointer_depth<T *>
            : std::integral_constant<size_t, pointer_depth<T>::value + 1>
        {
        };
        template <typename T>
        struct pointer_depth<const T> : pointer_depth<T>
        {
        };
        template <typename T>
        struct pointer_depth<volatile T> : pointer_depth<T>
        {
        };
        template <typename T>
        struct pointer_depth<const volatile T> : pointer_depth<T>
        {
        };

    }; // namespace detail

    template <typename T>
    constexpr size_t pointer_depth_v = detail::pointer_depth<T>::value; // NOLINT

}; // namespace mcs::vulkan::type_traits