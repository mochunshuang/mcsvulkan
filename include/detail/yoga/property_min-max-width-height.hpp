#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/min-max-width-height
    struct property_min_width
        : property::noauto_property<YGNodeStyleSetMinWidth, YGNodeStyleSetMinWidthPercent>
    {
        using noauto_property::operator=;
    };
    struct property_max_width
        : property::noauto_property<YGNodeStyleSetMaxWidth, YGNodeStyleSetMaxWidthPercent>
    {
        using noauto_property::operator=;
    };
    struct property_min_height : property::noauto_property<YGNodeStyleSetMinHeight,
                                                           YGNodeStyleSetMinHeightPercent>
    {
        using noauto_property::operator=;
    };
    struct property_max_height : property::noauto_property<YGNodeStyleSetMaxHeight,
                                                           YGNodeStyleSetMaxHeightPercent>
    {
        using noauto_property::operator=;
    };

}; // namespace mcs::vulkan::yoga