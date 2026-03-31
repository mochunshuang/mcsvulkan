#pragma once
#include "../__harfbuzz_import.hpp"
#include <string>

namespace mcs::vulkan::font::harfbuzz
{
    static constexpr std::string tag_to_string(hb_tag_t tag)
    {
        char buf[5] = {0}; // NOLINT
        hb_tag_to_string(tag, buf);
        return {buf};
    }
}; // namespace mcs::vulkan::font::harfbuzz
