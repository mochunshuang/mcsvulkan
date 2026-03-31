
#pragma once

#include "texture_bind_sampler.hpp"
#include <string>
#include <variant>
#include "../load/raw_stbi_image.hpp"

namespace mcs::vulkan::font
{
    struct texture_info
    {
        struct stbi_image_type
        {
            using type = load::raw_stbi_image;
            int image_format = STBI_rgb_alpha; // NOLINT
            std::string image_path;            // NOLINT
            constexpr auto operator<=>(const stbi_image_type &) const noexcept = default;
        };
        // NOLINTBEGIN
        texture_bind_sampler bind;
        std::variant<struct stbi_image_type> image_variant;
        // NOLINTEND

        constexpr auto operator<=>(const texture_info &) const noexcept = default;
    };

}; // namespace mcs::vulkan::font