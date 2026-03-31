#pragma once
#include "../__harfbuzz_import.hpp"
#include <string_view>

namespace mcs::vulkan::font::harfbuzz
{
    constexpr static hb_tag_t bcp47_to_tag(const std::string_view &bcp47) noexcept
    {
        if (bcp47.empty())
            return HB_TAG_NONE;
        hb_language_t lang =
            hb_language_from_string(bcp47.data(), static_cast<int>(bcp47.size()));
        if ((lang == nullptr) || lang == HB_LANGUAGE_INVALID)
            return HB_TAG_NONE;

        unsigned int count = 1;
        hb_tag_t tags[1]; // NOLINT
        hb_ot_tags_from_script_and_language(HB_SCRIPT_UNKNOWN, lang, nullptr, nullptr,
                                            &count, tags);
        // 如果没有生成有效标签，或生成的标签是默认语言标签（DFLT），则返回无匹配
        if (count == 0 || tags[0] == HB_OT_TAG_DEFAULT_LANGUAGE)
            return HB_TAG_NONE;
        return tags[0];
    }
}; // namespace mcs::vulkan::font::harfbuzz