#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/aspect-ratio
    struct property_aspect_ratio
        : property::single_value<float, YGNodeStyleSetAspectRatio>
    {
        using single_value::operator=;
    };
}; // namespace mcs::vulkan::yoga