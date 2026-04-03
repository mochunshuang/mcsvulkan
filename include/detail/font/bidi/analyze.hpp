#pragma once

#include "visual_result.hpp"
#include "segment_scripts_for_run.hpp"

#include "../harfbuzz/script_to_string.hpp"

#include "../utf8proc/codepoint_to_escaped.hpp"
#include "../utf8proc/codepoint_to_utf8.hpp"

#include "../../utils/unique_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <print>
#include <ranges>
#include <span>
#include <stdnoreturn.h>
#include <utility>

namespace mcs::vulkan::font::bidi
{
    // 双向分析 (UAX #9) ─→ 逻辑顺序字符信息 + 镜像映射 + 方向运行
    static constexpr visual_result analyze(std::vector<uint32_t> codepoints,
                                           int base_level)
    {
        using SBAlgorithmPtr =
            unique_handle<SBAlgorithmRef, [](SBAlgorithmRef value) constexpr noexcept {
                SBAlgorithmRelease(value);
            }>;
        using SBParagraphPtr =
            unique_handle<SBParagraphRef, [](SBParagraphRef value) constexpr noexcept {
                SBParagraphRelease(value);
            }>;
        using SBLinePtr =
            unique_handle<SBLineRef, [](SBLineRef value) constexpr noexcept {
                SBLineRelease(value);
            }>;

        SBCodepointSequence codepointSequence{SBStringEncodingUTF32, codepoints.data(),
                                              codepoints.size()};
        auto RAIIAlog = SBAlgorithmPtr{SBAlgorithmCreate(&codepointSequence)};
        SBAlgorithmRef bidiAlgorithm = RAIIAlog.get();
        if (bidiAlgorithm == nullptr)
            return {};

        std::vector<visual_run> visual_bidi_runs;
        SBUInteger offset = 0;
        const SBUInteger totalLength = codepoints.size(); // NOLINT
        while (offset < totalLength)
        {
            // 获取当前段落的实际长度（不含分隔符）和分隔符长度
            SBUInteger actualLength, separatorLength; // NOLINT
            SBAlgorithmGetParagraphBoundary(bidiAlgorithm, offset, INT32_MAX,
                                            &actualLength, &separatorLength);
            if (actualLength == 0)
                break;

            // 创建段落：绝对偏移 offset，长度 actualLength
            auto RAIIParagraph = SBParagraphPtr{SBAlgorithmCreateParagraph(
                bidiAlgorithm, offset, actualLength, static_cast<SBLevel>(base_level))};
            SBParagraphRef paragraph = RAIIParagraph.get();
            if (paragraph == nullptr)
                break;

            // 创建行：使用绝对偏移 offset，长度 actualLength
            auto RAIILine =
                SBLinePtr{SBParagraphCreateLine(paragraph, offset, actualLength)};
            SBLineRef line = RAIILine.get();
            if (line == nullptr)
                break;

            SBUInteger runCount = SBLineGetRunCount(line);
            const SBRun *runArray = SBLineGetRunsPtr(line);
            for (SBUInteger i = 0; i < runCount; ++i)
            {
                visual_bidi_runs.emplace_back(visual_run{
                    .offset = runArray[i].offset,
                    .length = runArray[i].length,
                    .level = runArray[i].level,
                    .script = segment_scripts_for_run(std::span{
                        codepoints.data() + runArray[i].offset, runArray[i].length})});
            }

            // 镜像部分保持注释（未启用）
            // ...

            // 移动到下一段落
            offset += actualLength + separatorLength;
        }

        // 注意：这里返回的 mirrored_codepoints 与原 codepoints 相同（未镜像）
        return {.mirrored_codepoints = std::move(codepoints),
                .visual_bidi_runs = std::move(visual_bidi_runs)};
    }
    static constexpr void print_bidi_result(const std::vector<uint32_t> &codepoints,
                                            const visual_result &visual_result)
    {
        std::println("--- Stage 2: Bidi Analysis (UAX #9) ---");
        std::println(" codepoints.size() == visual_result.mirrored_codepoints.size(): {}",
                     codepoints.size() == visual_result.mirrored_codepoints.size());
        for (const auto &run : visual_result.visual_bidi_runs)
        {
            auto scripts = run.script | std::ranges::views::transform([](bidi_script s) {
                               return harfbuzz::script_to_string(s.script);
                           });
            std::print("  [{}, {}) [Level {}, direction {}] [script {}]:  ", run.offset,
                       run.offset + run.length, run.level,
                       ((run.level % 2) != 0) ? "RTL" : "LTR", scripts);
            for (const auto &c : std::span{
                     visual_result.mirrored_codepoints.data() + run.offset, run.length})
                std::print("{}", utf8proc::codepoint_to_escaped(c));
            std::println("");
        }
        std::println("Mirror Info:");
        for (size_t i = 0, size = codepoints.size(); i < size; ++i)
        {
            if (codepoints[i] != visual_result.mirrored_codepoints[i])
            {
                char utf8_buf[4]; // NOLINT
                std::string_view code =
                    utf8proc::codepoint_to_utf8(codepoints[i], utf8_buf);
                char utf8_buf2[4]; // NOLINT
                std::string_view mirrored =
                    utf8proc::codepoint_to_utf8(codepoints[i], utf8_buf2);
                std::println(
                    "  Mirror Index: {} [U+{:06X} -> U+{:06X}] [form->to : '{}' -> '{}']",
                    i, codepoints[i], visual_result.mirrored_codepoints[i],
                    static_cast<unsigned>(codepoints[i]),
                    static_cast<unsigned>(visual_result.mirrored_codepoints[i]), code,
                    mirrored);
            }
        }
    }

}; // namespace mcs::vulkan::font::bidi
