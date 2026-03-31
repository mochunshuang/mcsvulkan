#pragma once
#include "../__harfbuzz_import.hpp"
#include <string_view>

namespace mcs::vulkan::font::harfbuzz
{
    struct language_type
    {
        hb_tag_t tag = HB_TAG_NONE;
        hb_language_t language{};
        std::string_view lang_bcp47;
    };
}; // namespace mcs::vulkan::font::harfbuzz