#pragma once

#include "../type_traits/ref_depth.hpp"

namespace mcs::vulkan::concepts
{
    template <typename T>
    concept raw_lref = type_traits::ref_depth_depth_v<T> == 1;
};
