#pragma once

#include <memory>
#include <vector>

namespace mcs::vulkan::font::utf8proc
{
    // NOLINTBEGIN
    struct normalize_result
    {
        std::unique_ptr<char[]> utf8_buf; // 持有 NFC 结果（以 '\0' 结尾）
        size_t utf8_len;                  // UTF-8 字符串长度（不含结尾 '\0'）
        std::vector<uint32_t> codepoints; // 解码后的 UTF-32 码点

        // 提供只读视图
        [[nodiscard]] std::string_view utf8() const noexcept
        {
            return {utf8_buf.get(), utf8_len};
        }
    };
    // NOLINTEND

}; // namespace mcs::vulkan::font::utf8proc