#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga::literals
{
    constexpr property::Pixels operator""_px(unsigned long long v) noexcept
    {
        return property::Pixels{static_cast<float>(v)};
    }
    constexpr property::Pixels operator""_px(long double v) noexcept
    {
        return property::Pixels{static_cast<float>(v)};
    }
    constexpr property::Percentage operator""_pc(unsigned long long v) noexcept
    {
        return property::Percentage{static_cast<float>(v)};
    }
    constexpr property::Percentage operator""_pc(long double v) noexcept
    {
        return property::Percentage{static_cast<float>(v)};
    }
}; // namespace mcs::vulkan::yoga::literals
