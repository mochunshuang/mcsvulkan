#pragma once

#include "../__yoga_import.hpp"

namespace mcs::vulkan::yoga
{
    struct layout_size
    {
        float available_width{YGUndefined};
        float available_height{YGUndefined};
    };
}; // namespace mcs::vulkan::yoga