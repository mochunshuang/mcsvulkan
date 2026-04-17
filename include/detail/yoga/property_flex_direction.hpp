#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/flex-direction
    struct property_flex_direction
        : property::single_value<YGFlexDirection, YGNodeStyleSetFlexDirection>
    {
        using single_value::operator=;
    };

}; // namespace mcs::vulkan::yoga