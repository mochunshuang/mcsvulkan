#pragma once

#include <type_traits>

namespace mcs::vulkan::type_traits
{
    namespace detail
    {
        template <typename T>
        struct ref_depth : std::integral_constant<size_t, 0>
        {
        };
        template <typename T>
        struct ref_depth<T &> : std::integral_constant<size_t, ref_depth<T>::value + 1>
        {
        };
        template <typename T>
        struct ref_depth<const T> : ref_depth<T>
        {
        };
        template <typename T>
        struct ref_depth<volatile T> : ref_depth<T>
        {
        };
        template <typename T>
        struct ref_depth<const volatile T> : ref_depth<T>
        {
        };
    }; // namespace detail

    template <typename T>
    inline constexpr size_t ref_depth_depth_v = detail::ref_depth<T>::value; // NOLINT

}; // namespace mcs::vulkan::type_traits