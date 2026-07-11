#pragma once

#include "FontInfo.hpp"
#include "freetype/face.hpp"

#include <concepts>
#include <memory>
#include <utility>
#include <vector>

namespace mcs::vulkan::font
{
    template <typename FontContext, typename MakeFontTexture>
        requires(requires(MakeFontTexture make, FontInfo &info) {
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
                auto bind_texture = make_(info);
                fonts_.emplace_back(std::make_unique<FontContext>(
                    registration.json_path, std::move(bind_texture),
                    ft_face_type{library_, registration.font_path,
                                 info.meta_data.face_index},
                    registration.type, registration.texture_info.bind,
                    std::move(info.meta_data)));
                return fonts_.back().get();
            }
            return nullptr;
        }

        template <typename FontFactory>
        friend class GenFontSelector;

      private:
        void removeFont(const FontContext *font)
        {
            if (auto it =
                    std::find_if(fonts_.begin(), fonts_.end(),
                                 [font](const auto &ptr) { return ptr.get() == font; });
                it != fonts_.end())
                fonts_.erase(it); // 释放 unique_ptr，即销毁 FontContext
        }
        MakeFontTexture make_;
        FT_Library library_;
        std::vector<std::unique_ptr<FontContext>> fonts_;
    };
}; // namespace mcs::vulkan::font