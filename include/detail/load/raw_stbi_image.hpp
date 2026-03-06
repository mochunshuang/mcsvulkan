#pragma once

#include "../__stb_import.hpp"
#include "../utils/make_vk_exception.hpp"
#include "../utils/mcs_assert.hpp"

#include "image_source.hpp"

namespace mcs::vulkan::load
{
    struct raw_stbi_image
    {
        raw_stbi_image() = default;
        constexpr explicit raw_stbi_image(const char *filename,
                                          int req_comp = STBI_rgb_alpha)
        {
            stbi_uc *pixels =
                ::stbi_load(filename, &width_, &height_, &channels_, req_comp);
            if (pixels == nullptr)
                throw make_vk_exception("failed to load texture image!");

            data_ = pixels;
            static_assert(STBI_rgb_alpha == 4); // 4个通道
            imageSize_ = width_ * height_ * req_comp;

            MCS_ASSERT(width_ != 0);
            MCS_ASSERT(height_ != 0);
        }
        constexpr void destroy() noexcept
        {
            if (data_ != nullptr)
                ::stbi_image_free(data_);
        }
        constexpr ~raw_stbi_image() noexcept
        {
            destroy();
        }

        constexpr raw_stbi_image(raw_stbi_image &&other) noexcept
            : data_{std::exchange(other.data_, {})},
              width_{std::exchange(other.width_, {})},
              height_{std::exchange(other.height_, {})},
              channels_{std::exchange(other.channels_, {})},
              imageSize_{std::exchange(other.imageSize_, {})} {};

        constexpr raw_stbi_image &operator=(raw_stbi_image &&other) noexcept
        {
            if (&other != this)
            {
                this->destroy();
                data_ = std::exchange(other.data_, {});
                width_ = std::exchange(other.width_, {});
                height_ = std::exchange(other.height_, {});
                channels_ = std::exchange(other.channels_, {});
                imageSize_ = std::exchange(other.imageSize_, {});
            }
            return *this;
        }
        raw_stbi_image(const raw_stbi_image &) = delete;
        raw_stbi_image &operator=(const raw_stbi_image &) = delete;

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return data_ != nullptr;
        }

        [[nodiscard]] constexpr int width() const noexcept
        {
            return width_;
        }

        [[nodiscard]] constexpr int height() const noexcept
        {
            return height_;
        }

        [[nodiscard]] constexpr int channels() const noexcept
        {
            return channels_;
        }

        [[nodiscard]] constexpr std::size_t size() const noexcept
        {
            return imageSize_;
        }

        [[nodiscard]] constexpr auto *data() const noexcept
        {
            return data_;
        }

      private:
        stbi_uc *data_ = nullptr;
        int width_{};
        int height_{};
        int channels_{};
        std::size_t imageSize_{};
    };

    static_assert(image_source<raw_stbi_image>);

}; // namespace mcs::vulkan::load