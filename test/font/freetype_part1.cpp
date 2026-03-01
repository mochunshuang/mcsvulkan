

#include <cuchar>
#include <iostream>
#include <exception>

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
    bool is_color = false;

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
        is_color = true;
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
        is_color = true;
        break;
    case FT_PIXEL_MODE_BGRA:
        bitCount = 32;
        need_color_table = false;
        is_color = true;
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

// ==================== 第四章：访问 face 数据（不调用保存函数）====================
void test_chapter4(FT_Library lib, const std::string &font_path)
{
    std::println("\n========== Chapter 4: Accessing Face Data ==========");
    freetype_face face(lib, font_path);
    FT_Face ft_face = *face;

    // 1. 基本身份信息
    std::println("Family name: {}",
                 ft_face->family_name ? ft_face->family_name : "(null)");
    std::println("Style name : {}", ft_face->style_name ? ft_face->style_name : "(null)");
    std::println("Number of glyphs (num_glyphs) : {}", ft_face->num_glyphs);
    // === PATCH START ===
    std::println("  → num_glyphs 表示字体包含的字形总数，用于确保字符索引不越界。");

    // 2. 使用官方宏解析 face_flags，并添加解释
    std::println("Face flags (raw): 0x{:X}", ft_face->face_flags);
    // 每个宏后添加简短说明
    bool has_horizontal = FT_HAS_HORIZONTAL(ft_face);
    std::println("  FT_HAS_HORIZONTAL(face)      : {} (是否有水平度量，影响文本水平布局)",
                 has_horizontal ? "yes" : "no");
    bool has_vertical = FT_HAS_VERTICAL(ft_face);
    std::println("  FT_HAS_VERTICAL(face)        : {} (是否有垂直度量，用于竖排文本)",
                 has_vertical ? "yes" : "no");
    bool has_kerning = FT_HAS_KERNING(ft_face);
    std::println("  FT_HAS_KERNING(face)         : {} (是否支持字距调整，影响字符间距)",
                 has_kerning ? "yes" : "no");
    bool has_fixed_sizes = FT_HAS_FIXED_SIZES(ft_face);
    std::println("  FT_HAS_FIXED_SIZES(face)     : {} "
                 "(是否内嵌固定位图，若为是，渲染时可优先使用)",
                 has_fixed_sizes ? "yes" : "no");
    bool has_glyph_names = FT_HAS_GLYPH_NAMES(ft_face);
    std::println(
        "  FT_HAS_GLYPH_NAMES(face)     : {} (是否提供字形名称，用于PostScript处理)",
        has_glyph_names ? "yes" : "no");
    bool has_color = FT_HAS_COLOR(ft_face);
    std::println("  FT_HAS_COLOR(face)           : {} (是否为彩色字体，加载时需使用 "
                 "FT_LOAD_COLOR 标志)",
                 has_color ? "yes" : "no");
    bool has_mm = FT_HAS_MULTIPLE_MASTERS(ft_face);
    std::println(
        "  FT_HAS_MULTIPLE_MASTERS(face): {} (是否支持可变字体，可动态调整字重等)",
        has_mm ? "yes" : "no");
    bool is_scalable = FT_IS_SCALABLE(ft_face);
    std::println(
        "  FT_IS_SCALABLE(face)         : {} (是否可缩放，若否则只能使用固定位图)",
        is_scalable ? "yes" : "no");
    bool is_sfnt = FT_IS_SFNT(ft_face);
    std::println(
        "  FT_IS_SFNT(face)             : {} (是否为 SFNT 格式，如 TrueType/OpenType)",
        is_sfnt ? "yes" : "no");
    bool is_fixed_width = FT_IS_FIXED_WIDTH(ft_face);
    std::println("  FT_IS_FIXED_WIDTH(face)      : {} (是否为等宽字体)",
                 is_fixed_width ? "yes" : "no");
    bool is_cid = FT_IS_CID_KEYED(ft_face);
    std::println("  FT_IS_CID_KEYED(face)        : {} (是否为 CID 字体，用于PostScript)",
                 is_cid ? "yes" : "no");
    bool is_tricky = FT_IS_TRICKY(ft_face);
    std::println(
        "  FT_IS_TRICKY(face)            : {} (是否需要特殊处理，如某些字体需强制缩放)",
        is_tricky ? "yes" : "no");
    bool is_named_instance = FT_IS_NAMED_INSTANCE(ft_face);
    std::println("  FT_IS_NAMED_INSTANCE(face)    : {} (是否为可变字体的命名实例)",
                 is_named_instance ? "yes" : "no");
    bool is_variation = FT_IS_VARIATION(ft_face);
    std::println("  FT_IS_VARIATION(face)         : {} (是否为可变字体的变体实例)",
                 is_variation ? "yes" : "no");

    // 3. 如果可缩放，打印 units_per_EM，并解释其作用
    if (is_scalable)
    {
        std::println("units_per_EM: {}", ft_face->units_per_EM);
        std::println(
            "  → units_per_EM 是字体设计网格的大小，用于将轮廓坐标转换为实际像素。");
        std::println("    例如，设置字符大小为 12pt (1/72英寸) 时，FreeType "
                     "会根据分辨率和 units_per_EM 计算缩放因子。");
        // 演示：使用 FT_Set_Char_Size 基于点大小设置
        FT_Set_Char_Size(ft_face, 0, 12 * 64, 96, 96); // 12pt @ 96dpi
        std::println("    设置 12pt @ 96dpi 后，计算出的 ppem 为 {}x{}",
                     ft_face->size->metrics.x_ppem, ft_face->size->metrics.y_ppem);
    }

    // 4. 固定尺寸信息，并说明如何影响渲染
    std::println("Number of fixed sizes (num_fixed_sizes): {}", ft_face->num_fixed_sizes);
    for (int i = 0; i < ft_face->num_fixed_sizes; ++i)
    {
        const auto &sz = ft_face->available_sizes[i];
        std::println("  Strike {}: height={}, width={}, size={} (ppem x={}, y={})", i,
                     sz.height, sz.width, sz.size, sz.x_ppem, sz.y_ppem);
    }
    if (ft_face->num_fixed_sizes > 0)
    {
        std::println("  → 这些是内嵌的位图尺寸，若请求的像素大小与某个 strike "
                     "匹配，FreeType 将直接使用位图而无需缩放轮廓。");
    }
    else
    {
        std::println("  → 无内嵌位图，所有渲染都将基于轮廓缩放。");
    }
    // === PATCH END ===

    // 5. 选择一个字符（例如 'A'）进行实际渲染，观察固定尺寸和缩放的效果
    //    先确保选择了 Unicode charmap（通常默认）
    FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE);
    FT_ULong charcode = 'A'; // 也可以使用 U+0041
    FT_UInt glyph_index = FT_Get_Char_Index(ft_face, charcode);
    if (glyph_index == 0)
    {
        std::println("Warning: glyph not found for 'A'");
    }
    else
    {
        // 5a. 如果有固定尺寸，尝试使用第一个 strike 加载并渲染
        if (ft_face->num_fixed_sizes > 0)
        {
            // 设置像素大小为第一个 strike 的尺寸（使用 FT_Set_Pixel_Sizes
            // 会优先使用嵌入位图）
            FT_Set_Pixel_Sizes(ft_face, ft_face->available_sizes[0].width,
                               ft_face->available_sizes[0].height);
            FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
            FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);
            save_ft_bitmap(ft_face->glyph->bitmap, lib, "chapter4_fixed_A.bmp");
            std::println("Saved fixed-size glyph (strike 0) to chapter4_fixed_A.bmp");
            std::println("  → "
                         "此图像直接使用了字体内嵌的位图，未经缩放，适合该像素大小时显示"
                         "效果最佳。");
        }

        // 5b. 设置一个常规像素大小（24px）进行缩放渲染
        if (is_scalable)
        {
            FT_Set_Pixel_Sizes(ft_face, 0, 24);
            std::println("  Set pixel size: x_ppem={}, y_ppem={}",
                         ft_face->size->metrics.x_ppem, ft_face->size->metrics.y_ppem);
            FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
            FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);
            std::println("    Glyph bitmap size: {}x{} (位图尺寸通常小于 EM "
                         "正方形，因为字形轮廓未占满整个 EM)",
                         ft_face->glyph->bitmap.width, ft_face->glyph->bitmap.rows);
            save_ft_bitmap(ft_face->glyph->bitmap, lib, "chapter4_scaled_A.bmp");
            std::println("Saved scaled glyph (24px) to chapter4_scaled_A.bmp");
        }
    }

    std::println("Chapter 4 test completed. Inspect the generated BMP files.");
}

// ==================== 第五章：设置像素大小 ====================
void test_chapter5(FT_Library lib, const std::string &font_path)
{
    std::println("\n========== Chapter 5: Setting the Current Pixel Size ==========");
    freetype_face face(lib, font_path);
    FT_Face ft_face = *face;

    // 确保选择了 Unicode charmap
    FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE);

    // 选择几个有代表性的字符
    struct TestChar
    {
        FT_ULong code;
        const char *name;
    } test_chars[] = {
        {U'A', "A"},      // 大写字母，有升部
        {U'g', "g"},      // 小写字母，有降部
        {U'W', "W"},      // 宽字符
        {U'中', "zhong"}, // 中文字符（如果字体支持）
    };

    // 辅助函数：渲染并保存当前设置的字符
    auto render_and_save = [&](const TestChar &tc, const std::string &suffix) {
        FT_UInt glyph_index = FT_Get_Char_Index(ft_face, tc.code);
        if (glyph_index == 0)
        {
            std::println("  Character U+{:04X} ({}) not found in font, skipped.",
                         static_cast<unsigned>(tc.code), tc.name);
            return;
        }
        FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
        FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);
        std::string filename = std::format("ch5_{}_{}.bmp", tc.name, suffix);
        save_ft_bitmap(ft_face->glyph->bitmap, lib, filename);
        std::println("  Saved {}", filename);
    };

    // 1. 初始状态：刚创建 face，size 对象已存在但 metrics 为零
    std::println("Initial face->size metrics (all zero until set):");
    std::println("  x_ppem = {}, y_ppem = {}", ft_face->size->metrics.x_ppem,
                 ft_face->size->metrics.y_ppem);

    // 2. 使用 FT_Set_Char_Size 基于点大小和分辨率设置
    std::println("\n--- FT_Set_Char_Size ---");
    // 例1: 12pt @ 72dpi (标准点大小对应 1pt = 1px 当 72dpi)
    FT_Set_Char_Size(ft_face, 0, 12 * 64, 72, 72);
    std::println("Set 12pt @ 72dpi -> x_ppem = {}, y_ppem = {}",
                 ft_face->size->metrics.x_ppem, ft_face->size->metrics.y_ppem);
    std::println(
        "  (12pt @ 72dpi 应得 12px ppem，因为 12pt * (1/72 inch) * 72 dpi = 12px)");
    for (const auto &tc : test_chars)
        render_and_save(tc, "12pt_72dpi");

    // 例2: 12pt @ 96dpi (屏幕常见)
    FT_Set_Char_Size(ft_face, 0, 12 * 64, 96, 96);
    std::println("\nSet 12pt @ 96dpi -> x_ppem = {}, y_ppem = {}",
                 ft_face->size->metrics.x_ppem, ft_face->size->metrics.y_ppem);
    std::println(
        "  (12pt @ 96dpi 应得 16px ppem，因为 12pt * (1/72 inch) * 96 dpi = 16px)");
    for (const auto &tc : test_chars)
        render_and_save(tc, "12pt_96dpi");

    // 例3: 24pt @ 96dpi
    FT_Set_Char_Size(ft_face, 0, 24 * 64, 96, 96);
    std::println("\nSet 24pt @ 96dpi -> x_ppem = {}, y_ppem = {}",
                 ft_face->size->metrics.x_ppem, ft_face->size->metrics.y_ppem);
    std::println("  (24pt @ 96dpi 应得 32px ppem)");
    for (const auto &tc : test_chars)
        render_and_save(tc, "24pt_96dpi");

    // 3. 使用 FT_Set_Pixel_Sizes 直接指定像素大小
    std::println("\n--- FT_Set_Pixel_Sizes ---");
    // 例4: 16px
    FT_Set_Pixel_Sizes(ft_face, 0, 16);
    std::println("Set 16px -> x_ppem = {}, y_ppem = {}", ft_face->size->metrics.x_ppem,
                 ft_face->size->metrics.y_ppem);
    for (const auto &tc : test_chars)
        render_and_save(tc, "16px");

    // 例5: 32px
    FT_Set_Pixel_Sizes(ft_face, 0, 32);
    std::println("\nSet 32px -> x_ppem = {}, y_ppem = {}", ft_face->size->metrics.x_ppem,
                 ft_face->size->metrics.y_ppem);
    for (const auto &tc : test_chars)
        render_and_save(tc, "32px");

    // 4. 演示非整数 ppem 的处理（如果字体可缩放且 hinting 引擎要求整数）
    if (FT_IS_SCALABLE(ft_face))
    {
        std::println("\n--- Fractional ppem handling ---");
        // 设置 13pt @ 96dpi，计算得 ppem = 13 * 96 / 72 = 17.333...
        FT_Set_Char_Size(ft_face, 0, 13 * 64, 96, 96);
        std::println("Set 13pt @ 96dpi -> x_ppem = {}, y_ppem = {}",
                     ft_face->size->metrics.x_ppem, ft_face->size->metrics.y_ppem);
        std::println("  (理论值 13*96/72 = 17.333..., FreeType 可能四舍五入，尤其是 "
                     "hinting 引擎只支持整数)");
        // 加载并渲染一个字符，查看实际位图尺寸
        FT_UInt idx = FT_Get_Char_Index(ft_face, U'A');
        if (idx)
        {
            FT_Load_Glyph(ft_face, idx, FT_LOAD_DEFAULT);
            FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);
            std::println("  Glyph bitmap size for 'A': {}x{}",
                         ft_face->glyph->bitmap.width, ft_face->glyph->bitmap.rows);
            save_ft_bitmap(ft_face->glyph->bitmap, lib, "ch5_A_13pt_96dpi.bmp");
        }
    }

    // 5. 如果字体有固定尺寸（num_fixed_sizes > 0），演示设置不支持的像素大小会返回错误
    if (FT_HAS_FIXED_SIZES(ft_face))
    {
        std::println("\n--- Fixed-size font demonstration ---");
        std::println("Font has {} fixed strikes:", ft_face->num_fixed_sizes);
        for (int i = 0; i < ft_face->num_fixed_sizes; ++i)
        {
            std::println("  Strike {}: {}x{} px", i, ft_face->available_sizes[i].width,
                         ft_face->available_sizes[i].height);
        }
        // 尝试设置一个不存在的像素大小（例如 15px），预期失败
        FT_Error error = FT_Set_Pixel_Sizes(ft_face, 0, 15);
        if (error)
        {
            std::println("FT_Set_Pixel_Sizes(15px) failed with error {}: as expected, "
                         "only listed strikes are supported.",
                         error);
        }
        else
        {
            std::println("Warning: setting 15px succeeded unexpectedly.");
        }
    }
    else
    {
        std::println("\nFont is scalable, no fixed sizes.");
    }

    // 6. 打印 size 对象中的其他重要 metrics（以当前设置为准，比如 32px）
    FT_Set_Pixel_Sizes(ft_face, 0, 32);
    std::println("\n--- Size metrics for 32px setting ---");
    std::println("  ascender  = {} (in 1/64px, i.e. {} px)",
                 ft_face->size->metrics.ascender, ft_face->size->metrics.ascender / 64);
    std::println("  descender = {} (in 1/64px, i.e. {} px)",
                 ft_face->size->metrics.descender, ft_face->size->metrics.descender / 64);
    std::println("  height    = {} (in 1/64px, i.e. {} px)",
                 ft_face->size->metrics.height, ft_face->size->metrics.height / 64);
    std::println("  max_advance = {} (in 1/64px, i.e. {} px)",
                 ft_face->size->metrics.max_advance,
                 ft_face->size->metrics.max_advance / 64);
    std::println("  (这些值影响整行文本的垂直布局和水平最大宽度)");

    std::println("\nChapter 5 test completed. Inspect the generated BMP files to see how "
                 "character size affects glyph bitmaps.");
}

// ==================== 第六章：加载字形图像 ====================
void test_chapter6(FT_Library lib, const std::string &font_path)
{
    std::println("\n========== Chapter 6: Loading a Glyph Image ==========");
    freetype_face face(lib, font_path);
    FT_Face ft_face = *face;

    // 设置一个像素大小以便观察
    FT_Set_Pixel_Sizes(ft_face, 0, 24);
    std::println("Set pixel size: 24px, x_ppem={}, y_ppem={}",
                 ft_face->size->metrics.x_ppem, ft_face->size->metrics.y_ppem);

    // -------------------- a. 字符码到字形索引转换 --------------------
    std::println("\n--- a. Converting Character Code to Glyph Index ---");
    // 使用几个不同 Unicode 字符
    FT_ULong test_chars[] = {U'A', U'g', U'中', U'😀'};
    const char *names[] = {"A (U+0041)", "g (U+0067)", "中 (U+4E2D)", "😀 (U+1F600)"};
    for (int i = 0; i < 4; ++i)
    {
        FT_ULong charcode = test_chars[i];
        FT_UInt glyph_index = FT_Get_Char_Index(ft_face, charcode);
        std::println("  Character {}: glyph index = {}", names[i], glyph_index);
        if (glyph_index == 0)
        {
            std::println("    → 字形缺失，将显示为方块或空格 (missing glyph)");
        }
    }

    // 演示字符 'A' 的加载和渲染
    FT_ULong charcode = U'A';
    FT_UInt glyph_index = FT_Get_Char_Index(ft_face, charcode);
    std::println("\n--- b. Loading a Glyph From the Face ---");
    std::println("Loading glyph for 'A' (index {})", glyph_index);

    // -------------------- b. 从 face 加载字形 --------------------
    // 使用默认加载标志
    FT_Error error = FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
    if (error)
    {
        std::println("FT_Load_Glyph failed with error {}", error);
        return;
    }
    // 检查字形格式
    std::println("  Glyph format: {}",
                 ft_face->glyph->format == FT_GLYPH_FORMAT_OUTLINE     ? "outline"
                 : ft_face->glyph->format == FT_GLYPH_FORMAT_BITMAP    ? "bitmap"
                 : ft_face->glyph->format == FT_GLYPH_FORMAT_COMPOSITE ? "composite"
                                                                       : "other");
    std::println("  Glyph metrics before rendering:");
    std::println(
        "    width={}, height={}, horiBearingX={}, horiBearingY={}, horiAdvance={}",
        ft_face->glyph->metrics.width, ft_face->glyph->metrics.height,
        ft_face->glyph->metrics.horiBearingX, ft_face->glyph->metrics.horiBearingY,
        ft_face->glyph->metrics.horiAdvance);

    // 渲染为抗锯齿位图
    error = FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);
    if (error)
    {
        std::println("FT_Render_Glyph failed with error {}", error);
        return;
    }
    std::println("  After rendering:");
    std::println("    bitmap: {}x{}, pitch={}, pixel_mode={}",
                 ft_face->glyph->bitmap.width, ft_face->glyph->bitmap.rows,
                 ft_face->glyph->bitmap.pitch, ft_face->glyph->bitmap.pixel_mode);
    std::println("    bitmap_left={}, bitmap_top={}", ft_face->glyph->bitmap_left,
                 ft_face->glyph->bitmap_top);
    save_ft_bitmap(ft_face->glyph->bitmap, lib, "ch6_A_default.bmp");
    std::println("  Saved ch6_A_default.bmp");

    // 演示不同加载标志：FT_LOAD_NO_BITMAP（强制不使用内嵌位图）
    std::println("\n--- Using FT_LOAD_NO_BITMAP flag ---");
    FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_NO_BITMAP);
    FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);
    save_ft_bitmap(ft_face->glyph->bitmap, lib, "ch6_A_no_bitmap.bmp");
    std::println(
        "  Saved ch6_A_no_bitmap.bmp (if font had embedded bitmap, this would differ)");

    // 演示单色渲染 FT_RENDER_MODE_MONO
    std::println("\n--- Monochrome rendering (FT_RENDER_MODE_MONO) ---");
    FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
    FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_MONO);
    save_ft_bitmap(ft_face->glyph->bitmap, lib, "ch6_A_mono.bmp");
    std::println("  Saved ch6_A_mono.bmp");

    // -------------------- c. 使用其他 charmap --------------------
    std::println("\n--- c. Using Other Charmaps ---");
    // 列出所有可用的 charmap
    std::println("  Number of charmaps: {}", ft_face->num_charmaps);
    for (int i = 0; i < ft_face->num_charmaps; ++i)
    {
        FT_CharMap cm = ft_face->charmaps[i];
        std::println("    CharMap {}: platform_id={}, encoding_id={}", i, cm->platform_id,
                     cm->encoding_id);
    }

    // 尝试选择 Mac Roman 编码 (如果存在)
    FT_CharMap found = nullptr;
    for (int i = 0; i < ft_face->num_charmaps; ++i)
    {
        FT_CharMap cm = ft_face->charmaps[i];
        if (cm->platform_id == 1 && cm->encoding_id == 0)
        { // Mac Roman 通常 platform=1, encoding=0
            found = cm;
            break;
        }
    }
    if (found)
    {
        error = FT_Set_Charmap(ft_face, found);
        if (!error)
        {
            std::println("  Selected Mac Roman charmap. Now getting glyph index for 'A' "
                         "(ASCII 65)");
            FT_UInt idx = FT_Get_Char_Index(ft_face, 65); // ASCII 'A'
            std::println("    Glyph index = {}", idx);
            // 切换回 Unicode
            FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE);
        }
    }
    else
    {
        std::println("  Mac Roman charmap not found.");
    }

    // 演示通过 FT_Select_Charmap 选择 Unicode
    error = FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE);
    std::println("  Re-selected Unicode charmap via FT_Select_Charmap");

    // -------------------- d. 字形变换 --------------------
    std::println("\n--- d. Glyph Transformations ---");
    // 设置一个 30 度旋转矩阵（需要数学库，这里使用近似值）
    double angle = 30.0 * 3.14159265 / 180.0;
    FT_Matrix matrix;
    matrix.xx = (FT_Fixed)(std::cos(angle) * 0x10000L);
    matrix.xy = (FT_Fixed)(-std::sin(angle) * 0x10000L);
    matrix.yx = (FT_Fixed)(std::sin(angle) * 0x10000L);
    matrix.yy = (FT_Fixed)(std::cos(angle) * 0x10000L);
    FT_Vector delta = {0, 0}; // 无平移

    FT_Set_Transform(ft_face, &matrix, &delta);
    std::println("  Set rotation by 30 degrees.");

    // 加载并渲染同一个字符 'A'，观察变换效果
    FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
    FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);
    save_ft_bitmap(ft_face->glyph->bitmap, lib, "ch6_A_rotated.bmp");
    std::println("  Saved ch6_A_rotated.bmp (rotated 30 deg)");

    // 恢复变换（设置为 NULL 以清除）
    FT_Set_Transform(ft_face, nullptr, nullptr);
    std::println("  Transform reset.");

    std::println("\nChapter 6 test completed. Inspect the generated BMP files to see "
                 "different loading and rendering effects.");
}

// ==================== 第七章：简单文本渲染 ====================
// 辅助函数：将 UTF-32 字符串渲染为 BMP 图片，使用给定的 face 和设置
void render_text_to_bmp(FT_Face face, FT_Library lib, const std::vector<char32_t> &text,
                        const std::string &filename, int pixel_size,
                        FT_Matrix *matrix = nullptr, bool use_load_char = false)
{
    // 设置像素大小
    FT_Set_Pixel_Sizes(face, 0, pixel_size);

    // 第一次遍历：收集每个字形的位图和位置，同时计算整体画布边界
    struct GlyphInfo
    {
        FT_Bitmap bitmap;
        int x, y; // 在画布上的最终位置（相对于画布左上角）
        int width, rows;
    };
    std::vector<GlyphInfo> glyphs;

    FT_Vector pen = {50 * 64, 50 * 64}; // 起始笔位置（26.6 坐标），留出边距
    int min_x = INT_MAX, min_y = INT_MAX, max_x = 0, max_y = 0;

    for (char32_t ch : text)
    {
        FT_UInt glyph_index = FT_Get_Char_Index(face, ch);
        if (glyph_index == 0)
        {
            std::println("  Skipping U+{:04X} (missing glyph)",
                         static_cast<unsigned>(ch));
            continue;
        }

        // 加载并渲染（根据 use_load_char 选择不同方式）
        FT_Error error;
        if (use_load_char)
        {
            error = FT_Load_Char(face, ch, FT_LOAD_RENDER);
        }
        else
        {
            error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
            if (!error)
                error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        }
        if (error)
        {
            std::println("  Failed to load/render U+{:04X}", static_cast<unsigned>(ch));
            continue;
        }

        FT_GlyphSlot slot = face->glyph;
        FT_Bitmap &bmp = slot->bitmap;

        // 计算该字形在画布上的位置（相对于当前笔位置）
        int x_pos = (pen.x >> 6) + slot->bitmap_left;
        int y_pos = (pen.y >> 6) - slot->bitmap_top;

        // 更新边界
        min_x = std::min(min_x, x_pos);
        min_y = std::min(min_y, y_pos);
        max_x = std::max(max_x, x_pos + static_cast<int>(bmp.width));
        max_y = std::max(max_y, y_pos + static_cast<int>(bmp.rows));

        // 保存位图副本（注意 FT_Bitmap 只包含指针，需要复制像素数据）
        FT_Bitmap bmp_copy;
        FT_Bitmap_Init(&bmp_copy);
        FT_Bitmap_Copy(lib, &bmp, &bmp_copy); // 复制位图数据，需要释放

        glyphs.push_back({bmp_copy, x_pos, y_pos, static_cast<int>(bmp.width),
                          static_cast<int>(bmp.rows)});

        // 更新笔位置（advance 是 26.6 坐标）
        pen.x += slot->advance.x;
        pen.y += slot->advance.y;
    }

    if (glyphs.empty())
    {
        std::println("No glyphs rendered.");
        return;
    }

    // 创建画布（灰度，背景白色 255）
    int canvas_width = max_x - min_x;
    int canvas_height = max_y - min_y;
    std::vector<uint8_t> canvas(canvas_width * canvas_height, 255); // 白色背景

    // 第二次遍历：将每个字形绘制到画布上
    for (auto &g : glyphs)
    {
        int dest_x = g.x - min_x;
        int dest_y = g.y - min_y;

        // 复制位图数据，同时反转灰度：FreeType 灰度
        // 0=背景（白），255=前景（黑），但我们想要白底黑字，所以用 255 - src
        for (int row = 0; row < g.rows; ++row)
        {
            const uint8_t *src_row = g.bitmap.buffer + row * g.bitmap.pitch;
            uint8_t *dst_row = canvas.data() + (dest_y + row) * canvas_width + dest_x;
            for (int col = 0; col < g.width; ++col)
            {
                dst_row[col] = 255 - src_row[col]; // 反转
            }
        }

        // 释放复制的位图
        FT_Bitmap_Done(lib, &g.bitmap);
    }

    // 将画布包装为 FT_Bitmap 并保存
    FT_Bitmap canvas_bitmap;
    canvas_bitmap.width = canvas_width;
    canvas_bitmap.rows = canvas_height;
    canvas_bitmap.pitch = canvas_width; // 无填充
    canvas_bitmap.buffer = canvas.data();
    canvas_bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    canvas_bitmap.num_grays = 256;

    save_ft_bitmap(canvas_bitmap, lib, filename);
    std::println("Saved text rendering to {}", filename);
}

// 第七章主测试函数
void test_chapter7(FT_Library lib, const std::string &font_path)
{
    std::println("\n========== Chapter 7: Simple Text Rendering ==========");
    freetype_face face(lib, font_path);
    FT_Face ft_face = *face;

    // 选择 Unicode charmap
    FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE);

    // 将输入文本转换为 UTF-32
    auto utf32 = utf8_to_utf32(INPUT_TEXT);
    std::println("Input text: {}", INPUT_TEXT);
    std::println("UTF-32 length: {} characters", utf32.size());

    // a. 基础版（FT_Load_Glyph + FT_Render）
    std::println("\n--- a. Basic rendering (explicit FT_Load_Glyph + FT_Render) ---");
    render_text_to_bmp(ft_face, lib, utf32, "ch7_basic_24px.bmp", 24, nullptr, false);

    // b. 精炼版（FT_Load_Char with FT_LOAD_RENDER）
    std::println("\n--- b. Refined rendering (FT_Load_Char with FT_LOAD_RENDER) ---");
    render_text_to_bmp(ft_face, lib, utf32, "ch7_refined_24px.bmp", 24, nullptr, true);

    // 对比不同像素大小（12px, 36px）
    std::println("\n--- Different pixel sizes ---");
    render_text_to_bmp(ft_face, lib, utf32, "ch7_basic_12px.bmp", 12, nullptr, false);
    render_text_to_bmp(ft_face, lib, utf32, "ch7_basic_36px.bmp", 36, nullptr, false);

    // c. 变换版（旋转）
    std::println("\n--- c. Transformed rendering (rotation) ---");
    // 设置旋转矩阵（15度）
    double angle = 15.0 * 3.14159265 / 180.0;
    FT_Matrix matrix;
    matrix.xx = (FT_Fixed)(std::cos(angle) * 0x10000L);
    matrix.xy = (FT_Fixed)(-std::sin(angle) * 0x10000L);
    matrix.yx = (FT_Fixed)(std::sin(angle) * 0x10000L);
    matrix.yy = (FT_Fixed)(std::cos(angle) * 0x10000L);

    // 注意：变换版需要将矩阵应用到每个字符，我们在 render_text_to_bmp
    // 中尚未支持传递矩阵。 为了演示，我们重新实现一个带变换的版本，因为 FT_Set_Transform
    // 影响后续所有加载。 这里简单地在循环前设置变换，并在每个字符前重新设置（因为 pen
    // 位置也会通过变换调整）。 但为了简化，我们编写一个专用函数或直接在此处实现。
    // 我们选择在 render_text_to_bmp 基础上增加矩阵参数，但注意 FT_Set_Transform
    // 的平移部分需要与 pen 结合。
    // 更准确的做法是使用与教程一致的方法：在循环内设置变换，pen 用 FT_Vector 存储。
    // 为保持代码清晰，下面单独实现旋转渲染：

    std::println("\n--- Rotated rendering (15 degrees) ---");
    FT_Set_Pixel_Sizes(ft_face, 0, 24);
    FT_Vector pen = {50 * 64, 50 * 64}; // 起始笔（笛卡尔空间）
    // 用于收集 glyphs 并绘制到画布（类似 render_text_to_bmp 但使用变换）
    struct GlyphInfoTrans
    {
        FT_Bitmap bitmap;
        int x, y; // 变换后的位图在画布上的位置（像素）
    };
    std::vector<GlyphInfoTrans> trans_glyphs;
    int min_xt = INT_MAX, min_yt = INT_MAX, max_xt = 0, max_yt = 0;

    for (char32_t ch : utf32)
    {
        FT_Set_Transform(ft_face, &matrix, &pen);
        FT_UInt glyph_index = FT_Get_Char_Index(ft_face, ch);
        if (glyph_index == 0)
            continue;
        FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
        FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);

        FT_GlyphSlot slot = ft_face->glyph;
        FT_Bitmap &bmp = slot->bitmap;

        // 变换后，位图的 bitmap_left/top 已经考虑了平移和旋转，可直接作为画布坐标
        int x_pos = slot->bitmap_left;
        int y_pos = slot->bitmap_top;

        min_xt = std::min(min_xt, x_pos);
        min_yt = std::min(
            min_yt,
            y_pos - static_cast<int>(
                        bmp.rows)); // 注意 y_top 是 y_pos，y_bottom = y_pos - rows
        max_xt = std::max(max_xt, x_pos + static_cast<int>(bmp.width));
        max_yt = std::max(max_yt, y_pos);

        FT_Bitmap bmp_copy;
        FT_Bitmap_Init(&bmp_copy);
        FT_Bitmap_Copy(lib, &bmp, &bmp_copy);
        trans_glyphs.push_back({bmp_copy, x_pos, y_pos});

        // 更新笔位置
        pen.x += slot->advance.x;
        pen.y += slot->advance.y;
    }

    if (!trans_glyphs.empty())
    {
        // 画布边界调整（确保包含所有位图）
        int canvas_w = max_xt - min_xt;
        int canvas_h = max_yt - min_yt;
        std::vector<uint8_t> canvas_t(canvas_w * canvas_h, 255);

        for (auto &g : trans_glyphs)
        {
            int dest_x = g.x - min_xt;
            int dest_y =
                (max_yt -
                 g.y); // 因为 y 坐标是 bitmap_top（笛卡尔），画布 Y 向下，所以翻转
            for (int row = 0; row < static_cast<int>(g.bitmap.rows); ++row)
            {
                const uint8_t *src_row = g.bitmap.buffer + row * g.bitmap.pitch;
                uint8_t *dst_row = canvas_t.data() + (dest_y + row) * canvas_w + dest_x;
                for (int col = 0; col < static_cast<int>(g.bitmap.width); ++col)
                {
                    dst_row[col] = 255 - src_row[col];
                }
            }
            FT_Bitmap_Done(lib, &g.bitmap);
        }

        FT_Bitmap canvas_bmp;
        canvas_bmp.width = canvas_w;
        canvas_bmp.rows = canvas_h;
        canvas_bmp.pitch = canvas_w;
        canvas_bmp.buffer = canvas_t.data();
        canvas_bmp.pixel_mode = FT_PIXEL_MODE_GRAY;
        canvas_bmp.num_grays = 256;
        save_ft_bitmap(canvas_bmp, lib, "ch7_rotated_24px.bmp");
        std::println("Saved rotated text to ch7_rotated_24px.bmp");
    }

    std::println("\nChapter 7 test completed. Compare the generated BMP files:");
    std::println("  - ch7_basic_12px.bmp, ch7_basic_24px.bmp, ch7_basic_36px.bmp: "
                 "不同像素大小对比");
    std::println("  - ch7_basic_24px.bmp vs ch7_refined_24px.bmp: 两种方法应完全相同");
    std::println("  - ch7_rotated_24px.bmp: 旋转15度效果");
}

int main()
try
{
    freetype_loader loader{};
    freetype_face face{*loader, UNICODE_FONT_PATH};

    test_chapter4(*loader, "C:/Windows/Fonts/arial.ttf");

    {
        auto *lib = *loader;
        freetype_face face(lib, EMOJI_FONT_PATH);
        FT_Face ft_face = *face;
        // 6. 可选：如果有颜色字体（例如 emoji），尝试加载一个彩色字符（如 U+1F600）
        if (FT_HAS_COLOR(ft_face))
        {
            FT_ULong emoji = U'😀'; // 😀
            FT_UInt idx = FT_Get_Char_Index(ft_face, emoji);
            if (idx != 0)
            {
                FT_Set_Pixel_Sizes(ft_face, 0, 64);

                // 添加：打印设置的 ppem
                std::println("  Set pixel size: x_ppem={}, y_ppem={}",
                             ft_face->size->metrics.x_ppem,
                             ft_face->size->metrics.y_ppem);

                FT_Load_Glyph(ft_face, idx,
                              FT_LOAD_COLOR); // 使用 FT_LOAD_COLOR 加载彩色 glyph
                // 彩色 glyph 渲染后可能是 FT_PIXEL_MODE_BGRA 格式
                FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);

                // 添加：打印位图尺寸和字形度量
                std::println("  Glyph bitmap size: {}x{}", ft_face->glyph->bitmap.width,
                             ft_face->glyph->bitmap.rows);
                std::println("  Glyph metrics: advance.x={} (in 1/64px), bitmap_left={}, "
                             "bitmap_top={}",
                             ft_face->glyph->advance.x, ft_face->glyph->bitmap_left,
                             ft_face->glyph->bitmap_top);

                save_ft_bitmap(ft_face->glyph->bitmap, lib, "chapter4_color_emoji.bmp");
                std::println("Saved color emoji to chapter4_color_emoji.bmp");
            }
        }
    }
    /*
    第五章主要演示了如何通过 FT_Set_Char_Size 和 FT_Set_Pixel_Sizes
    设置字符大小。我们观察到：

    点大小和分辨率共同决定像素大小（ppem），例如 12pt@72dpi 得 12px，12pt@96dpi 得 16px。

    FT_Set_Pixel_Sizes 直接指定像素大小，与对应点大小的结果一致。

    对于非整数 ppem（如 13pt@96dpi），FreeType 会四舍五入为整数，以满足 hinting
    引擎要求，渲染结果略有不同。

    size 对象中的 ascender、descender 等全局度量在设置大小后按比例缩放，用于布局计算。

    若字体有内嵌位图，FT_Set_Pixel_Sizes
    只能设置为列表中的尺寸，否则返回错误；可缩放字体则无此限制。

    这些实验验证了第五章的理论，并直观展示了字符大小设置对渲染的影响
    */
    test_chapter5(*loader, UNICODE_FONT_PATH);

    /*
    ✅ 测试结果分析
    a. 字符码到字形索引转换
    'A' → glyph index 39

    'g' → glyph index 77

    '中' → glyph index 1048（说明 msyh.ttc 中文字体包含该字符）

    '😀' → glyph index 0（字体无此字形，正确返回缺失标记）

    b. 字形加载与渲染
    加载 'A' 前，字形度量（font units）为 width=1088, height=1152, horiBearingY=1152,
    horiAdvance=1088，与 units_per_EM=2048 相符。

    渲染后得到 17×18 像素的位图（24px ppem 下字形实际大小），bitmap_left=0, bitmap_top=18
    表示基准线位置。

    FT_LOAD_NO_BITMAP 无影响（字体无内嵌位图），结果与默认一致。

    单色渲染成功生成 ch6_A_mono.bmp，修正后的 save_ft_bitmap 正确保存为 8 位索引
    BMP，黑白分明，无崩溃。

    c. 使用其他 Charmaps
    字体有 3 个 charmaps（platform/encoding）：(0,3)、(3,1)、(3,10)，对应 Unicode 等编码。

    Mac Roman (platform=1, encoding=0) 不存在，查找失败（正常）。

    重新选择 Unicode charmap 成功。

    d. 字形变换
    30° 旋转后，位图尺寸变为 18×21（比原始 17×18 略大，因旋转后边界扩展），符合预期。

    清除变换后，后续加载不受影响。

    📌 第六章小结
    通过本章测试，您已经掌握：

    字符码与字形索引的转换：FT_Get_Char_Index 用法及缺失字形的处理（返回 0）。

    字形加载与渲染：FT_Load_Glyph 获取字形轮廓/度量，FT_Render_Glyph 生成位图，理解
    glyph->metrics 与最终位图尺寸的关系。

    不同加载标志：如 FT_LOAD_NO_BITMAP（强制使用轮廓）和不同渲染模式（单色 vs 抗锯齿）。

    Charmap 管理：枚举所有 charmap、尝试选择特定编码、切换回 Unicode。

    字形变换：通过 FT_Set_Transform 应用旋转矩阵，观察变换对位图的影响。
    */
    test_chapter6(*loader, UNICODE_FONT_PATH); // 第六章测试

    // NOTE: 将上述过程循环应用于整个字符串，通过累加 advance
    // NOTE:  实现字符流式布局，并通过画布合成得到完整文本图像
    /*
    🔍 数据背后的知识点
    1. 像素大小与画布尺寸的线性关系（第五章）
    12px → 153×12

    24px → 303×22

    36px → 456×35

    画布宽度和高度随像素大小近似线性增长（12px 到 24px 宽度 ×2.0，高度 ×1.83；24px 到 36px
    宽度 ×1.5，高度 ×1.59）。这是因为：

    FT_Set_Pixel_Sizes 设置的是 EM 正方形的像素尺寸，每个字形的位图尺寸和 advance
    会按比例缩放。

    高度主要受字体度量的 ascender 和 descender 缩放后影响（见第五章最后打印的
    ascender=34px, descender=-9px 等，在 32px 设置下总高度 34+9=43px，与 36px
    下实际画布高度 35px 接近，因为字符串中并非所有字符都触及 ascender/descender 极限）。

    2. 两种编码方式的等价性（第七章 a/b）
    基础版：显式 FT_Get_Char_Index → FT_Load_Glyph → FT_Render

    精炼版：FT_Load_Char 配合 FT_LOAD_RENDER
    两者生成的画布尺寸完全一致（303×22），验证了 FT_LOAD_RENDER
    是前三个步骤的快捷方式，且内部渲染逻辑相同。这是第六章内容的直接复用。

    3. 旋转变换对布局的剧烈影响（第七章 c）
    旋转 15° 后，画布尺寸从 303×22 变为 343×146，高度膨胀近 6 倍！
    原因：

    每个字形旋转后，其位图边界不再与水平基线对齐，导致整体垂直范围大幅增加。

    FT_Set_Transform 不仅旋转字形，还将 advance
    向量也旋转，所以字符间的水平间距变成了倾斜方向上的步进，但最终画布需容纳所有旋转后的位图，因此宽度略有增加（303→343），高度则因为旋转后字形上下延伸而剧增（22→146）。

    这直观展示了变换与 hinting
    无关，且旋转会严重扩大渲染区域，为后续高级排版（如第二部分）埋下伏笔。

    4. 缺失字形的处理（第六章 a）
    Emoji 字符索引为 0，渲染循环跳过并提示，体现了 FreeType 对缺失字形的约定：返回 0
    表示“缺失字形”，通常由应用程序显示占位符（如方框）。本例中直接跳过，保证流程继续。

    🧠 与第四、五、六章的联系
    第四章（访问 face 数据）：虽然未直接打印，但字体度量（如 ascender,
    descender）隐含在画布高度的计算中。例如 36px 下高度 35px，与第五章打印的 32px 设置下的
    ascender=34px, descender=-9px（总高
    43px）趋势一致——实际字符串高度小于理论最大高度，因为文本中没有同时包含极高和极低的字符。

    第五章（设置像素大小）：直接决定了
    ppem，进而控制所有字形的缩放。三个不同像素大小的输出数据是第五章最直观的验证。

    第六章（加载字形）：每个字符的 bitmap_left, bitmap_top, advance
    在循环中被用于定位，缺失字符处理也来自第六章。旋转版更展示了变换对 advance
    和位图位置的影响。

    🖼️ 可视化验证建议
    打开生成的 BMP 文件，可以观察到：

    12px vs 24px vs 36px：字形清晰度、笔画粗细、间距随像素增大而更平滑。

    24px 基础版 vs 精炼版：两个文件完全一致，肉眼无法区分。

    旋转版：文本倾斜
    15°，且整体位置居中，可以清晰看到每个字形都发生了旋转，且相互之间的相对位置仍然保持（因为
    advance 也被旋转了），但整体画布变得很高，需要更大的垂直空间。
    */
    test_chapter7(*loader, UNICODE_FONT_PATH);

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