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
                    auto [font, lang] = selector.selectFont(codepoints[left], script);
                    if (lang == nullptr)
                        lang = mcs::vulkan::font::FontSelector::getFallbackLanguage(
                            script, font);

                    size_t right = left;
                    // NOTE: 直到 right + 1 不同
                    while (right + 1 < logical_end)
                    {
                        auto [cur_font, cur_lang] =
                            selector.selectFont(codepoints[right + 1], script);
                        if (cur_lang == nullptr)
                            cur_lang =
                                mcs::vulkan::font::FontSelector::getFallbackLanguage(
                                    script, cur_font);

                        if (cur_font == font && cur_lang == lang)
                        {
                            ++right;
                            continue;
                        }
                        break;
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