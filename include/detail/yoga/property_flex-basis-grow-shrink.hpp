#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/flex-basis-grow-shrink
    struct property_flex_basis
        : property::common_property<YGNodeStyleSetFlexBasisAuto, YGNodeStyleSetFlexBasis,
                                    YGNodeStyleSetFlexBasisPercent>
    {
        using common_property::operator=;
    };
    struct property_flex_grow : property::single_value<float, YGNodeStyleSetFlexGrow>
    {
        using single_value::operator=;
    };
    struct property_flex_shrink : property::single_value<float, YGNodeStyleSetFlexShrink>
    {
        using single_value::operator=;
    };
}; // namespace mcs::vulkan::yoga