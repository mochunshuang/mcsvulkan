#pragma once

#include "../__freetype_import.hpp"
#include <span>
#include <string>

namespace mcs::vulkan::font::freetype
{
    struct face
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
        face(const face &) = delete;
        face(face &&o) noexcept : value_{std::exchange(o.value_, {})} {}
        face &operator=(const face &) = delete;
        face &operator=(face &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        // a. From a Font File
        constexpr face(FT_Library library, const std::string &fontPath,
                       FT_Long face_index = 0)
        {
            /*
            face_index:
            某些字体格式允许在单个文件中嵌入多个字体。此索引告诉您要加载哪个面。
                         如果其值太大，则返回错误。不过，索引0总是有效的。
            face_: 指向设置为描述新face对象的句柄的指针。如果出错，它被设置为NULL
            */
            auto error = FT_New_Face(library, fontPath.data(), face_index, &value_);
            if (error == FT_Err_Unknown_File_Format)
                throw std::runtime_error{"the font file could be opened and read, but it "
                                         "appears that its font format is unsupported"};
            if (error != FT_Err_Ok)
                throw std::runtime_error{
                    "another error code means that the font file could "
                    "not be opened or read, or that it is broken"};
        }
        // b. From Memory
        // NOTE: 请注意，在调用FT_Done_Face之前，您不能释放字体文件缓冲区。
        constexpr face(FT_Library library, const std::span<const FT_Byte> &bufferView,
                       FT_Long face_index = 0)
        {
            auto error = FT_New_Memory_Face(
                library, bufferView.data(),              /* first byte in memory */
                static_cast<FT_Long>(bufferView.size()), /* size in bytes */
                face_index,                              /* face_index           */
                &value_);
            if (error != FT_Err_Ok)
                throw std::runtime_error{"init FT_Face From Memory error."};
        }
        // c. From Other Sources (Compressed Files, Network, etc.)

        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                FT_Done_Face(value_);
                value_ = nullptr;
            }
        }
        constexpr ~face() noexcept
        {
            destroy();
        }

      private:
        FT_Face value_{}; /* handle to face object */
    };
}; // namespace mcs::vulkan::font::freetype