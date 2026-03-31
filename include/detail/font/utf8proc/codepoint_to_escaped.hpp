#pragma once

#include "codepoint_to_utf8.hpp"
#include <array>
#include <format>
#include <string>
#include <unordered_set>

namespace mcs::vulkan::font::utf8proc
{
    static constexpr std::string codepoint_to_escaped(char32_t cp) // NOLINTBEGIN
    {
        // C0 控制字符名称
        static constexpr std::array<const char *, 32> c0_names = {
            "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS",  "HT",  "LF",
            "VT",  "FF",  "CR",  "SO",  "SI",  "DLE", "DC1", "DC2", "DC3", "DC4", "NAK",
            "SYN", "ETB", "CAN", "EM",  "SUB", "ESC", "FS",  "GS",  "RS",  "US"};
        // 特殊码点（需要强制转义为 \uXXXX）
        static const std::unordered_set<char32_t> specials = {
            0x200B, 0x200C, 0x200D, 0x2066, 0x2067, 0x2068, 0x2069,
            0x202A, 0x202B, 0x202C, 0x202D, 0x202E, 0x061C};

        // C0 控制字符
        if (cp < 0x20)
        {
            return std::format("\\{}", c0_names[static_cast<size_t>(cp)]);
        }
        // DEL
        if (cp == 0x7F)
        {
            return "\\DEL";
        }
        // C1 控制字符
        if (cp >= 0x80 && cp <= 0x9F)
        {
            return std::format("\\u{:04X}", static_cast<unsigned>(cp));
        }
        // 其他特殊码点
        if (specials.contains(cp))
        {
            return std::format("\\u{:04X}", static_cast<unsigned>(cp));
        }
        // 正常字符：返回 UTF-8 表示
        return codepoint_to_utf8(cp);
    } // NOLINTEND

}; // namespace mcs::vulkan::font::utf8proc