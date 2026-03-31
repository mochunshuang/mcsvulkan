#pragma once

#include "break_result.hpp"
#include <cstdint>
#include <print>
#include <string_view>
#include <vector>

#include "../utf8proc/codepoint_to_utf8.hpp"

namespace mcs::vulkan::font::libunibreak
{
    constexpr static break_result analyze_line_breaks(
        const std::vector<uint32_t> &codepoints, std::string_view langBcp47)
    {
        size_t char_count = codepoints.size();

        if (char_count == 0)
            return {};

        break_result result{.types = std::vector<char>(char_count)};
        set_linebreaks_utf32(codepoints.data(), char_count,
                             langBcp47.empty() ? "" : langBcp47.data(),
                             result.types.data());
        return result;
    }

    constexpr static void print_break_result(break_result result,
                                             const std::vector<uint32_t> &codepoints)
    {
        std::println("\n---Stage 5: Line Breaking (UAX #14) ---");
        std::println("Character count: {}", codepoints.size());
        std::println("{:<4} {:<10} {:<8} {}", "Idx", "U+", "Char", "Break Type");

        for (size_t i = 0; i < codepoints.size(); ++i)
        {
            uint32_t cp = codepoints[i];
            char utf8_buf[4]; // NOLINT
            std::string_view char_str = utf8proc::codepoint_to_utf8(cp, utf8_buf);
            if (cp == 0x20)     // NOLINT
                char_str = " "; // 确保空格可见

            // 断行类型映射（基于你提供的宏定义）
            const char *break_str; // NOLINT
            switch (result.types[i])
            {
            case LINEBREAK_MUSTBREAK:
                break_str = "MUSTBREAK";
                break;
            case LINEBREAK_ALLOWBREAK:
                break_str = "ALLOWBREAK";
                break;
            case LINEBREAK_NOBREAK:
                break_str = "NOBREAK";
                break;
            case LINEBREAK_INSIDEACHAR:
                break_str = "INSIDEACHAR";
                break;
            case LINEBREAK_INDETERMINATE:
                break_str = "INDETERMINATE";
                break;
            default:
                break_str = "UNKNOWN";
                break;
            }

            std::println("{:<4} U+{:06X} {:<8} {}", i, cp, char_str, break_str);
        }
    }

}; // namespace mcs::vulkan::font::libunibreak