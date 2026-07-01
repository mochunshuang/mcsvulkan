#pragma once

#include <vector>

namespace mcs::vulkan::font
{
    template <typename shape_info>
    struct shape_run
    {
        hb_direction_t direction; // bidi
        std::vector<shape_info> runs;
    };
}; // namespace mcs::vulkan::font