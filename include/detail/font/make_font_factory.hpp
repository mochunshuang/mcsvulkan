#pragma once

#include "GenFontContext.hpp"
#include "GenFontFactory.hpp"

namespace mcs::vulkan::font
{
    template <typename MakeFontTexture>
    constexpr static auto make_font_factory(MakeFontTexture make, FT_Library library)
    {
        using TextureType = decltype(make(std::declval<FontInfo &>()));
        return GenFontFactory<GenFontContext<TextureType>, MakeFontTexture>{
            std::move(make), library};
    }
}; // namespace mcs::vulkan::font