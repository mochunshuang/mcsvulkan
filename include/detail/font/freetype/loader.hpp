#pragma once

#include "../__freetype_import.hpp"
#include "../../utils/make_vk_exception.hpp"
#include "../../utils/mcslog.hpp"
#include "version.hpp"

namespace mcs::vulkan::font::freetype
{
    struct loader
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
        [[nodiscard]] constexpr auto version() const noexcept
        {
            freetype::version ver{};
            ::FT_Library_Version(value_, &ver.major, &ver.minor, &ver.patch);
            return ver;
        }
        constexpr loader()
        {
            // https://freetype.org/freetype2/docs/reference/ft2-library_setup.html#ft_init_freetype
            auto error = FT_Init_FreeType(&value_);
            if (error != FT_Err_Ok)
                throw make_vk_exception(
                    "an error occurred during FT_Library initialization");
            auto ver = version();
            MCSLOG_INFO("freetype_loader: init library: {} freetype_version: {}.{}.{}",
                        static_cast<void *>(value_), ver.major, ver.minor, ver.patch);
        }
        loader(const loader &) = delete;
        constexpr loader(loader &&o) noexcept : value_{std::exchange(o.value_, {})} {}
        loader &operator=(const loader &) = delete;
        constexpr loader &operator=(loader &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                FT_Done_FreeType(value_);
                value_ = nullptr;
            }
        }
        constexpr ~loader() noexcept
        {
            destroy();
        }

      private:
        FT_Library value_{};
    };
}; // namespace mcs::vulkan::font::freetype