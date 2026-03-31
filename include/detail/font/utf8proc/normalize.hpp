#pragma once

#include "../__utf8proc_import.hpp"
#include "normalize_result.hpp"
#include "../../utils/make_vk_exception.hpp"
#include "../../utils/safe_reinterpret_cast.hpp"
#include <print>
#include <string>
#include <string_view>

namespace mcs::vulkan::font::utf8proc
{
    // UAX #15
    static constexpr normalize_result normalize(const utf8proc_uint8_t *text)
    {
        normalize_result result;
        auto *raw = utf8proc_NFC(text);
        if (raw == nullptr)
            throw make_vk_exception("Normalization failed");

        // 接管原始缓冲区（注意：utf8proc_NFC 返回的是 malloc 分配的缓冲区）
        result.utf8_buf.reset(safe_reinterpret_cast<char *>(raw));
        // 计算 UTF-8 字符串长度（不含结尾 '\0'）
        result.utf8_len = std::char_traits<char>::length(result.utf8_buf.get());

        // 解码为 UTF-32 码点
        const auto *p =
            safe_reinterpret_cast<const unsigned char *>(result.utf8_buf.get());
        const unsigned char *end = p + result.utf8_len;
        result.codepoints.clear();
        while (p < end)
        {
            utf8proc_int32_t cp; // NOLINT
            utf8proc_ssize_t bytes = utf8proc_iterate(p, end - p, &cp);
            if (bytes < 0)
                break;
            result.codepoints.push_back(static_cast<uint32_t>(cp));
            p += bytes;
        }

        return result;
    }
    static constexpr normalize_result normalize(const char *text)
    {
        return normalize(safe_reinterpret_cast<const utf8proc_uint8_t *>(text));
    }
    static constexpr normalize_result normalize(const char8_t *text)
    {
        return normalize(safe_reinterpret_cast<const utf8proc_uint8_t *>(text));
    }
    static constexpr normalize_result normalize(const std::string &text)
    {
        return normalize(safe_reinterpret_cast<const utf8proc_uint8_t *>(text.data()));
    }

    static constexpr void print_normalized(const normalize_result &norm,
                                           const char8_t *raw_text)
    {
        std::println("--- Stage 1: Normalization (UAX #15) ---");
        std::println("Original: \"{}\"", safe_reinterpret_cast<const char *>(raw_text));
        std::println("Normalized (NFC): \"{}\"", norm.utf8()); // 直接打印 string_view
    }
    static constexpr void print_normalized(const normalize_result &norm,
                                           std::string_view raw_text)
    {
        std::println("--- Stage 1: Normalization (UAX #15) ---");
        std::println("Original: \"{}\"",
                     safe_reinterpret_cast<const char *>(
                         raw_text.empty() ? nullptr : raw_text.data()));
        std::println("Normalized (NFC): \"{}\"", norm.utf8()); // 直接打印 string_view
    }

}; // namespace mcs::vulkan::font::utf8proc