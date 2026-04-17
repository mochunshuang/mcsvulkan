#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/width-height
    struct property_width
        : property::common_property<YGNodeStyleSetWidthAuto, YGNodeStyleSetWidth,
                                    YGNodeStyleSetWidthPercent>
    {
        using common_property::operator=;
    };
    struct property_height
        : property::common_property<YGNodeStyleSetHeightAuto, YGNodeStyleSetHeight,
                                    YGNodeStyleSetHeightPercent>
    {
        using common_property::operator=;
    };
}; // namespace mcs::vulkan::yoga