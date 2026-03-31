#pragma once

#include "../__harfbuzz_import.hpp"
#include "../__bidi_import.hpp"

namespace mcs::vulkan::font::bidi
{
    struct bidi_script
    {
        SBUInteger offset;
        SBUInteger length;
        hb_script_t script;
    };
}; // namespace mcs::vulkan::font::bidi