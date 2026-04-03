#pragma once

#include "../__utf8proc_import.hpp"
#include <array>
#include <string>

namespace mcs::vulkan::font::utf8proc
{
    static constexpr std::string_view codepoint_to_utf8(char32_t cp,
                                                        char (&buf)[4]) noexcept // NOLINT
    {
        utf8proc_ssize_t len =
            utf8proc_encode_char(static_cast<utf8proc_int32_t>(cp), // NOLINTNEXTLINE
                                 reinterpret_cast<utf8proc_uint8_t *>(buf));
        if (len > 0)
            return {buf, static_cast<size_t>(len)};
        buf[0] = 'E';
        buf[1] = 'O';
        buf[2] = 'R';
        buf[3] = '\0';
        return {buf, 3};
    }
    static constexpr std::string_view codepoint_to_utf8(char32_t cp,
                                                        std::array<char, 4> &buf) noexcept
    {
        utf8proc_ssize_t len =
            utf8proc_encode_char(static_cast<utf8proc_int32_t>(cp), // NOLINTNEXTLINE
                                 reinterpret_cast<utf8proc_uint8_t *>(buf.data()));
        if (len > 0)
            return {buf.data(), static_cast<size_t>(len)};
        buf = {'E', 'O', 'R', '\0'};
        return {buf.data(), 3};
    }
    static constexpr std::string codepoint_to_utf8(char32_t cp)
    {
        std::array<char, 4> buf; // 最大 4 字节 // NOLINT
        utf8proc_ssize_t len =
            utf8proc_encode_char(static_cast<utf8proc_int32_t>(cp),
                                 reinterpret_cast<utf8proc_uint8_t *>(buf.data()));
        if (len > 0)
            return {buf.data(), static_cast<size_t>(len)};
        return "ERR";
    }

}; // namespace mcs::vulkan::font::utf8proc