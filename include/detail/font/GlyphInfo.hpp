#pragma once

#include "FontContext.hpp"

namespace mcs::vulkan::font
{
    class GlyphInfo
    {
      public:
        using Bounds = Font::Bounds;
        GlyphInfo() = default;
        constexpr GlyphInfo(Bounds uv_bounds, const FontContext *font_ctx,
                            const Font::glyphs_type *glyph) noexcept
            : uvBounds_{uv_bounds}, fontCtx_{font_ctx}, glyph_{glyph}
        {
        }

        [[nodiscard]] constexpr auto &type() const noexcept
        {
            return fontCtx_->type;
        }

        // NOLINTBEGIN
        [[nodiscard]] constexpr auto &texture_index() const noexcept
        {
            return fontCtx_->bind.texture_index;
        }
        [[nodiscard]] constexpr auto &sampler_index() const noexcept
        {
            return fontCtx_->bind.sampler_index;
        }
        [[nodiscard]] constexpr auto &font() const noexcept
        {
            return fontCtx_->font;
        }
        [[nodiscard]] constexpr Bounds plane_bounds() const noexcept
        {
            return glyph_->planeBounds.value_or(Bounds{0, 0, 0, 0});
        }
        [[nodiscard]] constexpr double advance() const noexcept
        {
            return glyph_->advance;
        }

        constexpr static GlyphInfo make(const Font::glyphs_type &glyph,
                                        const FontContext &atlas) noexcept
        {
            auto atlasBounds = glyph.atlasBounds;
            if (!atlasBounds)
                // 空格等
                return GlyphInfo{
                    {.left = 0, .bottom = 0, .right = 0, .top = 0}, &atlas, &glyph};

            const auto &font = atlas.font;
            double w = font.atlas.width;
            double h = font.atlas.height;
            if (atlas.font.atlas.yOrigin == "bottom")
            {
                // Y_UPWARD: 需要翻转
                auto left = atlasBounds->left / w;
                auto bottom = (h - atlasBounds->bottom) / h;
                auto right = atlasBounds->right / w;
                auto top = (h - atlasBounds->top) / h;
                return GlyphInfo{
                    {.left = left, .bottom = bottom, .right = right, .top = top},
                    &atlas,
                    &glyph};
            }

            // "top"
            // Y_DOWNWARD: 无需翻转，直接使用原始 bounds（已交换）
            auto left = atlasBounds->left / w;
            auto bottom = atlasBounds->bottom / h;
            auto right = atlasBounds->right / w;
            auto top = atlasBounds->top / h;
            return GlyphInfo{{.left = left, .bottom = bottom, .right = right, .top = top},
                             &atlas,
                             &glyph};
        }

        [[nodiscard]] constexpr const FontContext *font_ctx() const noexcept
        {
            return fontCtx_;
        }

        [[nodiscard]] constexpr Bounds uv_bounds() const noexcept
        {
            return uvBounds_;
        }

      private:
        Bounds uvBounds_;
        const FontContext *fontCtx_;
        const Font::glyphs_type *glyph_;
    }; // NOLINTEND
}; // namespace mcs::vulkan::font