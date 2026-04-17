#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/align-content
    struct property_align_content
        : property::single_value<YGAlign, YGNodeStyleSetAlignContent>
    {
        using single_value::operator=;
    };
}; // namespace mcs::vulkan::yoga