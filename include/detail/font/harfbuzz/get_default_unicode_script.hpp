#pragma once
#include "../__harfbuzz_import.hpp"

namespace mcs::vulkan::font::harfbuzz
{
    static constexpr auto get_default_unicode_script(hb_codepoint_t unicode) noexcept
    {
        return hb_unicode_script(hb_unicode_funcs_get_default(), unicode);
    }
}; // namespace mcs::vulkan::font::harfbuzz