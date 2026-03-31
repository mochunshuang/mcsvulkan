#pragma once

#include "FontType.hpp"
#include "texture_info.hpp"

namespace mcs::vulkan::font
{

    struct font_registration // NOLINTBEGIN
    {
        std::string font_path;
        std::string json_path;
        FontType type;
        texture_info texture_info;
        constexpr auto operator<=>(const font_registration &) const noexcept = default;
    }; // NOLINTEND

}; // namespace mcs::vulkan::font