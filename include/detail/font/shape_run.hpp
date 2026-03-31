#pragma once

#include "shape_info.hpp"
#include <vector>

namespace mcs::vulkan::font
{
    struct shape_run
    {
        hb_direction_t direction; // bidi
        std::vector<shape_info> runs;
    };
}; // namespace mcs::vulkan::font