#pragma once

#include <cmath>
#include <algorithm>

namespace mcs::vulkan
{
    constexpr static uint32_t get_mip_levels(auto width, auto height) noexcept
    {
        return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    }
}; // namespace mcs::vulkan
