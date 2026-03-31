#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <filesystem>

#include "FontInfo.hpp"
#include "font_registration.hpp"
#include "freetype/face.hpp"
#include "freetype/loader.hpp"

#include "../utils/unique_handle.hpp"

#include "./harfbuzz/script_to_string.hpp"

namespace mcs::vulkan::font
{
    struct font_register
    {
      private:
        static constexpr auto getScriptTags(hb_face_t *hb_face, hb_tag_t table)
        {
            auto script_count =
                hb_ot_layout_table_get_script_tags(hb_face, table, 0, nullptr, nullptr);
            std::vector<hb_tag_t> script_tags(script_count);
            hb_ot_layout_table_get_script_tags(hb_face, table, 0, &script_count,
                                               script_tags.data());
            return script_tags;
        }
        constexpr static auto getLanguageTags(hb_face_t *hb_face, hb_tag_t table,
                                              unsigned int script_index)
        {
            unsigned lang_count = hb_ot_layout_script_get_language_tags(
                hb_face, table, script_index, 0, nullptr, nullptr);
            std::vector<hb_tag_t> lang_tags(lang_count);
            hb_ot_layout_script_get_language_tags(hb_face, table, script_index, 0,
                                                  &lang_count, lang_tags.data());
            return lang_tags;
        }

        static constexpr auto generateFontInfo(FT_Long face_index, FT_Face ft_face,
                                               hb_face_t *hb_face, font_registration reg)
        {
            FontInfo info{.registration = std::move(reg), .meta_data = {}};
            auto &meta_data = info.meta_data;
            meta_data.face_index = face_index;
            meta_data.family_name =
                (ft_face->family_name != nullptr) ? ft_face->family_name : "";
            hb_face_collect_unicodes(hb_face, meta_data.unicode_set.get());

            // 新增：建立脚本 -> 语言列表的映射
            std::unordered_map<hb_tag_t, std::vector<hb_tag_t>> script_to_languages;

            for (constexpr std::array TABLES = {HB_OT_TAG_GSUB, HB_OT_TAG_GPOS};
                 hb_tag_t table : TABLES)
            {
                std::vector<hb_tag_t> script_tags = getScriptTags(hb_face, table);

                // 收集脚本（原有逻辑）
                for (hb_tag_t tag : script_tags)
                    if (hb_script_t script = hb_ot_tag_to_script(tag);
                        script != HB_SCRIPT_INVALID)
                        meta_data.scripts.insert(script);

                // 新增：对每个脚本，记录其语言标签
                for (size_t i = 0, script_count = script_tags.size(); i < script_count;
                     ++i)
                {
                    auto lang_tags = getLanguageTags(hb_face, table, i);
                    auto &lang_vec = script_to_languages[script_tags[i]];
                    lang_vec.insert(lang_vec.end(), lang_tags.begin(), lang_tags.end());
                }

                // 原有语言收集（保持原有逻辑不变，用于整体语言列表）
                for (size_t i = 0, script_count = script_tags.size(); i < script_count;
                     ++i)
                    meta_data.lang_tags.insert_range(getLanguageTags(hb_face, table, i));
            }

            // ========== 原有打印 ==========
            std::println("Font: {}, face_index: {}", meta_data.family_name, face_index);
            std::string scripts_str;
            for (auto script : meta_data.scripts)
            {
                if (!scripts_str.empty())
                    scripts_str += ", ";
                scripts_str += harfbuzz::script_to_string(script);
            }
            std::println("  Scripts: {}", scripts_str);

            std::string langs_str;
            for (auto tag : meta_data.lang_tags)
            {
                if (!langs_str.empty())
                    langs_str += ", ";
                hb_language_t lang = hb_ot_tag_to_language(tag);
                langs_str += hb_language_to_string(lang);
            }
            std::println("  Languages: {}", langs_str);

            // ========== 新增：打印每个脚本对应的语言 ==========
            for (const auto &[script_tag, lang_tags] : script_to_languages)
            {
                hb_script_t script = hb_ot_tag_to_script(script_tag);
                std::string script_str = harfbuzz::script_to_string(script);

                // 对语言标签去重（因为同一个脚本可能在 GSUB 和 GPOS 中重复出现）
                std::unordered_set<hb_tag_t> unique_langs(lang_tags.begin(),
                                                          lang_tags.end());
                std::string langs_per_script;
                for (hb_tag_t lang_tag : unique_langs)
                {
                    hb_language_t lang = hb_ot_tag_to_language(lang_tag);
                    const char *lang_name = hb_language_to_string(lang);
                    if ((lang_name != nullptr) && (*lang_name != 0))
                    {
                        if (!langs_per_script.empty())
                            langs_per_script += ", ";
                        langs_per_script += lang_name;
                    }
                }

                if (!langs_per_script.empty())
                    std::println("  Script: {} -> Languages: {}", script_str,
                                 langs_per_script);
                else
                    std::println("  Script: {} -> Languages: (none)", script_str);
            }

            return info;
        }

      public:
        constexpr static std::vector<FontInfo> makeFontInfos(
            const freetype::loader &library,
            const std::vector<font_registration> &registrations) // NOTE: std::set 是BUG
        {
            using HB_Face_Ptr =
                unique_handle<hb_face_t *, [](hb_face_t *value) constexpr noexcept {
                    hb_face_destroy(value);
                }>;

            std::vector<FontInfo> fonts;
            for (const font_registration &reg : registrations)
            {
                const auto &file_path = reg.font_path;
                if (not std::filesystem::exists(file_path))
                {
                    MCSLOG_WARN("not exists: {}", file_path);
                    continue;
                }
                FT_Long face_index = 0;
                while (true)
                {
                    try
                    {
                        auto face = freetype::face(*library, file_path, face_index);
                        FT_Face ft_face = *face;
                        if (HB_Face_Ptr hb_face =
                                HB_Face_Ptr{hb_ft_face_create(ft_face, nullptr)};
                            hb_face)
                        {
                            fonts.emplace_back(
                                generateFontInfo(face_index, ft_face, *hb_face, reg));
                            MCSLOG_INFO(
                                "make FontInfos ok! file_path: {}, face_index: {}",
                                file_path, face_index);
                            ++face_index;

                            // TODO(mcs): msdf-atlas-gen 默认仅仅生成face_index==0的
                            break;
                        }
                    }
                    catch (...)
                    {
                        break;
                    }
                }
            }
            return fonts;
        }
    };
}; // namespace mcs::vulkan::font