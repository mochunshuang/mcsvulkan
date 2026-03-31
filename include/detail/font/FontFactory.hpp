#pragma once

#include "FontContext.hpp"
#include "FontInfo.hpp"
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "./freetype/face.hpp"
#include "font_registration.hpp"

namespace mcs::vulkan::font
{
    class FontFactory
    {
      public:
        constexpr FontFactory(FontAllocationContext allocator,
                              FT_Library library) noexcept
            : allocator_{allocator}, library_{library}
        {
        }

        using value_type = std::unique_ptr<FontContext>;
        const FontContext *make(FontInfo info)
        {
            using stbi_image_type = texture_info::stbi_image_type;
            const auto &registration = info.registration;
            if (std::holds_alternative<stbi_image_type>(
                    registration.texture_info.image_variant))
            {
                using image_type = stbi_image_type::type;
                const auto &image =
                    std::get<stbi_image_type>(registration.texture_info.image_variant);
                fonts_.emplace_back(std::make_unique<FontContext>(
                    registration.json_path,
                    FontTexture::msdf_info{
                        .image = image_type{image.image_path.data(), image.image_format},
                        .allocation = allocator_},
                    freetype::face{library_, registration.font_path,
                                   info.meta_data.face_index},
                    registration.type, registration.texture_info.bind,
                    std::move(info.meta_data)));
                return fonts_.back().get();
            }
            return nullptr;
        }

      private:
        std::vector<value_type> fonts_;
        FontAllocationContext allocator_;
        FT_Library library_;
    };

}; // namespace mcs::vulkan::font