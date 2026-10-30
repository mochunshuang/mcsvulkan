#pragma once

#include <meta>
#include <type_traits>

namespace mcs::vulkan::meta
{
    template <typename T>
    struct is_info_array : std::false_type
    {
    };
    template <std::size_t N>
    struct is_info_array<std::array<std::meta::info, N>> : std::true_type
    {
    };
    template <typename T>
    concept info_array = is_info_array<std::remove_cvref_t<T>>::value;

}; // namespace mcs::vulkan::meta