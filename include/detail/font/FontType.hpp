#pragma once

#include <cstdint>

namespace mcs::vulkan::font
{
    enum class FontType : uint8_t
    {
        eHARD_MASK,
        eSOFT_MASK,
        eSDF,
        ePSDF,
        eMSDF,
        eMTSDF,
        eBITMAP,
    };
} // namespace mcs::vulkan::font
