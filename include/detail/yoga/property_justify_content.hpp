#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/justify-content
    struct property_justify_content
        : property::single_value<YGJustify, YGNodeStyleSetJustifyContent>
    {
        using single_value::operator=;
    };
}; // namespace mcs::vulkan::yoga