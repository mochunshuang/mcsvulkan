#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <print>
#include <ranges>
#include <vector>

#include "../shape_run.hpp"

#include "../../utils/unique_handle.hpp"
#include "../../utils/mcslog.hpp"

#include "../GlyphInfo.hpp"

#include "../utf8proc/codepoint_to_utf8.hpp"

#include "hb.h"
#include "shape_result.hpp"

namespace mcs::vulkan::font::harfbuzz
{
    namespace detail
    {
        struct glyph_info
        {
            hb_glyph_info_t *info;
            hb_glyph_position_t *pos;
            unsigned int glyph_count;
        };

        static constexpr glyph_info get_glyph_info(
            const std::vector<uint32_t> &logical_codepoints, hb_buffer_t *buf,
            hb_direction_t direction, const shape_info &run)
        {
            // NOTE: 这里修改传递的是 logical_codepoints_with_mirror 就行，是吗？
            hb_buffer_add_utf32(buf, logical_codepoints.data() + run.logical_start,
                                run.length, 0, run.length);
            // If you know the direction, script, and language
            hb_buffer_set_direction(buf, direction);
            hb_buffer_set_script(buf, run.script);
            hb_buffer_set_language(buf, run.language);

            assert(run.font != nullptr);
            hb_shape(*run.font->hb_font, buf, nullptr, 0);

            // NOTE: 开始收集信息
            unsigned int glyph_count; // NOLINT
            return {.info = hb_buffer_get_glyph_infos(buf, &glyph_count),
                    .pos = hb_buffer_get_glyph_positions(buf, &glyph_count),
                    .glyph_count = glyph_count};
        }

        static constexpr void add_shape_result(
            std::vector<shape_result> &run_result,
            const std::vector<uint32_t> &logical_codepoints, hb_direction_t direction,
            const shape_info &run, const glyph_info &glyph_info)
        {
            const double fontSizePixels = run.font->font.atlas.size; // NOLINT
            // HarfBuzz uses 26.6 fixed-point numbers for positions (1 unit = 1/64 pixel)
            constexpr double fixed_point_value = 64.0; // NOLINT

            auto [info, pos, glyph_count] = glyph_info;
            for (unsigned i = 0; i < glyph_count; ++i)
            {
                size_t logical_idx = run.logical_start + info[i].cluster;
                hb_glyph_position_t position = pos[i];
                hb_glyph_info_t hb_info = info[i];
                double pixelAdvanceX = position.x_advance / fixed_point_value;
                double pixelAdvanceY = position.y_advance / fixed_point_value;
                double pixelOffsetX = position.x_offset / fixed_point_value;
                double pixelOffsetY = position.y_offset / fixed_point_value;
                double normalizedAdvanceX = pixelAdvanceX / fontSizePixels;
                double normalizedAdvanceY = pixelAdvanceY / fontSizePixels;
                double normalizedOffsetX = pixelOffsetX / fontSizePixels;
                double normalizedOffsetY = pixelOffsetY / fontSizePixels;

                hb_codepoint_t glyph_index = hb_info.codepoint;
                // 获取字形元数据
                auto it = run.font->glyph_index_to_glyphs.find(glyph_index);
                GlyphInfo glyph; // NOLINT
                if (it == run.font->glyph_index_to_glyphs.end())
                {
                    // 回退到该字符的默认字形（需要预先缓存或通过字体查找）
                    auto def_it = run.font->unicode_default_glyphs.find(
                        logical_codepoints[logical_idx]);
                    if (def_it != run.font->unicode_default_glyphs.end())
                        glyph = GlyphInfo::make(*def_it->second, *run.font);
                    else
                    {
                        MCSLOG_WARN("skip shape: no glyph for {}",
                                    logical_codepoints[logical_idx]);
                        continue; // 或使用占位符
                    }
                }
                else
                    glyph = GlyphInfo::make(*it->second, *run.font);
                run_result.emplace_back(
                    shape_result{.glyph_index = glyph_index,
                                 .logical_idx = logical_idx,
                                 .font_ctx = run.font,
                                 .advance_x = normalizedAdvanceX,
                                 .advance_y = normalizedAdvanceY,
                                 .offset_x = normalizedOffsetX,
                                 .offset_y = normalizedOffsetY,
                                 .direction = direction,
                                 .mask = hb_info.mask,
                                 .uv_bounds = glyph.uv_bounds(),
                                 .plane_bounds = glyph.plane_bounds()});
            }
        }

        static constexpr void replacement_shape_result(
            std::vector<shape_result> &run_result, hb_buffer_t *buf,
            hb_direction_t direction, const shape_info &run, const FontContext *font)
        {
            assert(font->glyph_index_to_glyphs.contains(0));

            std::vector<uint32_t> logical_codepoints(run.length, 0);
            hb_buffer_add_utf32(buf, logical_codepoints.data(), run.length, 0,
                                run.length);
            // If you know the direction, script, and language
            hb_buffer_set_direction(buf, direction);
            hb_buffer_set_script(buf, HB_SCRIPT_COMMON);
            hb_buffer_set_language(buf, hb_language_get_default());
            hb_shape(*font->hb_font, buf, nullptr, 0);

            unsigned int glyph_count; // NOLINT
            auto *info = hb_buffer_get_glyph_infos(buf, &glyph_count);
            auto *pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

            const double fontSizePixels = font->font.atlas.size; // NOLINT
            // HarfBuzz uses 26.6 fixed-point numbers for positions (1 unit = 1/64 pixel)
            constexpr double fixed_point_value = 64.0; // NOLINT

            for (unsigned i = 0; i < glyph_count; ++i)
            {
                size_t logical_idx = run.logical_start + info[i].cluster;
                hb_glyph_position_t position = pos[i];
                hb_glyph_info_t hb_info = info[i];
                double pixelAdvanceX = position.x_advance / fixed_point_value;
                double pixelAdvanceY = position.y_advance / fixed_point_value;
                double pixelOffsetX = position.x_offset / fixed_point_value;
                double pixelOffsetY = position.y_offset / fixed_point_value;
                double normalizedAdvanceX = pixelAdvanceX / fontSizePixels;
                double normalizedAdvanceY = pixelAdvanceY / fontSizePixels;
                double normalizedOffsetX = pixelOffsetX / fontSizePixels;
                double normalizedOffsetY = pixelOffsetY / fontSizePixels;

                constexpr hb_codepoint_t replacement_index = 0; // NOLINT
                // 获取字形元数据
                auto it = font->glyph_index_to_glyphs.find(replacement_index);
                GlyphInfo glyph = GlyphInfo::make(*it->second, *font);
                run_result.emplace_back(
                    shape_result{.glyph_index = replacement_index,
                                 .logical_idx = logical_idx,
                                 .font_ctx = font,
                                 .advance_x = normalizedAdvanceX,
                                 .advance_y = normalizedAdvanceY,
                                 .offset_x = normalizedOffsetX,
                                 .offset_y = normalizedOffsetY,
                                 .direction = direction,
                                 .mask = hb_info.mask,
                                 .uv_bounds = glyph.uv_bounds(),
                                 .plane_bounds = glyph.plane_bounds()});
            }
        }

    }; // namespace detail

    static constexpr std::vector<std::vector<shape_result>> shape(
        const std::vector<uint32_t> &logical_codepoints,
        const std::vector<shape_run> &shape_runs, const FontContext *notdefFont)
    {
        using HBBufferPtr = unique_handle<hb_buffer_t *, [](hb_buffer_t *value) noexcept {
            hb_buffer_destroy(value);
        }>;

        HBBufferPtr raii_buf = HBBufferPtr{hb_buffer_create()};
        hb_buffer_t *buf = raii_buf.get();

        std::vector<std::vector<shape_result>> result;
        result.reserve(shape_runs.size());

        for (const auto &shape_run : shape_runs)
        {
            hb_direction_t direction = shape_run.direction;
            std::vector<shape_result> run_result;
            if (direction == HB_DIRECTION_LTR)
            {
                for (const auto &run : shape_run.runs)
                {
                    hb_buffer_reset(buf);
                    if (run.font == nullptr)
                    {
                        detail::replacement_shape_result(run_result, buf, direction, run,
                                                         notdefFont);
                        continue;
                    }
                    detail::add_shape_result(
                        run_result, logical_codepoints, direction, run,
                        detail::get_glyph_info(logical_codepoints, buf, direction, run));
                }
            }
            else
            {
                for (const auto &run : shape_run.runs | std::ranges::views::reverse)
                {
                    hb_buffer_reset(buf);
                    if (run.font == nullptr)
                    {
                        detail::replacement_shape_result(run_result, buf, direction, run,
                                                         notdefFont);
                        continue;
                    }
                    detail::add_shape_result(
                        run_result, logical_codepoints, direction, run,
                        detail::get_glyph_info(logical_codepoints, buf, direction, run));
                }
            }
            result.emplace_back(std::move(run_result));
        }

        return result;
    }

    static constexpr void print_shape_result(
        const std::vector<uint32_t> &logical_codepoints,
        std::vector<std::vector<shape_result>> &results)
    {
        std::println("---Stage 6: Shape Result: Glyphs grouped by logical character ---");
        std::println("Total runs: {}", results.size());

        // 按 logical_idx 分组
        std::unordered_map<size_t, std::vector<const shape_result *>> char_to_glyphs;
        size_t total_glyphs{};
        for (const auto &run_result : results)
            for (const shape_result &run : run_result)
            {
                char_to_glyphs[run.logical_idx].push_back(&run);
                ++total_glyphs;
            }

        std::println("Total logical characters: {}", logical_codepoints.size());
        std::println("Total glyphs: {}", total_glyphs);

        // 遍历每个字符
        for (const auto &run_result : results)
        {
            int i = 0;
            for (const shape_result &run : run_result)
            {
                auto index = run.logical_idx;
                uint32_t cp = logical_codepoints[index];
                char utf8_buf[4]; // NOLINT
                std::string_view char_str = utf8proc::codepoint_to_utf8(cp, utf8_buf);
                std::print("[{} in run sequence] Char {}: U+{:04X} '{}' [is_rtl: {}] "
                           "texture_index: {}, "
                           "sampler_index: {}",
                           i++, index, cp, char_str, run.is_rtl(),
                           run.font_ctx->bind.texture_index,
                           run.font_ctx->bind.sampler_index);
                auto it = char_to_glyphs.find(index);
                if (it == char_to_glyphs.end() || it->second.empty())
                {
                    std::println("  -> No glyph produced for [{} '{}']", index, char_str);
                    continue;
                }
                std::println("  ->have {} glyphs:", it->second.size());
                for (size_t j = 0; j < it->second.size(); ++j)
                {
                    const auto *g = it->second[j];
                    std::println("    Glyph {}: index={}, advance=({:.6f}, {:.6f}), "
                                 "offset=({:.6f}, {:.6f}), "
                                 "unsafe={}, font='{}', "
                                 "uv=({:.6f},{:.6f},{:.6f},{:.6f}), "
                                 "plane=({:.6f},{:.6f},{:.6f},{:.6f})",
                                 j, g->glyph_index, g->advance_x, g->advance_y,
                                 g->offset_x, g->offset_y, g->unsafe_to_break(),
                                 (g->font_ctx != nullptr) ? g->font_ctx->name : "?",
                                 g->uv_bounds.left, g->uv_bounds.bottom,
                                 g->uv_bounds.right, g->uv_bounds.top,
                                 g->plane_bounds.left, g->plane_bounds.bottom,
                                 g->plane_bounds.right, g->plane_bounds.top);
                }
            }
            std::println("");
        }
    }
}; // namespace mcs::vulkan::font::harfbuzz