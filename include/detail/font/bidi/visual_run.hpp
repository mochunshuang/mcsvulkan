#pragma once

#include "bidi_script.hpp"
#include <vector>

namespace mcs::vulkan::font::bidi
{
    struct visual_run // NOLINTBEGIN
    {
        size_t offset;
        size_t length;
        uint8_t level;
        std::vector<bidi_script> script;

        [[nodiscard]] constexpr hb_direction_t direction() const noexcept
        {
            // Bidi 算法只产生水平方向,否则BUG
            return ((level % 2) != 0) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
        }
    }; // NOLINTEND

}; // namespace mcs::vulkan::font::bidi