#pragma once

#include "FontContext.hpp"
namespace mcs::vulkan::font
{
    struct shape_info
    {
        size_t logical_start;
        int length;
        hb_script_t script;      // 脚本（继承自 script run）
        hb_language_t language;  // 该 run 使用的语言（可沿用原 script run 的语言）
        const FontContext *font; // 确保能渲染该段所有字符的字体,因此额外限定
    };
}; // namespace mcs::vulkan::font