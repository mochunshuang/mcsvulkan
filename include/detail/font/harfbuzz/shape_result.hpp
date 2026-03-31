#pragma once

#include <cassert>
#include <cstdint>

#include "../GlyphInfo.hpp"
#include "../FontContext.hpp"
#include "hb.h"

namespace mcs::vulkan::font::harfbuzz
{
    struct shape_result // NOLINTBEGIN
    {
        uint32_t glyph_index;
        size_t logical_idx;
        const FontContext *font_ctx;
        double advance_x; // 归一化（相对于 1em）
        double advance_y; // 归一化（相对于 1em）
        double offset_x;  // 归一化
        double offset_y;  // 归一化
        hb_direction_t direction;
        hb_mask_t mask;

        GlyphInfo::Bounds uv_bounds;    // 归一化 UV
        GlyphInfo::Bounds plane_bounds; // 归一化平面边界，使用json的

        [[nodiscard]] constexpr bool unsafe_to_break() const noexcept
        {
            return (mask & HB_GLYPH_FLAG_UNSAFE_TO_BREAK) != 0;
        }

        [[nodiscard]] constexpr bool is_rtl() const noexcept
        {
            return direction == HB_DIRECTION_RTL;
        }
    }; // NOLINTEND

}; // namespace mcs::vulkan::font::harfbuzz