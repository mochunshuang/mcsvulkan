#pragma once

#include "Font.hpp"
#include "FontMetadata.hpp"
#include "FontType.hpp"
#include "FontTexture.hpp"

#include "freetype/face.hpp"
#include "harfbuzz/font.hpp"
#include "texture_bind_sampler.hpp"
#include <cassert>
#include <variant>

namespace mcs::vulkan::font
{
    class FontContext
    {
      public: // NOLINTBEGIN
        std::string name;
        Font font;
        FontTexture texture;
        freetype::face face;

        FontType type;
        texture_bind_sampler bind;

        std::unordered_map<FT_UInt, const Font::glyphs_type *> glyph_index_to_glyphs;
        std::unordered_map<uint32_t, const Font::glyphs_type *> unicode_default_glyphs;
        harfbuzz::font hb_font;

        FontMetadata meta_data;
        // NOLINTEND

        constexpr FontContext(const std::string &jsonPath,
                              const FontTexture::msdf_info &info, freetype::face &&face,
                              FontType type, texture_bind_sampler bind,
                              FontMetadata meta_data)
            : name(jsonPath), font(Font::make(jsonPath)), texture(info),
              face(std::move(face)), type(type), bind(bind),
              meta_data{std::move(meta_data)}
        {
            FT_Face raw_face = *this->face; // 获取原始 FT_Face
            FT_Set_Pixel_Sizes(raw_face, 0,
                               static_cast<FT_UInt>(font.atlas.size)); // 设置像素大小

            hb_font = harfbuzz::font{raw_face}; // 创建 HarfBuzz 字体

            // 1. 填充 glyph_index_to_info
            for (const auto &glyph : font.glyphs)
            {
                using GLYPH_INDEX = Font::glyphs_type::GLYPH_INDEX;
                using UNICODE_CODEPOINT = Font::glyphs_type::UNICODE_CODEPOINT;
                static_assert(sizeof(GLYPH_INDEX) == sizeof(UNICODE_CODEPOINT),
                              "should equal.");
                static_assert(sizeof(GLYPH_INDEX) == sizeof(FT_UInt), "should equal.");

                assert(!std::holds_alternative<std::monostate>(glyph.index_or_unicode));
                if (std::holds_alternative<GLYPH_INDEX>(glyph.index_or_unicode))
                {
                    auto glyph_index = static_cast<FT_UInt>(
                        std::get<GLYPH_INDEX>(glyph.index_or_unicode));
                    MCS_ASSERT(!glyph_index_to_glyphs.contains(glyph_index));
                    glyph_index_to_glyphs[glyph_index] = &glyph;
                }
                else
                {
                    auto codepoint = std::get<UNICODE_CODEPOINT>(glyph.index_or_unicode);
                    FT_UInt glyph_index = FT_Get_Char_Index(raw_face, codepoint);
                    if (glyph_index != 0)
                    {
                        MCS_ASSERT(!glyph_index_to_glyphs.contains(glyph_index));
                        glyph_index_to_glyphs[glyph_index] = &glyph;
                    }
                }
            }

            // 2. 构建 unicode_default_glyphs
            FT_Select_Charmap(raw_face, FT_ENCODING_UNICODE); // 确保使用 Unicode 映射
            FT_UInt gindex;                                   // NOLINT
            FT_ULong charcode = FT_Get_First_Char(raw_face, &gindex);
            while (gindex != 0)
            {
                auto it = glyph_index_to_glyphs.find(gindex);
                if (it != glyph_index_to_glyphs.end())
                {
                    MCS_ASSERT(!unicode_default_glyphs.contains(charcode));
                    unicode_default_glyphs[charcode] = it->second;
                }
                charcode = FT_Get_Next_Char(raw_face, charcode, &gindex);
            }
        }

        [[nodiscard]] constexpr double getEmUnits() const noexcept
        {
            return static_cast<double>((*face)->units_per_EM);
        }
        // NOLINTBEGIN
        [[nodiscard]] constexpr bool has_glyph(uint32_t codepoint) const noexcept
        {
            return unicode_default_glyphs.contains(codepoint);
        }
        static constexpr bool contain_codepoint(const FontMetadata &meta_data,
                                                char32_t codepoint) noexcept
        {
            return hb_set_has(meta_data.unicode_set.get(), codepoint) != 0;
        }
        [[nodiscard]] constexpr static bool contain_lang(const FontMetadata &meta_data,
                                                         hb_tag_t language_tag) noexcept
        {
            return meta_data.lang_tags.contains(language_tag);
        }
        [[nodiscard]] constexpr static bool contain_script(const FontMetadata &meta_data,
                                                           hb_script_t script) noexcept
        {
            return meta_data.scripts.contains(script);
        }

        [[nodiscard]] constexpr bool contain_codepoint(char32_t codepoint) const noexcept
        {
            return contain_codepoint(meta_data, codepoint);
        }
        [[nodiscard]] constexpr bool contain_lang(hb_tag_t language_tag) const noexcept
        {
            return contain_lang(meta_data, language_tag);
        }
        [[nodiscard]] constexpr bool contain_script(hb_script_t script) const noexcept
        {
            return contain_script(meta_data, script);
        }
        // NOLINTEND
    };
}; // namespace mcs::vulkan::font