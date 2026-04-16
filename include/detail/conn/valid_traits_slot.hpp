#pragma once

namespace mcs::vulkan::conn
{
    template <typename T>
    concept valid_traits_slot = requires { typename T::sloter_type; };
}; // namespace mcs::vulkan::conn