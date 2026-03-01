

#include <cassert>
#include <cuchar>
#include <algorithm>
#include <iostream>
#include <exception>

#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <vector>
#include <fstream>

// https://freetype.org/freetype2/docs/tutorial/step1.html
// diff: 1. Header Files
#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftbitmap.h>

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

static std::vector<char32_t> utf8_to_utf32(const std::string &utf8)
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

// ==================== 通用、正确的 FT_Bitmap 保存函数 ====================
// BMP 文件头结构（紧凑对齐）
#pragma pack(push, 1)
struct BMPFileHeader
{
    uint16_t bfType = 0x4D42; // "BM"
    uint32_t bfSize = 0;      // 文件大小
    uint16_t bfReserved1 = 0;
    uint16_t bfReserved2 = 0;
    uint32_t bfOffBits = 0; // 像素数据偏移
};

struct BMPInfoHeader
{
    uint32_t biSize = 40; // 此结构大小
    int32_t biWidth = 0;
    int32_t biHeight = 0; // 若为正，表示倒置（原点左下），我们设为负值表示从上到下
    uint16_t biPlanes = 1;
    uint16_t biBitCount = 0;    // 1,4,8,16,24,32
    uint32_t biCompression = 0; // 0 = BI_RGB
    uint32_t biSizeImage = 0;
    int32_t biXPelsPerMeter = 0;
    int32_t biYPelsPerMeter = 0;
    uint32_t biClrUsed = 0; // 实际使用的颜色数
    uint32_t biClrImportant = 0;
};
#pragma pack(pop)

/**
 * 将 FT_Bitmap 保存为 BMP 文件，保证数据与 FreeType 内部完全一致。
 * 支持所有像素模式：
 *   - MONO   : 生成 1 位 BMP（调色板：0=黑，1=白）
 *   - GRAY2  : 生成 8 位 BMP，调色板将原始 2 位值 (0-3) 映射到灰度 0,85,170,255
 *   - GRAY4  : 生成 8 位 BMP，调色板将原始 4 位值 (0-15) 映射到灰度 0,17,34,...,255
 *   - GRAY   : 生成 8 位 BMP，调色板为标准灰度渐变
 *   - LCD    : 生成 24 位 BMP，宽度调整为 1/3，BGR 顺序
 *   - LCD_V  : 生成 24 位 BMP，高度调整为 1/3，从三行中提取 RGB
 *   - BGRA   : 生成 32 位 BMP，保留 BGRA 顺序
 * @param bitmap  原始 FT_Bitmap
 * @param library FT_Library 实例（用于 FT_Bitmap_Convert）
 * @param filename 输出文件名（建议 .bmp）
 * @return 成功返回 true，否则 false
 */
bool save_ft_bitmap(const FT_Bitmap &bitmap, FT_Library library,
                    const std::string &filename)
{
    if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.buffer == nullptr)
    {
        std::println("save_ft_bitmap: bitmap is empty");
        return false;
    }

    // 确定输出图像的参数
    int out_width = bitmap.width;
    int out_height = bitmap.rows;
    uint16_t bitCount = 0;
    int palette_size = 0; // 调色板条目数（0表示无调色板）
    bool need_color_table = false;

    // 根据像素模式设置输出格式
    switch (bitmap.pixel_mode)
    {
    case FT_PIXEL_MODE_MONO:
        bitCount = 8; // 改为 8，配合转换后的灰度数据
        need_color_table = true;
        palette_size = 2; // 黑白两色
        break;
    case FT_PIXEL_MODE_GRAY2:
        // 我们转换为 8 位索引，保留原始 2 位值，通过调色板映射
        bitCount = 8;
        need_color_table = true;
        palette_size = 4; // 2 位有 4 种值
        break;
    case FT_PIXEL_MODE_GRAY4:
        bitCount = 8;
        need_color_table = true;
        palette_size = 16; // 4 位有 16 种值
        break;
    case FT_PIXEL_MODE_GRAY:
        bitCount = 8;
        need_color_table = true;
        palette_size = 256; // 全灰度调色板
        break;
    case FT_PIXEL_MODE_LCD:
        if (bitmap.width % 3 != 0)
        {
            std::println("save_ft_bitmap: LCD width {} not multiple of 3", bitmap.width);
            return false;
        }
        out_width = bitmap.width / 3;
        out_height = bitmap.rows;
        bitCount = 24;
        need_color_table = false;
        break;
    case FT_PIXEL_MODE_LCD_V:
        if (bitmap.rows % 3 != 0)
        {
            std::println("save_ft_bitmap: LCD_V height {} not multiple of 3",
                         bitmap.rows);
            return false;
        }
        out_width = bitmap.width;
        out_height = bitmap.rows / 3;
        bitCount = 24;
        need_color_table = false;
        break;
    case FT_PIXEL_MODE_BGRA:
        bitCount = 32;
        need_color_table = false;
        break;
    default:
        std::println("save_ft_bitmap: unsupported pixel mode {}", bitmap.pixel_mode);
        return false;
    }

    // 对于需要调色板的模式，我们先用 FT_Bitmap_Convert 转为 8 位灰度（并调整 pitch 对齐）
    FT_Bitmap converted;
    FT_Bitmap_Init(&converted);
    const uint8_t *pixel_data = nullptr;
    bool need_free_converted = false;

    if (need_color_table)
    {
        // 转换为 8 位灰度，最后一个参数 1 表示每像素 1 字节，且 pitch 调整为宽度
        FT_Error error = FT_Bitmap_Convert(library, &bitmap, &converted, 1);
        if (error)
        {
            std::println("FT_Bitmap_Convert failed with error {}", error);
            return false;
        }
        pixel_data = converted.buffer;
        out_width = converted.width; // 转换后宽度可能不变
        out_height = converted.rows;
        need_free_converted = true;
    }
    else
    {
        // 彩色模式直接使用原始数据，但需要处理对齐和尺寸
        pixel_data = bitmap.buffer;
    }

    // 计算 BMP 行对齐字节数（每行必须是 4 字节的倍数）
    int bytes_per_pixel = (bitCount == 24) ? 3 : (bitCount == 32) ? 4 : 1;
    int line_bytes = ((out_width * bitCount + 31) / 32) * 4;
    int image_size = line_bytes * out_height;

    // 文件头和信息头
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;

    infoHeader.biWidth = out_width;
    infoHeader.biHeight = -out_height; // 负值表示从上到下（与 FreeType 行序一致）
    infoHeader.biBitCount = bitCount;
    infoHeader.biSizeImage = image_size;
    infoHeader.biClrUsed = palette_size;

    fileHeader.bfOffBits =
        sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + palette_size * 4;
    fileHeader.bfSize = fileHeader.bfOffBits + image_size;

    // 打开文件
    std::ofstream file(filename, std::ios::binary);
    if (!file)
    {
        std::println("save_ft_bitmap: cannot open file {}", filename);
        if (need_free_converted)
            FT_Bitmap_Done(library, &converted);
        return false;
    }

    // 写入文件头和信息头
    file.write(reinterpret_cast<const char *>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char *>(&infoHeader), sizeof(infoHeader));

    // 写入调色板（如果需要）
    if (need_color_table)
    {
        std::vector<uint8_t> palette(palette_size * 4, 0); // 每个条目 B,G,R,0
        switch (bitmap.pixel_mode)
        {
        case FT_PIXEL_MODE_MONO:
            // 索引 0: 黑 (0,0,0), 索引 1: 白 (255,255,255)
            palette[0] = 0;
            palette[1] = 0;
            palette[2] = 0; // BGR0
            palette[4] = 255;
            palette[5] = 255;
            palette[6] = 255;
            break;
        case FT_PIXEL_MODE_GRAY2: {
            // 2 位灰度，值 0-3 映射为 0,85,170,255
            uint8_t vals[4] = {0, 85, 170, 255};
            for (int i = 0; i < 4; ++i)
            {
                palette[i * 4 + 0] = vals[i];
                palette[i * 4 + 1] = vals[i];
                palette[i * 4 + 2] = vals[i];
            }
            break;
        }
        case FT_PIXEL_MODE_GRAY4: {
            // 4 位灰度，值 0-15 映射为 0,17,34,...,255
            for (int i = 0; i < 16; ++i)
            {
                uint8_t val = static_cast<uint8_t>(i * 17);
                palette[i * 4 + 0] = val;
                palette[i * 4 + 1] = val;
                palette[i * 4 + 2] = val;
            }
            break;
        }
        case FT_PIXEL_MODE_GRAY: {
            // 8 位灰度，值 i 映射为 i
            for (int i = 0; i < 256; ++i)
            {
                palette[i * 4 + 0] = static_cast<uint8_t>(i);
                palette[i * 4 + 1] = static_cast<uint8_t>(i);
                palette[i * 4 + 2] = static_cast<uint8_t>(i);
            }
            break;
        }
        default:
            break;
        }
        file.write(reinterpret_cast<const char *>(palette.data()), palette.size());
    }

    // 准备一行缓冲区，用于写入 BMP
    std::vector<uint8_t> line_buf(line_bytes, 0);

    // 逐行处理（从上到下）
    for (int y = 0; y < out_height; ++y)
    {
        const uint8_t *src_row;
        if (need_color_table)
        {
            // 对于灰度模式，converted 的 pitch 已经等于 width，可以直接按行取
            src_row = pixel_data + y * out_width; // 因为 pitch == width
        }
        else
        {
            // 彩色模式需要根据原始 pitch 取行
            src_row = pixel_data + y * bitmap.pitch;
        }

        // 根据模式将源数据填充到 line_buf
        if (bitmap.pixel_mode == FT_PIXEL_MODE_LCD)
        {
            // LCD: 原始数据每 3 字节一个 RGB 像素，顺序为 R,G,B
            for (int x = 0; x < out_width; ++x)
            {
                int src_idx = x * 3;
                int dst_idx = x * 3;
                // BMP 24位顺序为 B,G,R
                line_buf[dst_idx + 0] = src_row[src_idx + 2]; // B
                line_buf[dst_idx + 1] = src_row[src_idx + 1]; // G
                line_buf[dst_idx + 2] = src_row[src_idx + 0]; // R
            }
        }
        else if (bitmap.pixel_mode == FT_PIXEL_MODE_LCD_V)
        {
            // LCD_V: 需要从连续三行中提取 RGB
            const uint8_t *row_r = pixel_data + (y * 3) * bitmap.pitch;
            const uint8_t *row_g = pixel_data + (y * 3 + 1) * bitmap.pitch;
            const uint8_t *row_b = pixel_data + (y * 3 + 2) * bitmap.pitch;
            for (int x = 0; x < out_width; ++x)
            {
                int dst_idx = x * 3;
                line_buf[dst_idx + 0] = row_b[x]; // B
                line_buf[dst_idx + 1] = row_g[x]; // G
                line_buf[dst_idx + 2] = row_r[x]; // R
            }
        }
        else if (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA)
        {
            // BGRA: 直接复制 B,G,R,A
            memcpy(line_buf.data(), src_row, out_width * 4);
        }
        else
        {
            // 灰度/索引模式：converted 的数据已经是 8 位索引，直接复制
            memcpy(line_buf.data(), src_row, out_width);
        }

        // 写入对齐后的行
        file.write(reinterpret_cast<const char *>(line_buf.data()), line_bytes);
        if (!file)
        {
            std::println("save_ft_bitmap: write error at line {}", y);
            if (need_free_converted)
                FT_Bitmap_Done(library, &converted);
            return false;
        }
    }

    if (need_free_converted)
    {
        FT_Bitmap_Done(library, &converted);
    }

    std::println("save_ft_bitmap: saved {} ({}x{}, {} bpp)", filename, out_width,
                 out_height, bitCount);
    return true;
}

/*
A face object models all information that globally describes the face. Usually, this data
can be accessed directly by dereferencing a handle, like in .face−>num_glyphs
NOTE: FT_Face 可以访问所有 全局face描述符
*/

constexpr auto *UNICODE_FONT_PATH = "C:/Windows/Fonts/msyh.ttc";   // 中文字体
constexpr auto *EMOJI_FONT_PATH = "C:/Windows/Fonts/seguiemj.ttf"; // 彩色表情字体

constexpr auto INPUT_TEXT = "Hello, 世界! こんにちは 123 😀👍🎉";

static bool is_verticallayout(const std::string &filename,
                              std::optional<char32_t> testChar = {})
{
    freetype_loader loader{};
    freetype_face face_(*loader, filename);
    FT_Face face = *face_;

    constexpr auto LOAD_FLAG = FT_LOAD_VERTICAL_LAYOUT | FT_LOAD_RENDER;
    constexpr auto HEIGHT = 16;
    FT_Set_Pixel_Sizes(face, 0, HEIGHT);
    bool ret = FT_HAS_VERTICAL(face);
    if (ret && testChar.has_value())
    {
        auto Char = *testChar;
        // FT_UInt glyph_index = FT_Get_Char_Index(face, Char);
        // if (glyph_index == 0)
        // {
        //     std::println("Glyph not found for U+{:X}", static_cast<uint32_t>(Char));
        //     return false;
        // }
        // return FT_Err_Ok == FT_Load_Glyph(face, glyph_index, LOAD_FLAG);
        return FT_Load_Char(face, Char, LOAD_FLAG);
        ;
    }
    return ret;
}

auto render_Char(FT_Library library, FT_Face face, char32_t Char, std::string filename,
                 int pixel_width = 0, int pixel_height = 16,
                 std::optional<uint32_t> foreground_color = std::nullopt,
                 std::optional<uint32_t> background_color = std::nullopt)
{
    // 设置像素尺寸
    FT_Set_Pixel_Sizes(face, pixel_width, pixel_height);

    // 获取字形索引
    FT_UInt glyph_index = FT_Get_Char_Index(face, Char);
    if (glyph_index == 0)
        throw std::runtime_error("Character not found in font");

    // 加载字形并渲染（支持彩色字体）
    FT_Error error = FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER | FT_LOAD_COLOR);
    if (error != FT_Err_Ok)
        throw std::runtime_error("Failed to load and render glyph");

    FT_Bitmap &original_bitmap = face->glyph->bitmap;

    // 如果用户指定了颜色映射，并且位图是灰度模式，则生成彩色位图
    std::optional<std::vector<uint8_t>> pixel_buffer; // 用 vector 管理内存
    FT_Bitmap colored_bitmap = {};                    // 初始化为零，稍后填充

    if (foreground_color.has_value() && background_color.has_value() &&
        original_bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
    {
        // 准备颜色分量
        uint32_t fg = foreground_color.value();
        uint32_t bg = background_color.value();
        uint8_t fg_r = (fg >> 16) & 0xFF;
        uint8_t fg_g = (fg >> 8) & 0xFF;
        uint8_t fg_b = fg & 0xFF;
        uint8_t bg_r = (bg >> 16) & 0xFF;
        uint8_t bg_g = (bg >> 8) & 0xFF;
        uint8_t bg_b = bg & 0xFF;

        // 计算彩色位图参数
        int width = original_bitmap.width;
        int height = original_bitmap.rows;
        int pitch = width * 4; // BGRA 每像素 4 字节

        // 分配像素缓冲区（vector 自动管理）
        pixel_buffer.emplace(pitch * height);
        uint8_t *dst = pixel_buffer->data();

        // 混合每个像素
        for (int y = 0; y < height; ++y)
        {
            const uint8_t *src = original_bitmap.buffer + y * original_bitmap.pitch;
            uint8_t *dst_row = dst + y * pitch;
            for (int x = 0; x < width; ++x)
            {
                uint8_t alpha = src[x]; // 0（背景）～ 255（前景）
                // 混合：result = bg * (1 - alpha/255) + fg * (alpha/255)
                uint8_t b =
                    static_cast<uint8_t>((bg_b * (255 - alpha) + fg_b * alpha) / 255);
                uint8_t g =
                    static_cast<uint8_t>((bg_g * (255 - alpha) + fg_g * alpha) / 255);
                uint8_t r =
                    static_cast<uint8_t>((bg_r * (255 - alpha) + fg_r * alpha) / 255);
                dst_row[x * 4 + 0] = b;
                dst_row[x * 4 + 1] = g;
                dst_row[x * 4 + 2] = r;
                dst_row[x * 4 + 3] = 255; // 完全不透明
            }
        }

        // 填充 FT_Bitmap 结构
        colored_bitmap.width = width;
        colored_bitmap.rows = height;
        colored_bitmap.pitch = pitch;
        colored_bitmap.pixel_mode = FT_PIXEL_MODE_BGRA;
        colored_bitmap.num_grays = 0; // 不适用
        colored_bitmap.buffer = dst;  // vector 的数据指针
        // 其他字段保持默认（0）
    }

    // 决定要保存的位图
    const FT_Bitmap &bitmap_to_save =
        (pixel_buffer.has_value()) ? colored_bitmap : original_bitmap;

    // 保存为 BMP 文件（save_ft_bitmap 不会持有 buffer 指针，只读取数据）
    bool success = save_ft_bitmap(bitmap_to_save, library, filename);
    if (!success)
        throw std::runtime_error("Failed to save bitmap");

    // pixel_buffer 在函数退出时自动释放内存
}

int main()
try
{
    // assert(is_verticallayout("TiroDevanagariHindi-Regular.ttf", 'g'));
    freetype_loader loader{};
    {
        freetype_face face(*loader, "TiroDevanagariHindi-Regular.ttf");
        render_Char(*loader, *face, 'g', "g_400x600.bmp", 400, 600);
        render_Char(*loader, *face, 'g', "g_0x600.bmp", 0, 600);
        render_Char(*loader, *face, 'g', "g_0x600_red.bmp", 0, 600, 0xFF0000, 0xFFFFFF);
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