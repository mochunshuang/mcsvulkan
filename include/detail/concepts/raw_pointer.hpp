#pragma once

#include "../type_traits/pointer_depth.hpp"

namespace mcs::vulkan::concepts
{
    template <typename T>
    concept raw_pointer = type_traits::pointer_depth_v<T> == 1;
};