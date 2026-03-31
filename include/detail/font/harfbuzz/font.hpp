#pragma once
#include "../__harfbuzz_import.hpp"
#include "../../utils/make_vk_exception.hpp"

namespace mcs::vulkan::font::harfbuzz
{
    struct font
    {
        constexpr operator bool() const noexcept // NOLINT
        {
            return value_ != nullptr;
        }
        constexpr auto &operator*() noexcept
        {
            return value_;
        }
        constexpr const auto &operator*() const noexcept
        {
            return value_;
        }

        font() = default;
        // 从 FT_Face 创建 hb_font_t
        constexpr explicit font(FT_Face ft_face)
            : value_(hb_ft_font_create(ft_face, nullptr))
        {
            if (value_ == nullptr)
                throw make_vk_exception("hb_ft_font_create failed");
        }
        constexpr font(font &&o) noexcept : value_{std::exchange(o.value_, {})} {}
        constexpr font &operator=(font &&o) noexcept
        {
            if (this != &o)
            {
                destroy();
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        constexpr font(const font &) = delete;
        constexpr font &operator=(const font &) = delete;
        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                hb_font_destroy(value_);
                value_ = nullptr;
            }
        }
        constexpr ~font() noexcept
        {
            destroy();
        }

      private:
        hb_font_t *value_{};
    };
}; // namespace mcs::vulkan::font::harfbuzz