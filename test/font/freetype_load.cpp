
#include <algorithm>
#include <bit>
#include <cassert>
#include <cuchar>
#include <iostream>
#include <exception>

// https://freetype.org/freetype2/docs/tutorial/step1.html
// diff: 1. Header Files
#include <ft2build.h>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include FT_FREETYPE_H

// 2. Library Initialization
struct freetype_version
{
    int major;
    int minor;
    int patch;
};
template <>
struct std::formatter<freetype_version>
{
    static constexpr auto parse(format_parse_context &ctx)
    {
        return ctx.begin();
    }
    static auto format(const freetype_version &ver, format_context &ctx)
    {
        return std::format_to(ctx.out(), "freetype_version: {}.{}.{}", ver.major,
                              ver.minor, ver.patch);
    }
};
struct freetype_loader
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
    [[nodiscard]] auto version() const noexcept
    {
        freetype_version ver{};
        FT_Library_Version(value_, &ver.major, &ver.minor, &ver.patch);
        return ver;
    }
    constexpr freetype_loader()
    {
        // https://freetype.org/freetype2/docs/reference/ft2-library_setup.html#ft_init_freetype
        auto error = FT_Init_FreeType(&value_);
        if (error != FT_Err_Ok)
            throw std::runtime_error{
                "an error occurred during FT_Library initialization"};
        std::println("freetype_loader: init library: {} {}", static_cast<void *>(value_),
                     version());
    }
    freetype_loader(const freetype_loader &) = delete;
    constexpr freetype_loader(freetype_loader &&o) noexcept
        : value_{std::exchange(o.value_, {})}
    {
    }
    freetype_loader &operator=(const freetype_loader &) = delete;
    freetype_loader &operator=(freetype_loader &&o) noexcept
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
    constexpr ~freetype_loader() noexcept
    {
        destroy();
    }

  private:
    FT_Library value_{};
};
// 3. Loading a Font Face
struct freetype_face
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
    freetype_face(const freetype_face &) = delete;
    freetype_face(freetype_face &&o) noexcept : value_{std::exchange(o.value_, {})} {}
    freetype_face &operator=(const freetype_face &) = delete;
    freetype_face &operator=(freetype_face &&o) noexcept
    {
        if (&o != this)
        {
            destroy();
            value_ = std::exchange(o.value_, {});
        }
        return *this;
    }
    // a. From a Font File
    freetype_face(FT_Library library, const std::string &fontPath, FT_Long face_index = 0)
    {
        /*
        face_index:  某些字体格式允许在单个文件中嵌入多个字体。此索引告诉您要加载哪个面。
                     如果其值太大，则返回错误。不过，索引0总是有效的。
        face_: 指向设置为描述新face对象的句柄的指针。如果出错，它被设置为NULL
        */
        auto error = FT_New_Face(library, fontPath.data(), face_index, &value_);
        if (error == FT_Err_Unknown_File_Format)
            throw std::runtime_error{"the font file could be opened and read, but it "
                                     "appears that its font format is unsupported"};
        if (error != FT_Err_Ok)
            throw std::runtime_error{"another error code means that the font file could "
                                     "not be opened or read, or that it is broken"};
    }
    // b. From Memory
    // NOTE: 请注意，在调用FT_Done_Face之前，您不能释放字体文件缓冲区。
    freetype_face(FT_Library library, const std::span<const FT_Byte> &bufferView,
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
    constexpr ~freetype_face() noexcept
    {
        destroy();
    }

  private:
    FT_Face value_{}; /* handle to face object */
};

// 字体路径（请根据您的系统修改）
constexpr auto *UNICODE_FONT_PATH = "C:/Windows/Fonts/msyh.ttc";   // 中文字体
constexpr auto *EMOJI_FONT_PATH = "C:/Windows/Fonts/seguiemj.ttf"; // 彩色表情字体

constexpr auto INPUT_TEXT = "Hello, 世界! こんにちは 123 😀👍🎉";

// 4. Accessing the Face Data
static void AccessingtheFaceData(FT_Face ftFace)
{
    // https://freetype.org/freetype2/docs/reference/ft2-face_creation.html#ft_facerec
    static_assert(std::is_same_v<FT_Face, FT_FaceRec_ *>);

    // 1. 访问基本字段.获取 FT_Face 后，可以直接访问其成员。注意检查某些字段的有效性（例如
    // units_per_EM 只对可缩放字体有意义）
    {
        // 打印基本属性
        std::println("Number of glyphs: {}", ftFace->num_glyphs);
        std::println("Face flags: 0x{:X}", ftFace->face_flags);
        std::println("Units per EM: {}", ftFace->units_per_EM);
        std::println("Number of fixed sizes (bitmap strikes): {}",
                     ftFace->num_fixed_sizes);
    }
    // 2. 检查 face_flags 中的标志位.
    // face_flags 是一个位掩码，可以通过预定义的宏（如
    // FT_FACE_FLAG_SCALABLE）来判断是否支持某些特性
    {
        if ((ftFace->face_flags & FT_FACE_FLAG_SCALABLE) != 0)
            std::println("Font is scalable");
        else
            std::println("Font is not scalable (bitmap only)");
    }
    // 3. 遍历 embedded bitmap strikes
    // 如果 num_fixed_sizes > 0，则可以通过 available_sizes 数组访问每个 bitmap strike
    // 的尺寸信息。available_sizes 是一个 FT_Bitmap_Size 数组指针，每个元素描述了该 strike
    // 的宽度、高度等信息
    {
        if (ftFace->num_fixed_sizes > 0)
        {
            std::println("Embedded bitmap strikes:");
            for (int i = 0; i < ftFace->num_fixed_sizes; ++i)
            {
                const FT_Bitmap_Size &strike = ftFace->available_sizes[i];
                std::println(
                    "  Strike {}: width = {}, height = {}, x_ppem = {}, y_ppem = {}", i,
                    strike.width,   // 字符图像的最大宽度（像素）
                    strike.height,  // 字符图像的最大高度（像素）
                    strike.x_ppem,  // 水平每 EM 像素数（26.6 定点数，通常直接使用）
                    strike.y_ppem); // 垂直每 EM 像素数
            }
        }
        // 注意：FT_Bitmap_Size 中的 width 和 height
        // 是字符图像的最大尺寸，并不一定等于单元格尺寸。如果需要精确的度量信息，通常需要根据具体字符的
        // FT_Glyph_Metrics 来获取。
    }
}

#include <fstream>
// 将 FT_Bitmap 保存为二进制 PGM 文件
void save_bitmap_as_pgm(const FT_Bitmap &bitmap, const std::string &filename)
{
    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
    {
        throw std::runtime_error("Only 8-bit gray bitmaps are supported for PGM");
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Cannot open file for writing");
    }

    // 写入 PGM 头
    file << "P5\n" << bitmap.width << " " << bitmap.rows << "\n255\n";

    // 写入像素数据
    // 注意：bitmap.buffer 每行可能有填充（pitch 可能大于 width），
    // 需要逐行写入 width 字节，跳过填充。
    for (unsigned int y = 0; y < bitmap.rows; ++y)
    {
        const unsigned char *row = bitmap.buffer + y * bitmap.pitch;
        file.write(std::bit_cast<const char *>(row), bitmap.width);
    }
}

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

/**
 * 将 FreeType 的 FT_Bitmap 保存为 BMP 文件。
 * 支持所有像素模式，并自动转换为合适的 BMP 格式。
 * @param bitmap FT_Bitmap 结构（来自 FreeType 渲染）
 * @param filename 输出 BMP 文件路径
 * @throws std::runtime_error 如果像素模式不支持或文件操作失败
 */
void save_ft_bitmap_as_bmp(const FT_Bitmap &bitmap, const std::string &filename)
{
    int width = static_cast<int>(bitmap.width);
    int height = static_cast<int>(bitmap.rows);
    int pitch = bitmap.pitch; // FT_Bitmap 中每行的字节数（可能包含填充）

    // 确定 BMP 的位深度（bpp）和调色板
    uint16_t bpp = 0;                    // bits per pixel
    std::vector<uint8_t> palette;        // 调色板数据（BGR格式，每项4字节）
    std::vector<uint8_t> converted_data; // 用于存放转换后的像素数据（仅在需要转换时使用）
    const uint8_t *src_data = bitmap.buffer; // 源数据指针

    switch (bitmap.pixel_mode)
    {
    case FT_PIXEL_MODE_MONO: {
        // 1位单色 -> BMP 1位
        bpp = 1;
        // 调色板：索引0=黑，索引1=白 (BGR顺序)
        palette = {
            0,   0,   0,   0, // 黑色 (B,G,R,保留)
            255, 255, 255, 0  // 白色
        };
        // 注意：FT_PIXEL_MODE_MONO 每像素1位，每个字节包含8个像素，MSB在前
        // BMP 1位也是每像素1位，但每行必须对齐到32位（我们会在写入时处理对齐）
        src_data = bitmap.buffer;
        break;
    }

    case FT_PIXEL_MODE_GRAY2: {
        // 2位灰度 -> 扩展为8位灰度保存，以便更通用（也可选择保存为4位BMP，但兼容性差）
        bpp = 8;
        // 创建256级灰度调色板
        palette.resize(256 * 4);
        for (int i = 0; i < 256; ++i)
        {
            palette[i * 4 + 0] = i; // B
            palette[i * 4 + 1] = i; // G
            palette[i * 4 + 2] = i; // R
            palette[i * 4 + 3] = 0; // 保留
        }
        // 将2位数据扩展为8位：每个像素2位，需要转换为0-255灰度值
        // 2位像素值范围0-3，映射到0-255
        converted_data.resize(width * height);
        for (int y = 0; y < height; ++y)
        {
            const uint8_t *src_row = bitmap.buffer + y * pitch;
            uint8_t *dst_row = converted_data.data() + y * width;
            for (int x = 0; x < width; ++x)
            {
                int byte_idx = x / 4;            // 每字节包含4个像素
                int bit_shift = 6 - 2 * (x % 4); // 高位在前（MSB）
                uint8_t val = (src_row[byte_idx] >> bit_shift) & 0x03;
                dst_row[x] = static_cast<uint8_t>((val * 255) / 3); // 映射到0-255
            }
        }
        src_data = converted_data.data();
        pitch = width; // 新的每行字节数（无填充）
        break;
    }

    case FT_PIXEL_MODE_GRAY4: {
        // 4位灰度 -> 扩展为8位灰度保存
        bpp = 8;
        palette.resize(256 * 4);
        for (int i = 0; i < 256; ++i)
        {
            palette[i * 4 + 0] = i;
            palette[i * 4 + 1] = i;
            palette[i * 4 + 2] = i;
            palette[i * 4 + 3] = 0;
        }
        // 将4位数据扩展为8位：每个像素4位，映射到0-255
        converted_data.resize(width * height);
        for (int y = 0; y < height; ++y)
        {
            const uint8_t *src_row = bitmap.buffer + y * pitch;
            uint8_t *dst_row = converted_data.data() + y * width;
            for (int x = 0; x < width; ++x)
            {
                int byte_idx = x / 2; // 每字节包含2个像素
                int nibble =
                    (x % 2 == 0) ? (src_row[byte_idx] >> 4) : (src_row[byte_idx] & 0x0F);
                dst_row[x] = static_cast<uint8_t>((nibble * 255) / 15);
            }
        }
        src_data = converted_data.data();
        pitch = width;
        break;
    }

    case FT_PIXEL_MODE_GRAY: {
        // 8位灰度 -> BMP 8位（带灰度调色板）
        bpp = 8;
        palette.resize(256 * 4);
        for (int i = 0; i < 256; ++i)
        {
            palette[i * 4 + 0] = i;
            palette[i * 4 + 1] = i;
            palette[i * 4 + 2] = i;
            palette[i * 4 + 3] = 0;
        }
        src_data = bitmap.buffer; // 直接使用原始数据
        break;
    }

    case FT_PIXEL_MODE_LCD: {
        // 水平LCD子像素：每个像素3个8位通道（RGB顺序），宽度是原宽度的3倍
        // 保存为24位RGB BMP
        bpp = 24;
        // 24位BMP不需要调色板
        // FT_PIXEL_MODE_LCD 的数据格式是：每个子像素一个字节，从左到右为RGBRGB...
        // 直接使用原始数据即可，但需要注意BMP要求每行对齐到4字节
        src_data = bitmap.buffer;
        break;
    }

    case FT_PIXEL_MODE_LCD_V: {
        // 垂直LCD子像素：每个像素3个8位通道，但排列方向不同，通常需要转置为水平
        // 为简化，这里将垂直子像素当作三个独立的灰度通道，保存为24位RGB，但可能会扭曲
        // 更正确的做法是转置，但为了保持简单，我们将其作为24位处理，并警告用户
        bpp = 24;
        // 注意：FT_PIXEL_MODE_LCD_V 的宽度是原宽度的3倍，但高度不变，数据是3个水平条带
        // 如果直接写入，图像会变形。这里选择抛异常，让用户决定是否处理
        throw std::runtime_error(
            "FT_PIXEL_MODE_LCD_V requires transposition; not implemented");
    }

    case FT_PIXEL_MODE_BGRA: {
        // 32位彩色（BGRA） -> BMP 32位（BGRX）
        bpp = 32;
        // 32位BMP不需要调色板，数据顺序也是BGRA，但BMP忽略alpha或保留为保留字节
        src_data = bitmap.buffer;
        break;
    }

    default:
        throw std::runtime_error("Unsupported FT_Pixel_Mode: " +
                                 std::to_string(bitmap.pixel_mode));
    }

    // 计算BMP每行字节数（必须为4的倍数）
    int stride = ((width * bpp + 31) / 32) * 4;
    int data_size = stride * height;

    // 调色板大小（字节）
    int palette_size = static_cast<int>(palette.size());
    // 文件头和信息头大小
    const uint32_t file_header_size = 14;
    const uint32_t info_header_size = 40;
    uint32_t pixel_data_offset = file_header_size + info_header_size + palette_size;
    uint32_t file_size = pixel_data_offset + data_size;

    // 打开文件（二进制模式）
    std::ofstream file(filename, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Cannot open file for writing: " + filename);
    }

    // ---- 1. 写入位图文件头 BITMAPFILEHEADER ----
    uint16_t bfType = 0x4D42; // "BM"
    uint32_t bfSize = file_size;
    uint16_t bfReserved1 = 0;
    uint16_t bfReserved2 = 0;
    uint32_t bfOffBits = pixel_data_offset;

    file.write(reinterpret_cast<const char *>(&bfType), 2);
    file.write(reinterpret_cast<const char *>(&bfSize), 4);
    file.write(reinterpret_cast<const char *>(&bfReserved1), 2);
    file.write(reinterpret_cast<const char *>(&bfReserved2), 2);
    file.write(reinterpret_cast<const char *>(&bfOffBits), 4);

    // ---- 2. 写入位图信息头 BITMAPINFOHEADER ----
    uint32_t biSize = info_header_size;
    int32_t biWidth = width;
    int32_t biHeight =
        -height; // 负值表示自上而下（BMP默认自下而上，但FreeType是自上而下，所以取负）
    uint16_t biPlanes = 1;
    uint16_t biBitCount = bpp;
    uint32_t biCompression = 0; // BI_RGB (无压缩)
    uint32_t biSizeImage = data_size;
    int32_t biXPelsPerMeter = 2835; // 72 DPI ≈ 2835 像素/米
    int32_t biYPelsPerMeter = 2835;
    uint32_t biClrUsed = (bpp <= 8) ? (palette.size() / 4) : 0;
    uint32_t biClrImportant = 0;

    file.write(reinterpret_cast<const char *>(&biSize), 4);
    file.write(reinterpret_cast<const char *>(&biWidth), 4);
    file.write(reinterpret_cast<const char *>(&biHeight), 4);
    file.write(reinterpret_cast<const char *>(&biPlanes), 2);
    file.write(reinterpret_cast<const char *>(&biBitCount), 2);
    file.write(reinterpret_cast<const char *>(&biCompression), 4);
    file.write(reinterpret_cast<const char *>(&biSizeImage), 4);
    file.write(reinterpret_cast<const char *>(&biXPelsPerMeter), 4);
    file.write(reinterpret_cast<const char *>(&biYPelsPerMeter), 4);
    file.write(reinterpret_cast<const char *>(&biClrUsed), 4);
    file.write(reinterpret_cast<const char *>(&biClrImportant), 4);

    // ---- 3. 写入调色板（如果有） ----
    if (!palette.empty())
    {
        file.write(reinterpret_cast<const char *>(palette.data()), palette.size());
    }

    // ---- 4. 写入像素数据 ----
    // BMP要求每行对齐到4字节，我们已经计算了stride。
    // 对于大多数情况，src_data 的行长度（pitch）可能不等于 stride，
    // 需要逐行复制并填充对齐字节。
    std::vector<uint8_t> row_buffer(stride, 0); // 用于对齐的临时缓冲区

    for (int y = 0; y < height; ++y)
    {
        const uint8_t *src_row = src_data + y * pitch;
        uint8_t *dst_row = row_buffer.data();

        // 计算源行有效字节数（以字节为单位的宽度，注意bpp不同）
        int src_row_bytes = (width * bpp + 7) / 8; // 实际像素数据占用的字节数

        // 复制源行数据到行缓冲区
        std::memcpy(dst_row, src_row, src_row_bytes);

        // 写入行缓冲区（自动包含尾部填充）
        file.write(reinterpret_cast<const char *>(dst_row), stride);
    }
}

std::vector<char32_t> utf8_to_utf32(const std::string &utf8)
{
    std::vector<char32_t> result;
    std::mbstate_t state{}; // 初始转换状态
    const char *ptr = utf8.data();
    const char *end = utf8.data() + utf8.size();

    while (ptr < end)
    {
        char32_t cp;
        std::size_t rc = std::mbrtoc32(&cp, ptr, end - ptr, &state);
        if (rc == static_cast<std::size_t>(-1))
        {
            // 编码错误，跳过
            ptr++;
            continue;
        }
        if (rc == static_cast<std::size_t>(-3))
        {
            // 上一个字符的后续字节，已处理，继续
            continue;
        }
        if (rc == static_cast<std::size_t>(-2))
        {
            // 不完整的序列，但我们的字符串是完整的，不会出现
            break;
        }
        // 成功转换一个字符
        result.push_back(cp);
        ptr += rc;
    }
    return result;
}
void render_text_line(FT_Face face, const std::string &utf8_text, int font_size_px,
                      const std::string &output_filename)
{
    // 1. 将 UTF-8 转换为 UTF-32 码点序列
    std::vector<char32_t> codepoints = utf8_to_utf32(utf8_text);

    // 2. 设置字号
    FT_Set_Pixel_Sizes(face, 0, font_size_px);

    // 第一遍：收集每个字符的度量，计算画布尺寸
    struct CharInfo
    {
        FT_Bitmap bitmap;
        int bitmap_left;
        int bitmap_top;
        int advance_x;                    // 像素
        std::vector<uint8_t> bitmap_data; // 保存位图副本
    };
    std::vector<CharInfo> chars;

    int total_advance = 0;
    int max_ascent = 0, max_descent = 0;
    int pen_x = 0;

    for (char32_t cp : codepoints)
    {
        FT_UInt glyph_index = FT_Get_Char_Index(face, static_cast<FT_ULong>(cp));
        if (glyph_index == 0)
        {
            std::println("Glyph not found for U+{:X}", static_cast<uint32_t>(cp));
            continue;
        }

        FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);

        FT_GlyphSlot slot = face->glyph;
        FT_Bitmap &bitmap = slot->bitmap;

        // 保存度量
        CharInfo info;
        info.bitmap_left = slot->bitmap_left;
        info.bitmap_top = slot->bitmap_top;
        info.advance_x = slot->advance.x >> 6;

        // 拷贝位图数据
        info.bitmap_data.assign(bitmap.buffer,
                                bitmap.buffer + bitmap.rows * bitmap.pitch);
        info.bitmap = bitmap;
        info.bitmap.buffer = info.bitmap_data.data();
        info.bitmap.pitch = bitmap.pitch; // 保持原pitch

        chars.push_back(std::move(info));

        // 更新升降
        int ascent = info.bitmap_top;
        int descent = static_cast<int>(bitmap.rows) - info.bitmap_top;
        max_ascent = std::max(ascent, max_ascent);
        max_descent = std::max(descent, max_descent);

        total_advance += info.advance_x;
    }

    if (chars.empty())
    {
        std::println("No characters rendered.");
        return;
    }

    // 确定画布大小
    int canvas_width = total_advance + 20;             // 留左右边距
    int canvas_height = max_ascent + max_descent + 20; // 留上下边距
    int baseline_y = max_ascent + 10;                  // 基线位置，留出上边距

    // 创建画布（灰度图）
    std::vector<uint8_t> canvas(canvas_width * canvas_height, 0);

    // 第二遍：将字形绘制到画布
    pen_x = 10; // 左边距
    for (const auto &info : chars)
    {
        const FT_Bitmap &bitmap = info.bitmap;
        int x = pen_x + info.bitmap_left;
        int y = baseline_y - info.bitmap_top;

        // 将位图拷贝到画布（只处理灰度）
        if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
        {
            for (unsigned int row = 0; row < bitmap.rows; ++row)
            {
                const uint8_t *src = bitmap.buffer + row * bitmap.pitch;
                if (y + row < 0 || y + row >= canvas_height)
                    continue;
                uint8_t *dst = canvas.data() + (y + row) * canvas_width;
                for (unsigned int col = 0; col < bitmap.width; ++col)
                {
                    int dst_x = x + col;
                    if (dst_x >= 0 && dst_x < canvas_width)
                    {
                        dst[dst_x] = src[col];
                    }
                }
            }
        }
        pen_x += info.advance_x;
    }

    // 保存画布为 BMP
    FT_Bitmap canvas_bitmap;
    canvas_bitmap.rows = canvas_height;
    canvas_bitmap.width = canvas_width;
    canvas_bitmap.pitch = canvas_width;
    canvas_bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    canvas_bitmap.buffer = canvas.data();
    save_ft_bitmap_as_bmp(canvas_bitmap, output_filename);
    std::println("Saved rendered text to {}", output_filename);
}

// 在画布上画矩形边框（灰度图，颜色为 255）
void draw_rect(std::vector<uint8_t> &canvas, int canvas_width, int canvas_height, int x1,
               int y1, int x2, int y2, uint8_t color = 255)
{
    // 裁剪到画布范围
    int left = std::max(0, x1);
    int right = std::min(canvas_width - 1, x2);
    int top = std::max(0, y1);
    int bottom = std::min(canvas_height - 1, y2);
    if (left > right || top > bottom)
        return;

    // 画上下边框
    for (int x = left; x <= right; ++x)
    {
        canvas[top * canvas_width + x] = color;
        canvas[bottom * canvas_width + x] = color;
    }
    // 画左右边框
    for (int y = top + 1; y < bottom; ++y)
    {
        canvas[y * canvas_width + left] = color;
        canvas[y * canvas_width + right] = color;
    }
}

// 渲染一行文本，并为每个字符画出边界框
// Main function with proper color emoji support
void render_text_line_with_boxes(FT_Face face, const std::string &utf8_text,
                                 int font_size_px, const std::string &output_filename)
{
    // 1. Convert to codepoints
    std::vector<char32_t> codepoints = utf8_to_utf32(utf8_text);

    FT_Set_Pixel_Sizes(face, 0, font_size_px);

    struct CharInfo
    {
        int bitmap_left, bitmap_top;
        unsigned int bitmap_width, bitmap_rows;
        int advance_x;
        std::vector<uint8_t> bitmap_data; // stored as rows with original pitch stride
        int pitch;                        // absolute stride (positive)
        FT_Pixel_Mode pixel_mode;
    };
    std::vector<CharInfo> chars;

    int total_advance = 0;
    int max_ascent = 0, max_descent = 0;

    for (char32_t cp : codepoints)
    {
        FT_UInt glyph_index = FT_Get_Char_Index(face, static_cast<FT_ULong>(cp));
        if (glyph_index == 0)
        {
            std::println("Glyph not found for U+{:X}", static_cast<uint32_t>(cp));
            continue;
        }

        // IMPORTANT: Use FT_LOAD_COLOR for color fonts (emojis)
        FT_Load_Glyph(face, glyph_index, FT_LOAD_COLOR);
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);

        FT_GlyphSlot slot = face->glyph;
        FT_Bitmap &bitmap = slot->bitmap;

        // Even if bitmap is empty (rows=0 or width=0), we still need to record advance
        // for layout
        CharInfo info;
        info.bitmap_left = slot->bitmap_left;
        info.bitmap_top = slot->bitmap_top;
        info.bitmap_width = bitmap.width;
        info.bitmap_rows = bitmap.rows;
        info.advance_x = slot->advance.x >> 6;
        info.pixel_mode = static_cast<FT_Pixel_Mode>(bitmap.pixel_mode);
        info.pitch = std::abs(bitmap.pitch); // store absolute stride

        // Copy bitmap data if present
        if (bitmap.buffer && bitmap.rows > 0 && bitmap.width > 0)
        {
            size_t row_size = info.pitch;
            info.bitmap_data.resize(bitmap.rows * row_size);
            for (unsigned int y = 0; y < bitmap.rows; ++y)
            {
                const uint8_t *src_row =
                    bitmap.buffer + y * bitmap.pitch; // pitch may be negative
                uint8_t *dst_row = info.bitmap_data.data() + y * row_size;
                std::memcpy(dst_row, src_row, row_size);
            }
        }

        chars.push_back(std::move(info));

        int ascent = info.bitmap_top;
        int descent = static_cast<int>(info.bitmap_rows) - info.bitmap_top;
        if (ascent > max_ascent)
            max_ascent = ascent;
        if (descent > max_descent)
            max_descent = descent;

        total_advance += info.advance_x;
    }

    if (chars.empty())
    {
        std::println("No characters rendered.");
        return;
    }

    int margin = 20;
    int canvas_width = total_advance + 2 * margin;
    int canvas_height = max_ascent + max_descent + 2 * margin;
    int baseline_y = max_ascent + margin;

    std::vector<uint8_t> canvas(canvas_width * canvas_height, 0); // grayscale canvas

    int pen_x = margin;
    for (const auto &info : chars)
    {
        int x = pen_x + info.bitmap_left;
        int y = baseline_y - info.bitmap_top;

        // Draw bitmap pixels according to pixel mode
        if (info.bitmap_width > 0 && info.bitmap_rows > 0)
        {
            size_t row_stride = info.pitch; // stored absolute stride
            for (unsigned int row = 0; row < info.bitmap_rows; ++row)
            {
                int dst_y = y + row;
                if (dst_y < 0 || dst_y >= canvas_height)
                    continue;
                const uint8_t *src_row = info.bitmap_data.data() + row * row_stride;
                uint8_t *dst_row = canvas.data() + dst_y * canvas_width;

                if (info.pixel_mode == FT_PIXEL_MODE_GRAY)
                {
                    for (unsigned int col = 0; col < info.bitmap_width; ++col)
                    {
                        int dst_x = x + col;
                        if (dst_x >= 0 && dst_x < canvas_width)
                        {
                            dst_row[dst_x] = src_row[col];
                        }
                    }
                }
                else if (info.pixel_mode == FT_PIXEL_MODE_BGRA)
                {
                    // Convert BGRA to grayscale (luma)
                    for (unsigned int col = 0; col < info.bitmap_width; ++col)
                    {
                        int dst_x = x + col;
                        if (dst_x < 0 || dst_x >= canvas_width)
                            continue;
                        // BGRA order: src_row[col*4 + 0] = B, +1 = G, +2 = R, +3 = A
                        uint8_t b = src_row[col * 4 + 0];
                        uint8_t g = src_row[col * 4 + 1];
                        uint8_t r = src_row[col * 4 + 2];
                        uint8_t a = src_row[col * 4 + 3];
                        // Simple luminance (Rec. 709): 0.2126*R + 0.7152*G + 0.0722*B
                        // Using integer math to avoid floating point
                        uint8_t gray = static_cast<uint8_t>((r * 54 + g * 183 + b * 18) >>
                                                            8); // 54+183+18=255
                        // If you want to show alpha, you could blend with background, but
                        // here we just set gray For now, just use gray (ignore alpha)
                        dst_row[dst_x] = gray;
                    }
                }
                else
                {
                    // Other pixel modes not implemented
                }
            }
        }

        // Always draw bounding box (white) even if bitmap was empty
        int left = x;
        int right = x + (info.bitmap_width > 0 ? info.bitmap_width - 1 : 0);
        int top = y;
        int bottom = y + (info.bitmap_rows > 0 ? info.bitmap_rows - 1 : 0);
        // Ensure box is visible even for empty glyphs (draw a small dot)
        if (info.bitmap_width == 0 || info.bitmap_rows == 0)
        {
            // If no bitmap, draw a small cross or dot at pen position
            if (pen_x >= 0 && pen_x < canvas_width && baseline_y >= 0 &&
                baseline_y < canvas_height)
            {
                canvas[baseline_y * canvas_width + pen_x] = 255; // dot
            }
        }
        else
        {
            draw_rect(canvas, canvas_width, canvas_height, left, top, right, bottom, 255);
        }

        // Mark pen position on baseline
        if (pen_x >= 0 && pen_x < canvas_width && baseline_y >= 0 &&
            baseline_y < canvas_height)
        {
            canvas[baseline_y * canvas_width + pen_x] = 255; // baseline dot
        }

        pen_x += info.advance_x;
    }

    // Save canvas as BMP
    FT_Bitmap canvas_bitmap;
    canvas_bitmap.rows = canvas_height;
    canvas_bitmap.width = canvas_width;
    canvas_bitmap.pitch = canvas_width;
    canvas_bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    canvas_bitmap.buffer = canvas.data();
    canvas_bitmap.num_grays = 256;
    canvas_bitmap.palette_mode = 0;
    canvas_bitmap.palette = nullptr;

    save_ft_bitmap_as_bmp(canvas_bitmap, output_filename);
    std::println("Saved debug image to {}", output_filename);
}

void render_line_debug(FT_Face face, const char32_t *text, int font_size)
{
    FT_Set_Pixel_Sizes(face, 0, font_size);

    int pen_x = 0; // 26.6 格式的横坐标（1/64 像素）
    int pen_y = 0; // 基线位置

    // 准备一张足够大的画布（灰度图），先用固定大小，之后可改进
    const int canvas_width = 800;
    const int canvas_height = 200;
    std::vector<uint8_t> canvas(canvas_width * canvas_height, 0);
    int baseline_y = 100; // 画布上的基线 y 坐标（像素）

    std::println("开始渲染文本，字号 {} 像素", font_size);

    for (const char32_t *cp = text; *cp; ++cp)
    {
        FT_UInt glyph_idx = FT_Get_Char_Index(face, static_cast<FT_ULong>(*cp));
        if (!glyph_idx)
        {
            std::println("  字符 U+{:04X} 缺失，跳过", static_cast<uint32_t>(*cp));
            continue;
        }

        // 加载并渲染字形（使用 FT_LOAD_RENDER 一步到位）
        FT_Load_Glyph(face, glyph_idx, FT_LOAD_RENDER | FT_LOAD_COLOR); // 彩色字体兼容
        FT_GlyphSlot slot = face->glyph;
        FT_Bitmap &bitmap = slot->bitmap;

        // 打印当前字形的度量
        std::println("  字符 U+{:04X}", static_cast<uint32_t>(*cp));
        std::println("    bitmap_left: {}, bitmap_top: {}", slot->bitmap_left,
                     slot->bitmap_top);
        std::println("    bitmap 尺寸: {}x{}", bitmap.width, bitmap.rows);
        std::println("    advance.x: {} (1/64 像素)", slot->advance.x);

        // 计算在画布上的位置（注意 Y 方向转换）
        int x = pen_x / 64 + slot->bitmap_left;
        int y = baseline_y -
                slot->bitmap_top; // pen_y 是基线，这里 pen_y=0，所以直接 baseline_y

        // 绘制位图到画布（仅灰度模式，彩色可简单处理）
        if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
        {
            for (unsigned int row = 0; row < bitmap.rows; ++row)
            {
                int dst_y = y + row;
                if (dst_y < 0 || dst_y >= canvas_height)
                    continue;
                const uint8_t *src = bitmap.buffer + row * bitmap.pitch;
                uint8_t *dst = canvas.data() + dst_y * canvas_width;
                for (unsigned int col = 0; col < bitmap.width; ++col)
                {
                    int dst_x = x + col;
                    if (dst_x >= 0 && dst_x < canvas_width)
                    {
                        dst[dst_x] = src[col];
                    }
                }
            }
        }

        // 更新 pen 位置
        pen_x += slot->advance.x;
        std::println("    新的 pen_x = {}.{} 像素", pen_x / 64, (pen_x % 64) * 100 / 64);
    }

    // 保存画布为 BMP
    FT_Bitmap canvas_bmp;
    canvas_bmp.rows = canvas_height;
    canvas_bmp.width = canvas_width;
    canvas_bmp.pitch = canvas_width;
    canvas_bmp.pixel_mode = FT_PIXEL_MODE_GRAY;
    canvas_bmp.buffer = canvas.data();
    canvas_bmp.num_grays = 256;
    save_ft_bitmap_as_bmp(canvas_bmp, "line_debug.bmp");
    std::println("已保存图片 line_debug.bmp");
}

int main()
try
{
    freetype_loader loader{};
    freetype_face face{*loader, UNICODE_FONT_PATH};

    AccessingtheFaceData(*face);

    {
        // 设置像素大小（例如 48 像素高）
        FT_Set_Pixel_Sizes(*face, 0, 48);

        // 选择要渲染的字符（例如汉字 '世'）
        FT_ULong charcode = U'世'; // 或使用 'A'、0x4E16 等
        FT_UInt glyph_index = FT_Get_Char_Index(*face, charcode);
        if (glyph_index == 0)
        {
            std::cerr << "Character not found in font\n";
            return 1;
        }

        // 加载字形（仅加载轮廓，不渲染）
        FT_Load_Glyph(*face, glyph_index, FT_LOAD_DEFAULT);
        // 渲染为灰度位图
        FT_Render_Glyph((*face)->glyph, FT_Render_Mode::FT_RENDER_MODE_NORMAL);

        // 保存为 PGM 文件
        save_bitmap_as_pgm((*face)->glyph->bitmap, "output.pgm");
        std::println("Saved glyph to output.pgm");

        save_ft_bitmap_as_bmp((*face)->glyph->bitmap, "output.bmp");
        std::println("Saved glyph to output.bmp");
    }
    {
        // --- 彩色字体测试（验证 BMP 函数对 BGRA 的支持）---
        freetype_face face_color{*loader, EMOJI_FONT_PATH};
        FT_Set_Pixel_Sizes(*face_color, 0, 64); // 放大一点看效果
        // 选择彩色 emoji 字符：😀 (U+1F600)，注意 C++ 中需要转义为 U"\U0001F600"
        static_assert(0x1f600 == U'😀'); // NOLINT
        FT_ULong charcode_color = U'😀'; // '😀'
        FT_UInt glyph_index_color = FT_Get_Char_Index(*face_color, charcode_color);
        if (glyph_index_color == 0)
        {
            std::cerr << "Emoji character not found in font\n";
            return 1;
        }

        // 加载彩色字形：必须使用 FT_LOAD_COLOR 标志，否则可能加载轮廓
        FT_Load_Glyph(*face_color, glyph_index_color, FT_LOAD_COLOR);
        // 渲染：对于彩色位图，通常不需要再调用
        // FT_Render_Glyph，因为加载时已经获取到位图，
        // 但为了确保位图存在，可以调用（彩色字体可能自带位图，加载后 glyph->bitmap
        // 已填充）
        FT_Render_Glyph((*face_color)->glyph, FT_RENDER_MODE_NORMAL);

        // 检查像素模式是否为预期的 BGRA
        std::println("Color glyph pixel mode: {}",
                     (*face_color)->glyph->bitmap.pixel_mode);
        // 保存为 BMP
        save_ft_bitmap_as_bmp((*face_color)->glyph->bitmap, "output_color.bmp");
        std::println("Saved color glyph to output_color.bmp");
    }
    // render_text_line
    {
        render_text_line(*face, "你好 wolrd!", 48, "hello.bmp");
    }
    {
        std::string test_text = "Ajg世😀"; // 😀 不会被渲染
        render_text_line_with_boxes(*face, test_text, 48, "boxes.bmp");
    }
    {
        const char32_t text[] = U"Hello, 世界!";
        render_line_debug(*face, text, 48);
    }
    std::cout << "main done\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unknown exception\n";
    return 1;
}