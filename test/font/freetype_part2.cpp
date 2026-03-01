

#include <cuchar>
#include <algorithm>
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

// ==================== 第二部分第一章：字形度量 (Glyph Metrics) ====================
void test_chapter8_glyph_metrics(FT_Library lib, const std::string &font_path)
{
    std::println("\n========== Part II, Chapter 1: Glyph Metrics ==========");
    freetype_face face(lib, font_path);
    FT_Face ft_face = *face;

    // 选择 Unicode charmap
    FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE);

    // 设置一个像素大小用于缩放渲染
    FT_Set_Pixel_Sizes(ft_face, 0, 24);
    std::println("Set pixel size: 24px, ppem = {}x{}", ft_face->size->metrics.x_ppem,
                 ft_face->size->metrics.y_ppem);

    // 辅助函数：打印字形度量并保存位图
    auto print_metrics = [&](FT_UInt glyph_index, const char *name,
                             bool vertical = false) {
        if (glyph_index == 0)
        {
            std::println("  Glyph index 0 (missing), skipped.");
            return;
        }

        std::println("\n--- Metrics for '{}' (glyph index {}) ---", name, glyph_index);

        // 1. 默认加载（缩放，26.6 像素单位）
        FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
        FT_Glyph_Metrics &m = ft_face->glyph->metrics;
        std::println("  [Scaled, 26.6 pixel units]");
        std::println("    width        = {} ({} px)", m.width, m.width / 64.0);
        std::println("    height       = {} ({} px)", m.height, m.height / 64.0);
        std::println("    horiBearingX = {} ({} px)", m.horiBearingX,
                     m.horiBearingX / 64.0);
        std::println("    horiBearingY = {} ({} px)", m.horiBearingY,
                     m.horiBearingY / 64.0);
        std::println("    horiAdvance  = {} ({} px)", m.horiAdvance,
                     m.horiAdvance / 64.0);
        if (FT_HAS_VERTICAL(ft_face))
        {
            std::println("    vertBearingX = {} ({} px)", m.vertBearingX,
                         m.vertBearingX / 64.0);
            std::println("    vertBearingY = {} ({} px)", m.vertBearingY,
                         m.vertBearingY / 64.0);
            std::println("    vertAdvance  = {} ({} px)", m.vertAdvance,
                         m.vertAdvance / 64.0);
        }
        else
        {
            std::println("    (Font has no vertical metrics)");
        }

        // 渲染并保存位图（抗锯齿）
        FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);
        std::string fname = std::format("ch8_{}_scaled.bmp", name);
        save_ft_bitmap(ft_face->glyph->bitmap, lib, fname);

        // 2. 无缩放加载（字体单位）
        FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_NO_SCALE);
        FT_Glyph_Metrics &m_ns = ft_face->glyph->metrics;
        std::println("\n  [Unscaled, font units (EM={})]", ft_face->units_per_EM);
        std::println("    width        = {}", m_ns.width);
        std::println("    height       = {}", m_ns.height);
        std::println("    horiBearingX = {}", m_ns.horiBearingX);
        std::println("    horiBearingY = {}", m_ns.horiBearingY);
        std::println("    horiAdvance  = {}", m_ns.horiAdvance);
        if (FT_HAS_VERTICAL(ft_face))
        {
            std::println("    vertBearingX = {}", m_ns.vertBearingX);
            std::println("    vertBearingY = {}", m_ns.vertBearingY);
            std::println("    vertAdvance  = {}", m_ns.vertAdvance);
        }

        // 3. 线性 advance（16.16 像素单位，未舍入）
        FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT); // 重新缩放加载
        std::println("\n  [Linear advances (16.16 pixel units)]");
        std::println("    linearHoriAdvance = {} ({} px)",
                     ft_face->glyph->linearHoriAdvance,
                     ft_face->glyph->linearHoriAdvance / 65536.0);
        std::println("    linearVertAdvance = {} ({} px)",
                     ft_face->glyph->linearVertAdvance,
                     ft_face->glyph->linearVertAdvance / 65536.0);
        std::println("    (对比: 舍入后 horiAdvance = {} px)", m.horiAdvance / 64.0);

        // 4. 如果使用垂直标志加载，查看 advance 字段变化
        if (FT_HAS_VERTICAL(ft_face))
        {
            FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_VERTICAL_LAYOUT);
            std::println("\n  [Loaded with FT_LOAD_VERTICAL, advance field]");
            std::println("    advance.x = {} ({} px)", ft_face->glyph->advance.x,
                         ft_face->glyph->advance.x / 64.0);
            std::println("    advance.y = {} ({} px)", ft_face->glyph->advance.y,
                         ft_face->glyph->advance.y / 64.0);
        }
        else
        {
            // 对于无垂直度量的字体，使用 FT_LOAD_VERTICAL_LAYOUT 可能无效，但我们可以演示
            // advance 默认情况
            FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
            std::println("\n  [Default load, advance field]");
            std::println("    advance.x = {} ({} px)", ft_face->glyph->advance.x,
                         ft_face->glyph->advance.x / 64.0);
            std::println("    advance.y = {} ({} px)", ft_face->glyph->advance.y,
                         ft_face->glyph->advance.y / 64.0);
        }
    };

    // 选择测试字符：拉丁 'A' 和中文字符 '中'（后者通常有垂直度量）
    FT_ULong latin_code = U'A';
    FT_ULong chinese_code = U'中';
    FT_UInt latin_idx = FT_Get_Char_Index(ft_face, latin_code);
    FT_UInt chinese_idx = FT_Get_Char_Index(ft_face, chinese_code);

    std::println("\n--- Testing Latin character 'A' ---");
    print_metrics(latin_idx, "A");

    std::println("\n--- Testing Chinese character '中' ---");
    print_metrics(chinese_idx, "zhong");

    // 总结性说明
    std::println("\n========== Glyph Metrics Summary ==========");
    std::println("1. 缩放后的度量以 26.6 像素为单位，直接用于屏幕布局。");
    std::println("2. 无缩放度量以字体单位表示，便于进行设备无关的排版计算。");
    std::println("3. linearHoriAdvance 是未舍入的线性 advance，可用于精确的宽度累加。");
    std::println("4. advance 字段在变换后会自动更新（如旋转）。");
    std::println("5. 垂直度量仅在 FT_HAS_VERTICAL 为真时可靠。");
    std::println("\n请查看生成的 BMP 文件及控制台输出，对比不同字符的度量值。");
}

// 辅助函数：绘制一个像素（带边界检查）
static inline void draw_pixel(std::vector<uint8_t> &canvas, int x, int y,
                              int canvas_width, int canvas_height, uint8_t gray)
{
    if (x >= 0 && x < canvas_width && y >= 0 && y < canvas_height)
    {
        canvas[y * canvas_width + x] = gray;
    }
}

// Bresenham 直线算法（灰度）
static void draw_line(std::vector<uint8_t> &canvas, int x0, int y0, int x1, int y1,
                      int canvas_width, int canvas_height, uint8_t gray)
{
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true)
    {
        draw_pixel(canvas, x0, y0, canvas_width, canvas_height, gray);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void test_chapter_g_glyph_metrics2(FT_Library lib)
{
    std::println("\n========== Part II, Chapter 1: Glyph Metrics Demo (Horizontal & "
                 "Vertical) ==========");

    // 固定字体路径（Windows 系统常见字体）
    const std::string latin_font = "C:/Windows/Fonts/times.ttf"; // 水平演示用
    const std::string cjk_font = "C:/Windows/Fonts/msyh.ttc";    // 垂直演示用（微软雅黑）

    const int canvas_size = 800;
    const int target_height = static_cast<int>(canvas_size * 0.75); // 600 像素

    // 辅助函数：处理单个字体
    auto process_face = [&](FT_Face face, const std::string &face_name,
                            FT_ULong char_code, bool check_vertical,
                            const std::string &suffix, FT_Int32 load_flags) {
        FT_UInt glyph_index = FT_Get_Char_Index(face, char_code);
        if (glyph_index == 0)
        {
            std::println("错误：字体 {} 中未找到字形 U+{:04X}", face_name,
                         static_cast<unsigned>(char_code));
            return;
        }

        // 调整像素大小使字形高度接近 target_height
        int pixel_size = 400;
        FT_Bitmap bitmap;
        int final_pixel_size = pixel_size;
        const int max_attempts = 10;

        for (int attempt = 0; attempt < max_attempts; ++attempt)
        {
            FT_Set_Pixel_Sizes(face, 0, pixel_size);
            if (FT_Load_Glyph(face, glyph_index, load_flags) != 0)
                return;
            if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
                return;
            bitmap = face->glyph->bitmap;
            int current_height = bitmap.rows;
            std::println("{} 尝试 {}：pixel_size = {}，字形高度 = {}", face_name, attempt,
                         pixel_size, current_height);
            if (current_height >= target_height - 10 &&
                current_height <= target_height + 10)
            {
                final_pixel_size = pixel_size;
                break;
            }
            if (current_height > 0)
            {
                pixel_size = static_cast<int>(
                    pixel_size * (static_cast<double>(target_height) / current_height));
                if (pixel_size < 1)
                    pixel_size = 1;
            }
            final_pixel_size = pixel_size;
        }

        int w = bitmap.width;
        int h = bitmap.rows;

        // 创建灰度画布（背景白色 255）
        std::vector<uint8_t> canvas(canvas_size * canvas_size, 255);

        // 计算字形位置：居中略偏左 50 像素
        int offset_x = 50;
        int center_x = canvas_size / 2;
        int center_y = canvas_size / 2;
        int dest_x = center_x - offset_x - w / 2;
        int dest_y = center_y - h / 2;
        dest_x = std::max(0, std::min(dest_x, canvas_size - w));
        dest_y = std::max(0, std::min(dest_y, canvas_size - h));

        // 绘制字形（反转灰度：白底黑字）
        for (int row = 0; row < h; ++row)
        {
            const uint8_t *src = bitmap.buffer + row * bitmap.pitch;
            uint8_t *dst = canvas.data() + (dest_y + row) * canvas_size + dest_x;
            for (int col = 0; col < w; ++col)
            {
                dst[col] = 255 - src[col]; // 黑色前景
            }
        }

        // 计算原点画布坐标
        int bitmap_left = face->glyph->bitmap_left;
        int bitmap_top = face->glyph->bitmap_top;
        int origin_x = dest_x - bitmap_left;
        int origin_y = dest_y + bitmap_top; // 画布 y 向下为正

        // 绘制坐标系（黑色）
        draw_line(canvas, 0, origin_y, canvas_size - 1, origin_y, canvas_size,
                  canvas_size, 0); // 水平基线
        draw_line(canvas, origin_x, 0, origin_x, canvas_size - 1, canvas_size,
                  canvas_size, 0); // 垂直轴
        // 原点十字标记
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                if (abs(dx) + abs(dy) <= 2)
                    draw_pixel(canvas, origin_x + dx, origin_y + dy, canvas_size,
                               canvas_size, 0);

        // 保存图片
        std::string filename = "glyph_" + suffix + ".bmp";
        FT_Bitmap canvas_bitmap;
        canvas_bitmap.width = canvas_size;
        canvas_bitmap.rows = canvas_size;
        canvas_bitmap.pitch = canvas_size;
        canvas_bitmap.buffer = canvas.data();
        canvas_bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
        canvas_bitmap.num_grays = 256;
        save_ft_bitmap(canvas_bitmap, lib, filename);
        std::println("已保存字形渲染结果到 {}", filename);

        // 打印度量信息（仅控制台）
        FT_Glyph_Metrics &m = face->glyph->metrics;
        std::println("\n--- Glyph Metrics for '{}' (scaled, 26.6 pixel units) ---",
                     suffix);
        double width_px = m.width / 64.0;
        double height_px = m.height / 64.0;
        double horiBearingX_px = m.horiBearingX / 64.0;
        double horiBearingY_px = m.horiBearingY / 64.0;
        double horiAdvance_px = m.horiAdvance / 64.0;

        std::println("width        = {} ({} px)", m.width, width_px);
        std::println("height       = {} ({} px)", m.height, height_px);
        std::println("horiBearingX = {} ({} px)", m.horiBearingX, horiBearingX_px);
        std::println("horiBearingY = {} ({} px)", m.horiBearingY, horiBearingY_px);
        std::println("horiAdvance  = {} ({} px)", m.horiAdvance, horiAdvance_px);

        double xMin = horiBearingX_px;
        double xMax = horiBearingX_px + width_px;
        double yMax = horiBearingY_px;
        double yMin = horiBearingY_px - height_px;
        std::println("\n--- Bounding Box (relative to origin) ---");
        std::println("xMin = {:.2f} px (bearingX)", xMin);
        std::println("xMax = {:.2f} px (bearingX + width)", xMax);
        std::println("yMin = {:.2f} px (bearingY - height)", yMin);
        std::println("yMax = {:.2f} px (bearingY)", yMax);

        std::println("\n--- Origin Position on Canvas ---");
        std::println("origin (canvas coordinates) = ({}, {})", origin_x, origin_y);
        std::println("(Note: canvas origin is top-left, y increases downward)");

        if (check_vertical && FT_HAS_VERTICAL(face))
        {
            double vertBearingX_px = m.vertBearingX / 64.0;
            double vertBearingY_px = m.vertBearingY / 64.0;
            double vertAdvance_px = m.vertAdvance / 64.0;
            std::println("\n--- Vertical Metrics ---");
            std::println("vertBearingX = {} ({} px)", m.vertBearingX, vertBearingX_px);
            std::println("vertBearingY = {} ({} px)", m.vertBearingY, vertBearingY_px);
            std::println("vertAdvance  = {} ({} px)", m.vertAdvance, vertAdvance_px);

            double v_xMin = vertBearingX_px;
            double v_xMax = vertBearingX_px + width_px;
            double v_yMax = vertBearingY_px;
            double v_yMin = vertBearingY_px - height_px;
            std::println("Vertical bounding box (relative to origin):");
            std::println("  xMin = {:.2f} px (vertBearingX)", v_xMin);
            std::println("  xMax = {:.2f} px (vertBearingX + width)", v_xMax);
            std::println("  yMin = {:.2f} px (vertBearingY - height)", v_yMin);
            std::println("  yMax = {:.2f} px (vertBearingY)", v_yMax);
        }
        else if (check_vertical)
        {
            std::println("\n--- 字体无垂直度量，跳过垂直信息。---");
        }
        std::println("----------------------------------------\n");
    };

    // 水平演示：Times New Roman 的 'g'
    {
        freetype_face face(lib, latin_font);
        FT_Face ft_face = *face;
        FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE);
        process_face(ft_face, "Times New Roman", U'g', false, "horizontal",
                     FT_LOAD_DEFAULT);
    }

    // 垂直演示：微软雅黑 的 '中'
    {
        freetype_face face(lib, cjk_font);
        FT_Face ft_face = *face;
        FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE);
        process_face(ft_face, "Microsoft YaHei", U'中', true, "vertical",
                     FT_LOAD_VERTICAL_LAYOUT);
    }

    std::println("========== Demo 完成 ==========");
}

int main()
try
{
    freetype_loader loader{};

    // 测试第二部分第一章（使用中文字体，包含垂直度量）
    test_chapter8_glyph_metrics(*loader, UNICODE_FONT_PATH);
    // 绘制示意图 水平和垂直的
    // 第一步：“g”示意图，
    // 第二步：将文字注释也写入。先画出“g”菜写注释，注释是要远小于“g”,渲染出示意图
    test_chapter_g_glyph_metrics2(*loader);

    {
        FT_Face face;
        FT_New_Face(*loader, UNICODE_FONT_PATH, 0, &face);

        // 1. 必须检查是否支持垂直度量
        if (FT_HAS_VERTICAL(face))
        {
            std::println("✅ 该字体支持垂直度量");

            // 2. 加载 'g' 并检查垂直度量是否有效
            auto ret =
                FT_Load_Char(face, U'中', FT_LOAD_VERTICAL_LAYOUT | FT_LOAD_RENDER);
            std::println("ret: {},垂直加载后 bitmap_left = {}, bitmap_top = {}",
                         ret == FT_Err_Ok, face->glyph->bitmap_left,
                         face->glyph->bitmap_top);
            // 如果 left 和 top 不是 0，说明有真实的垂直度量数据

            std::println("vertBearingX = {} ({} px)", face->glyph->metrics.vertBearingX,
                         face->glyph->metrics.vertBearingX / 64.0);
            std::println("vertBearingY = {} ({} px)", face->glyph->metrics.vertBearingY,
                         face->glyph->metrics.vertBearingY / 64.0);
            std::println("vertAdvance  = {} ({} px)", face->glyph->metrics.vertAdvance,
                         face->glyph->metrics.vertAdvance / 64.0);
        }
        else
        {
            std::println("❌ 该字体不支持垂直度量");
        }
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