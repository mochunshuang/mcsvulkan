#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/display
    struct property_display : property::single_value<YGDisplay, YGNodeStyleSetDisplay>
    {
        using single_value::operator=;
    };
}; // namespace mcs::vulkan::yoga