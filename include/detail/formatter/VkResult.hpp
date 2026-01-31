#pragma once

#include "../to_string/VkResult.hpp"
#include <format>

template <>
struct std::formatter<VkResult> : std::formatter<std::string_view>
{
    auto format(VkResult result, std::format_context &ctx) const
    {
        return std::formatter<std::string_view>::format(
            mcs::vulkan::to_string::to_string(result), ctx);
    }
};