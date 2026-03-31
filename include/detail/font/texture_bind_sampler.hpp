#pragma once

#include <compare>
#include <cstdint>

namespace mcs::vulkan::font
{
    struct texture_bind_sampler // NOLINTBEGIN
    {
        uint32_t texture_index;
        uint32_t sampler_index;
        constexpr std::strong_ordering operator<=>(
            const texture_bind_sampler &) const noexcept = default;
    }; // NOLINTEND

}; // namespace mcs::vulkan::font