#pragma once

#include "tag_to_string.hpp"

namespace mcs::vulkan::font::harfbuzz
{
    static constexpr std::string script_to_string(hb_script_t script)
    {
        hb_tag_t tag = hb_script_to_iso15924_tag(script);
        return tag == HB_TAG_NONE ? "Invalid" : tag_to_string(tag);
    }
}; // namespace mcs::vulkan::font::harfbuzz