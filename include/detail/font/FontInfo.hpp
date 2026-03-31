#pragma once

#include <set>
#include "font_registration.hpp"
#include "FontMetadata.hpp"

namespace mcs::vulkan::font
{
    class FontInfo
    {
      public: // NOLINTBEGIN
        font_registration registration;
        FontMetadata meta_data; // NOLINTEND
    };
}; // namespace mcs::vulkan::font