#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/flex-wrap
    struct property_flex_wrap : property::single_value<YGWrap, YGNodeStyleSetFlexWrap>
    {
        using single_value::operator=;
    };
}; // namespace mcs::vulkan::yoga