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
        // using SBMirrorLocatorPtr =
        //     unique_handle<SBMirrorLocatorRef,
        //                   [](SBMirrorLocatorRef value) constexpr noexcept {
        //                       SBMirrorLocatorRelease(value);
        //                   }>;

        /* Create code point sequence for a sample bidirectional text. */
        SBCodepointSequence codepointSequence{SBStringEncodingUTF32, codepoints.data(),
                                              codepoints.size()};
        /* Extract the first bidirectional paragraph. */
        auto RAIIAlog = SBAlgorithmPtr{SBAlgorithmCreate(&codepointSequence)};
        SBAlgorithmRef bidiAlgorithm = RAIIAlog.get();
        if (bidiAlgorithm == nullptr)
            return {};

        auto RAIIParagraph = SBParagraphPtr{
            SBAlgorithmCreateParagraph(bidiAlgorithm, 0, INT32_MAX, base_level)};
        SBParagraphRef firstParagraph = RAIIParagraph.get();
        SBUInteger paragraphLength = SBParagraphGetLength(firstParagraph);

        /* Create a line consisting of the whole paragraph and get its runs. */
        auto RAIILine =
            SBLinePtr{SBParagraphCreateLine(firstParagraph, 0, paragraphLength)};
        SBLineRef paragraphLine = RAIILine.get();
        SBUInteger runCount = SBLineGetRunCount(paragraphLine);
        const SBRun *runArray = SBLineGetRunsPtr(paragraphLine);

        /* Create a mirror locator and load the line in it. */
        // SBMirrorLocatorPtr RAIIMirrorLocator =
        //     SBMirrorLocatorPtr{SBMirrorLocatorCreate()};
        // SBMirrorLocatorRef mirrorLocator = RAIIMirrorLocator.get();
        // SBMirrorLocatorLoadLine(mirrorLocator, paragraphLine, codepoints.data());
        // const SBMirrorAgent *mirrorAgent = SBMirrorLocatorGetAgent(mirrorLocator);
        // /* Log the details of each mirror in the line. */
        // while (SBMirrorLocatorMoveNext(mirrorLocator) != 0)
        // {
        //     assert(codepoints[mirrorAgent->index] == mirrorAgent->codepoint);
        //     codepoints[mirrorAgent->index] = mirrorAgent->mirror;
        // }

        /* Log the details of each run in the line. */
        std::vector<visual_run> visual_bidi_runs;
        visual_bidi_runs.reserve(runCount);
        for (SBUInteger i = 0; i < runCount; i++)
        {
            visual_bidi_runs.emplace_back(visual_run{
                .offset = runArray[i].offset,
                .length = runArray[i].length,
                .level = runArray[i].level,
                .script = segment_scripts_for_run(std::span{
                    codepoints.data() + runArray[i].offset, runArray[i].length})});
        }

        return {.mirrored_codepoints = std::move(codepoints),
                .visual_bidi_runs = std::move(visual_bidi_runs)};
    }

    static constexpr void print_bidi_result(const std::vector<uint32_t> &codepoints,
                                            const visual_result &visual_result)
    {
        std::println("--- Stage 2: Bidi Analysis (UAX #9) ---");
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
