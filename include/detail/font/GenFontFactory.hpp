#pragma once

#include "FontInfo.hpp"
#include "freetype/face.hpp"

#include <concepts>
#include <memory>
#include <vector>

namespace mcs::vulkan::font
{
    template <typename FontContext, typename MakeFontTexture>
        requires(requires(MakeFontTexture make, FontInfo info) {
            { make(info) } -> std::same_as<typename FontContext::texture_type>;
        })
    class GenFontFactory
    {

      public:
        using font_context_type = FontContext;
        using ft_face_type = freetype::face;
        constexpr GenFontFactory(MakeFontTexture make, FT_Library library) noexcept
            : make_{std::move(make)}, library_{library}
        {
        }

        const FontContext *make(FontInfo info)
        {
            using stbi_image_type = texture_info::stbi_image_type;
            const auto &registration = info.registration;
            if (std::holds_alternative<stbi_image_type>(
                    registration.texture_info.image_variant))
            {
                fonts_.emplace_back(std::make_unique<FontContext>(
                    registration.json_path, make_(info),
                    ft_face_type{library_, registration.font_path,
                                 info.meta_data.face_index},
                    registration.type, registration.texture_info.bind,
                    std::move(info.meta_data)));
                return fonts_.back().get();
            }
            return nullptr;
        }

      private:
        MakeFontTexture make_;
        FT_Library library_;
        std::vector<std::unique_ptr<FontContext>> fonts_;
    };
}; // namespace mcs::vulkan::font