#pragma once

#include "visual_run.hpp"
#include <vector>

namespace mcs::vulkan::font::bidi
{
    struct visual_result
    {
        std::vector<uint32_t> mirrored_codepoints;
        std::vector<visual_run> visual_bidi_runs;
    };
}; // namespace mcs::vulkan::font::bidi