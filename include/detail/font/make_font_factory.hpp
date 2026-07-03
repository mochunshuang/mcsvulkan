#pragma once

#include "GenFontContext.hpp"
#include "GenFontFactory.hpp"

namespace mcs::vulkan::font
{
    template <typename MakeFontTexture>
    constexpr static auto make_font_factory(MakeFontTexture make, FT_Library library)
        -> GenFontFactory<GenFontContext<decltype(make(std::declval<FontInfo>()))>,
                          MakeFontTexture>
    {
        return {std::move(make), library};
    }
}; // namespace mcs::vulkan::font