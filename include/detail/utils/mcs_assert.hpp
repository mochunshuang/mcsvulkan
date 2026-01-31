#pragma once

#include <print>
#include <source_location>
#include <string_view>

namespace mcs::vulkan
{

#define ENABLE_MCS_ASSERT

    constexpr void MCS_ASSERT(
        bool condition, const std::string_view &condition_str = "",
        std::source_location source = std::source_location::current()) noexcept
    {
#ifdef ENABLE_MCS_ASSERT
        if (not condition)
        {
            constexpr auto COLOR = "\033[100;41;37m";          // NOLINT
            constexpr auto FUN_COLOR = "\033[38;2;255;165;0m"; // NOLINT
            static constexpr auto RESET_COLOR = "\033[0m";     // NOLINT
            constexpr auto RED_COLOR = "\033[31m";             // NOLINT
            std::println("{}Assertion failed{}: [{}:{}:{}{}{}]: {}{}{}", COLOR,
                         RESET_COLOR, source.file_name(), source.line(), FUN_COLOR,
                         source.function_name(), RESET_COLOR, RED_COLOR, condition_str,
                         RESET_COLOR);
        }
#endif
    }

}; // namespace mcs::vulkan
