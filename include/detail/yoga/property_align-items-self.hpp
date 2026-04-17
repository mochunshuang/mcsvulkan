#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/align-items-self
    struct property_align_items
        : property::single_value<YGAlign, YGNodeStyleSetAlignItems>
    {
        using single_value::operator=;
    };
    struct property_align_self : property::single_value<YGAlign, YGNodeStyleSetAlignSelf>
    {
        using single_value::operator=;
    };
}; // namespace mcs::vulkan::yoga