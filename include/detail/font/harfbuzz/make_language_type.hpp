#pragma once

#include "language_type.hpp"
#include "tag_to_language.hpp"

namespace mcs::vulkan::font::harfbuzz
{
    static constexpr language_type make_language_type(hb_tag_t tag) noexcept
    {
        hb_language_t language = tag_to_language(tag);
        return {.tag = tag,
                .language = language,
                .lang_bcp47 = hb_language_to_string(language)};
    }
}; // namespace mcs::vulkan::font::harfbuzz