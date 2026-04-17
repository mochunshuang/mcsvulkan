#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/layout-direction
    struct property_layout_direction
        : property::single_value<YGDirection, YGNodeStyleSetDirection>
    {
        using single_value::operator=;
    };
}; // namespace mcs::vulkan::yoga