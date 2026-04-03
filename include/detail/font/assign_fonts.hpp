#pragma once

#include "FontSelector.hpp"
#include <cassert>
#include <cstddef>
#include <format>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "harfbuzz/script_to_string.hpp"

#include "bidi/visual_result.hpp"

#include "shape_run.hpp"

namespace mcs::vulkan::font
{
    namespace detail
    {
        static constexpr select_result select_font_with_callback(FontSelector &selector,
                                                                 char32_t codepoint,
                                                                 hb_script_t script)
        {
            auto [font, lang] = selector.selectFont(codepoint, script);
            if (lang == nullptr)
                lang = mcs::vulkan::font::FontSelector::getFallbackLanguage(script, font);
            return {.font = font, .language = lang};
        }

        // NOLINTBEGIN
        static constexpr bool skip_show_characters(char32_t cp) noexcept
        {
            // 换行符及段落分隔符
            if (cp == 0x000A || cp == 0x000D || cp == 0x000C || cp == 0x000B ||
                cp == 0x0085 || cp == 0x2028 || cp == 0x2029)
            {
                return true;
            }
            // 零宽空格（不占宽度，只用于断行）
            if (cp == 0x200B)
            {
                return true;
            }
            // 软连字符（不显示，只作为可选断行点）
            if (cp == 0x00AD)
            {
                return true;
            }
            // 其他不可见的 ASCII 控制字符（可选）
            if (cp <= 0x001F || cp == 0x007F)
            {
                return true;
            }
            return false;
        } // NOLINTEND

    }; // namespace detail

    /**
通常不能直接交给一个字体完成全部渲染。BIDI（双向文本）处理包括两个层次：

逻辑重排序：由 Unicode BIDI
算法负责，将混合方向（如阿拉伯语和英语）的文本分割成方向一致的片段（称为 run），并为每个
run 确定基方向。这一过程与字体无关。

字形渲染：每个 run
内部可能包含多种脚本（例如一段阿拉伯语中嵌入的拉丁数字或英语单词）。如果一个字体能覆盖该
run 中的所有字符，则可以用该字体渲染整个
run；否则，即使方向一致，也需要进一步按脚本或字体支持拆分 run，回退到其他字体。
     */
    constexpr static std::vector<shape_run> assign_fonts(
        const bidi::visual_result &script_runs, FontSelector &selector)
    {
        std::vector<shape_run> result;
        result.reserve(script_runs.visual_bidi_runs.size());
        for (const auto &codepoints = script_runs.mirrored_codepoints;
             const auto &bidi_run : script_runs.visual_bidi_runs)
        {
            std::vector<shape_info> runs;
            runs.reserve(bidi_run.script.size());
            for (const auto &script_info : bidi_run.script)
            {
                auto script = script_info.script;
                size_t left = bidi_run.offset + script_info.offset;
                auto logical_end = left + script_info.length;

                // NOTE: 继续按 语言分割
                while (left < logical_end)
                {
                    // TODO(mcs):selectFont()目前从左到右第一个满足就被选中.getFallbackLanguage()
                    auto [font, lang] = detail::select_font_with_callback(
                        selector, codepoints[left], script);

                    size_t right = left;
                    // NOTE: 直到 right + 1 不同
                    while (right + 1 < logical_end)
                    {
                        if (auto [cur_font, cur_lang] = detail::select_font_with_callback(
                                selector, codepoints[right + 1], script);
                            cur_font == font && cur_lang == lang)
                        {
                            ++right;
                            continue;
                        }
                        break;
                    }

                    if (right == left && detail::skip_show_characters(codepoints[left]))
                    {
                        left += 1;
                        continue;
                    }
                    runs.emplace_back(
                        shape_info{.logical_start = left,
                                   .length = static_cast<int>(right + 1 - left),
                                   .script = script,
                                   .language = lang,
                                   .font = font});
                    left = right + 1;
                }
            }
            result.emplace_back(
                shape_run{.direction = bidi_run.direction(), .runs = std::move(runs)});
        }
        return result;
    }

    constexpr static void print_text_runs(const std::vector<shape_run> &runs)
    {
        std::println("--- Stage 4: Font Selection ---");
        std::println("Shaping runs (logical):");
        for (const auto &run : runs)
        {
            std::string str{std::format(
                "direction: {} : ", (run.direction == HB_DIRECTION_RTL) ? "RTL" : "LTR")};
            for (auto r : run.runs)
            {
                // 安全获取字体名
                std::string font_name = r.font == nullptr ? "invalid_font" : r.font->name;
                // 安全获取语言字符串
                const char *lang_str = hb_language_to_string(r.language);
                std::string_view language = (lang_str != nullptr) ? lang_str : "unknown";

                str += std::format("[{},{}), script: {}, font: {}, language: {}; ",
                                   r.logical_start, r.logical_start + r.length,
                                   harfbuzz::script_to_string(r.script), font_name,
                                   language);
            }
            std::println("{}", str);
        }
    }

} // namespace mcs::vulkan::font