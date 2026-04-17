#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/position
    struct property_position
        : property::single_value<YGPositionType, YGNodeStyleSetPositionType>
    {
        using single_value::operator=;
    };
}; // namespace mcs::vulkan::yoga