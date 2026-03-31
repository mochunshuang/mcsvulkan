#pragma once

#include "__json_import.hpp"
#include "../utils/make_vk_exception.hpp"
#include <fstream>
#include <variant>

namespace mcs::vulkan::font
{
    /** NOTE: 查表即可解码
*@see https://cppreference.dev/w/c/io/fprintf
    %d 对应 int（有符号整数）
    %u 对应 unsigned int（无符号整数）
    当处理其他大小的整数类型（如 short、long long）时，必须添加正确的长度修饰符（如
    %hd、%llu）以确保类型匹配
     */
    /*
    glyph.getQuadAtlasBounds(l, b, r, t);
               if (l || b || r || t) {
                   switch (metrics.yDirection) {
                       case msdfgen::Y_UPWARD:
                           fprintf(f,
    ",\"atlasBounds\":{\"left\":%.17g,\"bottom\":%.17g,\"right\":%.17g,\"top\":%.17g}",
    l, b, r, t); break; case msdfgen::Y_DOWNWARD: fprintf(f,
    ",\"atlasBounds\":{\"left\":%.17g,\"top\":%.17g,\"right\":%.17g,\"bottom\":%.17g}",
    l, metrics.height-t, r, metrics.height-b); break;
                   }
               }
   */
    // msdf-atlas-gen\json-export.cpp
    class Font
    {
        class Atlas // NOLINTBEGIN
        {
          public:
            // "\"type\":\"%s\","
            std::string type;

            // "\"distanceRange\":%.17g," (only present for SDF/PSDF/MSDF/MTSDF)
            std::optional<double> distanceRange;

            // "\"distanceRangeMiddle\":%.17g," (only present for SDF/PSDF/MSDF/MTSDF)
            std::optional<double> distanceRangeMiddle;

            // "\"size\":%.17g,"
            double size = 0.0;

            // "\"width\":%d,"
            int width = 0;

            // "\"height\":%d,"
            int height = 0;

            // "\"yOrigin\":\"%s\""
            std::string yOrigin;

            // Optional grid object
            struct Grid
            {
                // "\"cellWidth\":%d,"
                int cellWidth = 0;

                // "\"cellHeight\":%d,"
                int cellHeight = 0;

                // "\"columns\":%d,"
                int columns = 0;

                // "\"rows\":%d"
                int rows = 0;

                // Optional "\"originX\":%.17g"
                std::optional<double> originX;

                // Optional "\"originY\":%.17g"
                std::optional<double> originY;
            };
            std::optional<Grid> grid;

            constexpr static Atlas make(const font::json_type &obj)
            {
                Atlas ret{};
                const auto &atlas = obj["atlas"];
                ret.type = atlas["type"];

                constexpr auto hasDistance =
                    [](std::string_view type) constexpr noexcept {
                        return type == "sdf" || type == "psdf" || type == "msdf" ||
                               type == "mtsdf";
                    };
                if (hasDistance(ret.type))
                {
                    ret.distanceRange = atlas["distanceRange"];
                    ret.distanceRangeMiddle = atlas["distanceRangeMiddle"];
                }
                ret.size = atlas["size"];
                ret.width = atlas["width"];
                ret.height = atlas["height"];
                ret.yOrigin = atlas["yOrigin"];
                if (const auto it = atlas.find("grid"); it != atlas.end())
                {
                    const auto &obj = *it;
                    const auto get_origin =
                        [&](const char *const key) noexcept -> std::optional<double> {
                        const auto it = obj.find(key);
                        return it != obj.end()
                                   ? std::optional<double>{it->template get<double>()}
                                   : std::nullopt;
                    };
                    ret.grid = Grid{.cellWidth = obj["cellWidth"],
                                    .cellHeight = obj["cellHeight"],
                                    .columns = obj["columns"],
                                    .rows = obj["rows"],
                                    .originX = get_origin("originX"),
                                    .originY = get_origin("originY")};
                }
                return ret;
            }
        };
        class Metrics
        {
          public:
            // "\"emSize\":%.17g,"
            double emSize = 0.0;

            // "\"lineHeight\":%.17g,"
            double lineHeight = 0.0;

            // "\"ascender\":%.17g,"
            double ascender = 0.0;

            // "\"descender\":%.17g,"
            double descender = 0.0;

            // "\"underlineY\":%.17g,"
            double underlineY = 0.0;

            // "\"underlineThickness\":%.17g"
            double underlineThickness = 0.0;

            constexpr static Metrics make(const font::json_type &obj)
            {
                const auto &metrics = obj["metrics"];
                return {.emSize = metrics["emSize"],
                        .lineHeight = metrics["lineHeight"],
                        .ascender = metrics["ascender"],
                        .descender = metrics["descender"],
                        .underlineY = metrics["underlineY"],
                        .underlineThickness = metrics["underlineThickness"]};
            }
        };

        // NOTE: 只考虑unicode码点的类型
        class Glyphs
        {
          public:
            class Bounds
            {
              public:
                //%.17g 使用double
                double left, bottom, right, top;
            };
            using PlaneBounds = Bounds;
            using AtlasBounds = Bounds;
            /*
             switch (font.getPreferredIdentifierType()) {
                            case GlyphIdentifierType::GLYPH_INDEX:
                                fprintf(f, "\"index\":%d,", glyph.getIndex());
                                break;
                            case GlyphIdentifierType::UNICODE_CODEPOINT:
                                fprintf(f, "\"unicode\":%u,", glyph.getCodepoint());
                                break;
                        }
            */
            using GLYPH_INDEX = int;
            using UNICODE_CODEPOINT = unsigned int;
            using IdentifierType =
                std::variant<std::monostate, GLYPH_INDEX, UNICODE_CODEPOINT>;
            // "\"unicode\":%u,"
            IdentifierType index_or_unicode;
            static_assert(!std::is_same_v<char32_t, uint32_t>);
            static_assert(sizeof(char32_t) == sizeof(uint32_t));

            double advance;
            std::optional<PlaneBounds> planeBounds;
            std::optional<AtlasBounds> atlasBounds;

            constexpr static std::vector<Glyphs> make(const font::json_type &obj)
            {
                std::vector<Glyphs> ret{};
                const auto &glyphs = obj["glyphs"];
                for (const auto &glyph : glyphs)
                {
                    // NOTE: 比如 32 即 空格字符，就没有Bounds
                    const auto parseBounds =
                        [&](const char *key) noexcept -> std::optional<Bounds> {
                        const auto it = glyph.find(key);
                        if (it == glyph.end())
                            return std::nullopt;
                        const auto &b = *it;
                        return Bounds{.left = b["left"],
                                      .bottom = b["bottom"],
                                      .right = b["right"],
                                      .top = b["top"]};
                    };
                    const auto get_index_or_unicode = [&]() noexcept -> IdentifierType {
                        const auto it = glyph.find("index");
                        return it != glyph.end()
                                   ? IdentifierType{it->template get<GLYPH_INDEX>()}
                                   : IdentifierType{
                                         glyph["unicode"]
                                             .template get<UNICODE_CODEPOINT>()};
                    };
                    // double a = glyph["advance"].operator double();
                    ret.emplace_back(Glyphs{.index_or_unicode = get_index_or_unicode(),
                                            .advance = glyph["advance"],
                                            .planeBounds = parseBounds("planeBounds"),
                                            .atlasBounds = parseBounds("atlasBounds")});
                }
                ret.shrink_to_fit();
                return ret;
            }
        };

        // NOTE: 仅仅考虑unicode ，不做运行时检查
        class Kerning
        {
          public:
            /*
            fprintf(f, "\"index1\":%d,", kernPair.first.first);
                                    fprintf(f, "\"index2\":%d,", kernPair.first.second);
                                    fprintf(f, "\"advance\":%.17g", kernPair.second);
            */
            struct GLYPH_INDEX
            {
                int index1;
                int index2;
                double advance;
            };
            //"\"unicode2\":%u,"
            struct UNICODE_CODEPOINT
            {
                unsigned int unicode1;
                unsigned int unicode2;
                double advance;
            };

            using kerning_data =
                std::variant<std::monostate, GLYPH_INDEX, UNICODE_CODEPOINT>;
            kerning_data value;

            constexpr static std::vector<Kerning> make(const font::json_type &obj)
            {
                const auto it = obj.find("kerning");
                if (it == obj.end())
                    return {};

                std::vector<Kerning> ret{};
                const auto &kernings = *it;
                for (const auto &kerning : kernings)
                {
                    const auto get_kerning_data = [&]() noexcept -> kerning_data {
                        const auto it = kerning.find("index1");
                        return it != kerning.end() ? kerning_data{GLYPH_INDEX{
                                                         .index1 = it->get<signed int>(),
                                                         .index2 = kerning["index2"],
                                                         .advance = kerning["advance"]}}
                                                   : kerning_data{UNICODE_CODEPOINT{
                                                         .unicode1 = kerning["unicode1"],
                                                         .unicode2 = kerning["unicode2"],
                                                         .advance = kerning["advance"]}};
                    };
                    ret.emplace_back(Kerning{.value = get_kerning_data()});
                }

                ret.shrink_to_fit();
                return ret;
            }
        };

      public:
        using Bounds = Glyphs::Bounds;
        using atlas_type = Atlas;
        using metrics_type = Metrics;
        using glyphs_type = Glyphs;
        using kerning_type = Kerning;
        Atlas atlas{};
        Metrics metrics{};
        std::vector<Glyphs> glyphs;
        std::vector<Kerning> kerning;

        constexpr static Font make(const std::string &jsonPath)
        {
            auto data = font::json_type ::parse(std::ifstream(jsonPath));
            if (data.contains("variants"))
                throw make_vk_exception(".......[TODO] unsuported now.");
            return {.atlas = Atlas::make(data),
                    .metrics = Metrics::make(data),
                    .glyphs = Glyphs::make(data),
                    .kerning = Kerning::make(data)};
        }
    }; // NOLINTEND
}; // namespace mcs::vulkan::font