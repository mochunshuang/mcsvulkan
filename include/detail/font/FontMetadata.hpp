#pragma once

#include "__freetype_import.hpp"
#include "__harfbuzz_import.hpp"
#include <set>
#include <string>

#include "../utils/unique_handle.hpp"

namespace mcs::vulkan::font
{
    class FontMetadata
    {
      public:
        using unicode_set_type = unique_handle<hb_set_t *, [](hb_set_t *value) noexcept {
            hb_set_destroy(value);
        }>;

        FT_Long face_index{-1};
        std::string family_name;
        unicode_set_type unicode_set = unicode_set_type{hb_set_create()};
        std::set<hb_script_t> scripts;
        std::set<hb_tag_t> lang_tags; // NOLINTEND
    };
}; // namespace mcs::vulkan::font