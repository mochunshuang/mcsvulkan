#pragma once

#include "FontContext.hpp"
#include "FontFactory.hpp"
#include <cassert>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "./harfbuzz/bcp47_to_tag.hpp"
#include "./harfbuzz/make_language_type.hpp"
#include "./harfbuzz/language_type.hpp"
#include "./harfbuzz/script_to_language.hpp"

#include "../utils/mcslog.hpp"

namespace mcs::vulkan::font
{
    struct select_result // NOLINTBEGIN
    {
        const FontContext *font{nullptr};
        hb_language_t language{nullptr};

        operator bool() const noexcept
        {
            return font != nullptr;
        }
    }; // NOLINTEND

    class FontSelector
    {
        // 辅助：在 selectable_ 中查找满足谓词的字体
        const FontContext *findInSelectable(auto &&pred) const noexcept
        {
            for (const auto *font : selectable_)
            {
                if (pred(font))
                    return font;
            }
            return nullptr;
        }

        // 辅助：在 candidate_ 中查找满足 info_pred 的字体，加载并验证 has_glyph
        const FontContext *loadFromCandidate(auto &&info_pred, char32_t codepoint)
        {
            for (auto it = candidate_.begin(); it != candidate_.end(); ++it)
            {
                if (info_pred(*it))
                    if (const auto *newFont = factory_->make(std::move(*it)); newFont)
                    {
                        selectable_.push_back(newFont);
                        candidate_.erase(it);
                        if (newFont->has_glyph(codepoint))
                            return newFont;
                    }
            }
            return nullptr;
        }

        select_result findOrLoad(std::optional<hb_tag_t> tag,
                                 std::optional<hb_script_t> script, char32_t codepoint)
        {
            // 构造 selectable 谓词：检查已加载字体
            auto selectable_pred = [&](const FontContext *font) -> bool {
                if (!font->has_glyph(codepoint))
                    return false;
                if (tag && !font->contain_lang(*tag))
                    return false;
                if (script && !font->contain_script(*script))
                    return false;
                // contain_codepoint 可省略，因为 has_glyph 更强
                return true;
            };

            // 构造 candidate 谓词：仅检查元数据（未加载）
            auto candidate_pred = [&](const FontInfo &info) -> bool {
                if (tag && !FontContext::contain_lang(info.meta_data, *tag))
                    return false;
                if (script && !FontContext::contain_script(info.meta_data, *script))
                    return false;
                if (!FontContext::contain_codepoint(info.meta_data, codepoint))
                    return false;
                return true;
            };

            // 1. 在已加载字体中查找
            if (const auto *font = findInSelectable(selectable_pred))
            {
                return {.font = font,
                        .language = tag ? harfbuzz::tag_to_language(*tag) : nullptr};
            }

            // 2. 在候选字体中查找并加载
            if (const auto *font = loadFromCandidate(candidate_pred, codepoint))
            {
                return {.font = font,
                        .language = tag ? harfbuzz::tag_to_language(*tag) : nullptr};
            }

            return {.font = nullptr, .language = nullptr};
        }

      public:
        // TODO(mcs): 可补充字体排序权重、缓存匹配结果、处理多语言回退顺序
        /*
        字体选择的核心是确定哪个字体能够提供该字符（码点）的合理字形，而具体字形形状、变体、合字等是通过
        HarfBuzz 的 shaping
        过程在选定字体后处理的。因此，将字形作为选择因素的必要性较低
        */
        [[nodiscard]] constexpr auto selectFont(char32_t codepoint, hb_script_t script)
            -> select_result
        {
            // 完全匹配
            if (auto ret = findOrLoad(preferenceTag(), script, codepoint))
                return ret;
            // 不限定包括语言
            if (auto ret = findOrLoad(std::nullopt, script, codepoint))
                return ret;
            // 不限定语言和脚本
            return findOrLoad(std::nullopt, std::nullopt, codepoint);
        }
        [[nodiscard]] constexpr auto operator()(char32_t codepoint, hb_script_t script)
            -> select_result
        {
            return selectFont(codepoint, script);
        }

        [[nodiscard]] static hb_language_t getFallbackLanguage(
            hb_script_t script, const FontContext *select_font) noexcept
        {
            if (select_font != nullptr && !select_font->meta_data.lang_tags.empty() &&
                select_font->meta_data.scripts.contains(script))
            {
                if (hb_language_t default_lang = harfbuzz::script_to_language(script);
                    default_lang != HB_LANGUAGE_INVALID)
                    for (hb_tag_t tag : select_font->meta_data.lang_tags)
                        if (default_lang == hb_ot_tag_to_language(tag))
                            return default_lang;

                MCSLOG_WARN("no default_lang in {} ", select_font->name);
                return hb_ot_tag_to_language(*select_font->meta_data.lang_tags.begin());
            }
            // 无字体支持该脚本时，使用 HarfBuzz 的默认语言
            return hb_language_get_default();
        }

        constexpr FontSelector(FontFactory *factory, hb_tag_t preference) noexcept
            : factory_{factory}, language_{harfbuzz::make_language_type(preference)}
        {
        }
        constexpr FontSelector(FontFactory *factory,
                               const std::string_view &bcp47) noexcept
            : FontSelector(factory, harfbuzz::bcp47_to_tag(bcp47))
        {
        }

        [[nodiscard]] hb_language_t preferenceLanguage() const noexcept
        {
            return language_.language;
        }

        // NOTE: 值语义更高效
        [[nodiscard]] std::string_view langBcp47() const noexcept
        {
            return language_.lang_bcp47;
        }

        [[nodiscard]] constexpr hb_tag_t preferenceTag() const noexcept
        {
            return language_.tag;
        }
        constexpr auto &&setPreferenceTag(hb_tag_t preferenceTag) noexcept
        {
            language_ = harfbuzz::make_language_type(preferenceTag);
            return *this;
        }
        // NOTE: 语言 地区
        constexpr auto &&setPreferenceLanguage(const std::string_view &bcp47) noexcept
        {
            language_ = harfbuzz::make_language_type(harfbuzz::bcp47_to_tag(bcp47));
            return std::move(*this);
        }
        constexpr auto &&load(FontInfo info)
        {
            const FontContext *newFont = factory_->make(std::move(info));
            MCS_ASSERT(newFont != nullptr);
            selectable_.emplace_back(newFont);
            return std::move(*this);
        }

        constexpr auto &&load(std::vector<FontInfo> infos)
        {
            for (auto &info : infos)
                load(std::move(info));
            return std::move(*this);
        }

        [[nodiscard]] constexpr const auto &candidate() const noexcept
        {
            return candidate_;
        }
        constexpr auto &&setCandidate(std::vector<FontInfo> candidate) noexcept
        {
            candidate_ = std::move(candidate);
            return std::move(*this);
        }

        [[nodiscard]] constexpr auto getTextureImageBases() const noexcept
        {
            return selectable_ |
                   std::views::transform([](const FontContext *atlas) noexcept
                                             -> const vma::texture_image_base & {
                       return atlas->texture.view();
                   });
        }
        constexpr void initNotdefFont() noexcept
        {
            for (const auto *c : selectable_)
            {
                if (c->glyph_index_to_glyphs.size() == 1 &&
                    c->glyph_index_to_glyphs.contains(0))
                {
                    notdefFont_ = c;
                    break;
                }
            }
        }

        [[nodiscard]] constexpr const FontContext *notdefFont() const noexcept
        {
            return notdefFont_;
        }

        constexpr void setNotdefFont(const FontContext *newFont) noexcept
        {
            notdefFont_ = newFont;
        }
        constexpr void setNotdefFont(int index) noexcept
        {
            notdefFont_ = selectable_[index];
            assert((notdefFont_->glyph_index_to_glyphs.contains(0)));
        }

      private:
        FontFactory *factory_;
        harfbuzz::language_type language_;
        std::vector<const FontContext *> selectable_; // NOTE: 已经排好序
        std::vector<FontInfo> candidate_;
        const FontContext *notdefFont_;
    };
}; // namespace mcs::vulkan::font