#pragma once

#include "../__harfbuzz_import.hpp"

namespace mcs::vulkan::font::harfbuzz
{
    static constexpr hb_language_t tag_to_language(hb_tag_t tag) noexcept
    {
        return (tag != HB_TAG_NONE) ? hb_ot_tag_to_language(tag) : nullptr;
    }
}; // namespace mcs::vulkan::font::harfbuzz