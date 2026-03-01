

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

// ==================== 辅助绘图函数（BGRA 版本，接受画布尺寸）====================
void draw_pixel_bgra(uint8_t *buffer, int pitch, int x, int y, uint32_t color,
                     int canvas_w, int canvas_h)
{
    if (x < 0 || x >= canvas_w || y < 0 || y >= canvas_h)
        return;
    uint32_t *pixel = (uint32_t *)(buffer + y * pitch);
    pixel[x] = color;
}

void draw_line_bgra(uint8_t *buffer, int pitch, int x0, int y0, int x1, int y1,
                    uint32_t color, int canvas_w, int canvas_h)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (true)
    {
        if (x0 >= 0 && x0 < canvas_w && y0 >= 0 && y0 < canvas_h)
            ((uint32_t *)(buffer + y0 * pitch))[x0] = color;
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
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

void draw_dashed_line_bgra(uint8_t *buffer, int pitch, int x0, int y0, int x1, int y1,
                           uint32_t color, int canvas_w, int canvas_h, int dash_len = 5,
                           int gap_len = 3)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = std::max(abs(dx), abs(dy));
    if (steps == 0)
        return;
    float step_x = dx / (float)steps;
    float step_y = dy / (float)steps;
    bool draw = true;
    int cnt = 0;
    for (int i = 0; i <= steps; ++i)
    {
        int x = x0 + (int)(i * step_x + 0.5f);
        int y = y0 + (int)(i * step_y + 0.5f);
        if (x >= 0 && x < canvas_w && y >= 0 && y < canvas_h)
        {
            if (draw)
                ((uint32_t *)(buffer + y * pitch))[x] = color;
        }
        cnt++;
        if (draw && cnt >= dash_len)
        {
            draw = false;
            cnt = 0;
        }
        else if (!draw && cnt >= gap_len)
        {
            draw = true;
            cnt = 0;
        }
    }
}

void draw_arrow_line_bgra(uint8_t *buffer, int pitch, int x0, int y0, int x1, int y1,
                          uint32_t color, int canvas_w, int canvas_h, int arrow_len = 8)
{
    draw_line_bgra(buffer, pitch, x0, y0, x1, y1, color, canvas_w, canvas_h);
    // 计算方向
    int dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1)
        return;
    float ux = dx / len, uy = dy / len;
    // 箭头两条斜线的端点
    int ax1 = x1 - (int)(arrow_len * ux - arrow_len * 0.3f * uy);
    int ay1 = y1 - (int)(arrow_len * uy + arrow_len * 0.3f * ux);
    int ax2 = x1 - (int)(arrow_len * ux + arrow_len * 0.3f * uy);
    int ay2 = y1 - (int)(arrow_len * uy - arrow_len * 0.3f * ux);
    draw_line_bgra(buffer, pitch, x1, y1, ax1, ay1, color, canvas_w, canvas_h);
    draw_line_bgra(buffer, pitch, x1, y1, ax2, ay2, color, canvas_w, canvas_h);
}

void draw_text_bgra(FT_Library lib, FT_Face face, const std::string &text, int x, int y,
                    uint8_t *buffer, int pitch, uint32_t color, int canvas_w,
                    int canvas_h, int pixel_size)
{
    FT_Set_Pixel_Sizes(face, 0, pixel_size);
    int pen_x = x, pen_y = y;
    for (char ch : text)
    {
        FT_UInt glyph_index = FT_Get_Char_Index(face, ch);
        if (!glyph_index)
            continue;
        FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        FT_GlyphSlot slot = face->glyph;
        FT_Bitmap &bmp = slot->bitmap;
        int dst_x = pen_x + slot->bitmap_left;
        int dst_y = pen_y - slot->bitmap_top;
        for (unsigned int row = 0; row < bmp.rows; ++row)
        {
            int canvas_row = dst_y + row;
            if (canvas_row < 0 || canvas_row >= canvas_h)
                continue;
            uint8_t *src = bmp.buffer + row * bmp.pitch;
            uint32_t *dst = (uint32_t *)(buffer + canvas_row * pitch) + dst_x;
            for (unsigned int col = 0; col < bmp.width; ++col)
            {
                if (dst_x + col >= 0 && dst_x + col < canvas_w && src[col] > 128)
                {
                    dst[col] = color;
                }
            }
        }
        pen_x += slot->advance.x >> 6;
    }
}

// ==================== test_chapter_g_glyph_metrics 重构版 ====================
void test_chapter_g_glyph_metrics(FT_Library lib, const std::string &filename)
{
    std::println("test_chapter_g_glyph_metrics with path: {}", filename);

    // 两个独立 face
    freetype_face face_h_(lib, filename);
    freetype_face face_v_(lib, filename);
    FT_Face face_h = *face_h_;
    FT_Face face_v = *face_v_;

    // 一个小字体的 face 用于绘制文字标注
    freetype_face face_text_(lib, filename);
    FT_Face face_text = *face_text_;

    if (!FT_HAS_VERTICAL(face_v))
    {
        std::terminate();
    }

    FT_ULong char_code = U'g';
    FT_UInt glyph_index_h = FT_Get_Char_Index(face_h, char_code);
    FT_UInt glyph_index_v = FT_Get_Char_Index(face_v, char_code);

    // 画布尺寸增大至 1200x800
    const int canvas_w = 1200, canvas_h = 800;
    const int target_min = static_cast<int>(std::min(canvas_w, canvas_h) * 0.75); // 600

    // 确定像素大小
    FT_Set_Pixel_Sizes(face_h, 0, 100);
    FT_Load_Glyph(face_h, glyph_index_h, FT_LOAD_DEFAULT);
    int bbox_h_px = face_h->glyph->metrics.height / 64;
    int bbox_w_px = face_h->glyph->metrics.width / 64;
    int desired_ppem = 100 * target_min / std::max(bbox_h_px, bbox_w_px);
    desired_ppem = std::clamp(desired_ppem, 50, 500);
    std::println("选择像素大小: {} (目标尺寸: {} 像素)", desired_ppem, target_min);

    // ---------- 水平布局 ----------
    FT_Set_Pixel_Sizes(face_h, 0, desired_ppem);
    FT_Load_Glyph(face_h, glyph_index_h, FT_LOAD_DEFAULT);
    FT_Render_Glyph(face_h->glyph, FT_RENDER_MODE_NORMAL);
    FT_GlyphSlot slot_h = face_h->glyph;
    FT_Bitmap bmp_h = slot_h->bitmap;
    int left_h = slot_h->bitmap_left;
    int top_h = slot_h->bitmap_top;

    FT_Glyph_Metrics &hmet = slot_h->metrics;
    std::println("\n--- 水平布局 ---");
    std::println("horiBearingX: {} ({} px)", hmet.horiBearingX, hmet.horiBearingX / 64);
    std::println("horiBearingY: {} ({} px)", hmet.horiBearingY, hmet.horiBearingY / 64);
    std::println("bitmap_left = {}, bitmap_top = {}", left_h, top_h);
    std::println("位图尺寸: {}x{}", bmp_h.width, bmp_h.rows);

    // ---------- 垂直布局 ----------
    FT_Set_Pixel_Sizes(face_v, 0, desired_ppem);
    FT_Load_Glyph(face_v, glyph_index_v, FT_LOAD_VERTICAL_LAYOUT);
    FT_Render_Glyph(face_v->glyph, FT_RENDER_MODE_NORMAL);
    FT_GlyphSlot slot_v = face_v->glyph;
    FT_Bitmap bmp_v = slot_v->bitmap;
    int left_v = slot_v->bitmap_left;
    int top_v = slot_v->bitmap_top;

    FT_Glyph_Metrics &vmet = slot_v->metrics;
    std::println("\n--- 垂直布局 ---");
    std::println("vertBearingX: {} ({} px)", vmet.vertBearingX, vmet.vertBearingX / 64);
    std::println("vertBearingY: {} ({} px)", vmet.vertBearingY, vmet.vertBearingY / 64);
    std::println("bitmap_left = {}, bitmap_top = {}", left_v, top_v);
    std::println("位图尺寸: {}x{}", bmp_v.width, bmp_v.rows);

    // 手动计算的垂直偏移（使用度量值）
    int left_v_manual = vmet.vertBearingX / 64;
    int top_v_manual = vmet.vertBearingY / 64;

    // ========== 动态计算原点，使字形完整且原点在画布内 ==========
    auto compute_origin = [&](int left, int top, int w, int h, int &ox, int &oy,
                              bool vertical) -> bool {
        int ox_min = std::max(0, -left);
        int ox_max = std::min(canvas_w - 1, canvas_w - left - w);
        if (ox_min > ox_max)
            return false;

        int oy_min, oy_max;
        if (vertical)
        {
            oy_min = std::max(0, -top);
            oy_max = std::min(canvas_h - 1, canvas_h - top - h);
        }
        else
        {
            oy_min = std::max(top, 0);
            oy_max = std::min(canvas_h - 1, canvas_h - h + top);
        }
        if (oy_min > oy_max)
            return false;

        ox = (ox_min + ox_max) / 2;
        oy = (oy_min + oy_max) / 2;
        return true;
    };

    int origin_x_h, origin_y_h, origin_x_v, origin_y_v;
    bool ok_h = compute_origin(left_h, top_h, bmp_h.width, bmp_h.rows, origin_x_h,
                               origin_y_h, false);
    bool ok_v = compute_origin(left_v_manual, top_v_manual, bmp_v.width, bmp_v.rows,
                               origin_x_v, origin_y_v, true);

    if (!ok_h)
    {
        std::println("警告：水平字形太大，使用固定原点 (300,500)");
        origin_x_h = 300;
        origin_y_h = 500;
    }
    if (!ok_v)
    {
        std::println("警告：垂直字形太大，使用固定原点 (300,500)");
        origin_x_v = 300;
        origin_y_v = 500;
    }

    std::println("水平原点: ({}, {})", origin_x_h, origin_y_h);
    std::println("垂直原点: ({}, {})", origin_x_v, origin_y_v);

    // ========== 创建 BGRA 画布 ==========
    const int pitch = canvas_w * 4;                            // 每行字节数
    std::vector<uint8_t> canvas_h_data(canvas_h * pitch, 255); // 白色背景
    FT_Bitmap canvas_h_bmp;
    canvas_h_bmp.width = canvas_w;
    canvas_h_bmp.rows = canvas_h;
    canvas_h_bmp.pitch = pitch;
    canvas_h_bmp.buffer = canvas_h_data.data();
    canvas_h_bmp.pixel_mode = FT_PIXEL_MODE_BGRA;
    canvas_h_bmp.num_grays = 0;
    canvas_h_bmp.palette_mode = 0;
    canvas_h_bmp.palette = nullptr;

    std::vector<uint8_t> canvas_v_data(canvas_h * pitch, 255);
    FT_Bitmap canvas_v_bmp = canvas_h_bmp;
    canvas_v_bmp.buffer = canvas_v_data.data();

    // 设置小字体用于标注（稍微放大以适应更大画布）
    FT_Set_Pixel_Sizes(face_text, 0, 32);

    // 定义颜色常量
    const uint32_t COLOR_WHITE = 0xFFFFFFFF;
    const uint32_t COLOR_BLACK = 0xFF000000;
    const uint32_t COLOR_AXIS = 0xFF808080;      // 灰
    const uint32_t COLOR_BEARING_X = 0xFF0000FF; // 红
    const uint32_t COLOR_BEARING_Y = 0xFF00FF00; // 绿
    const uint32_t COLOR_WIDTH = 0xFFFF0000;     // 蓝
    const uint32_t COLOR_HEIGHT = 0xFFFFFF00;    // 黄
    const uint32_t COLOR_ADVANCE = 0xFFFF00FF;   // 品红
    const uint32_t COLOR_XMIN = 0xFF00FFFF;      // 青
    const uint32_t COLOR_XMAX = 0xFF808000;      // 橄榄
    const uint32_t COLOR_YMIN = 0xFF800080;      // 紫
    const uint32_t COLOR_YMAX = 0xFF008080;      // 墨绿

    // ========== 绘制水平示意图 ==========
    {
        int dst_x = origin_x_h + left_h;
        int dst_y = origin_y_h - top_h; // 水平Y向上，画布Y向下

        // 复制字形（黑色）
        for (unsigned int row = 0; row < bmp_h.rows; ++row)
        {
            int canvas_row = dst_y + row;
            if (canvas_row < 0 || canvas_row >= canvas_h)
                continue;
            const uint8_t *src = bmp_h.buffer + row * bmp_h.pitch;
            uint32_t *dst =
                (uint32_t *)(canvas_h_bmp.buffer + canvas_row * pitch) + dst_x;
            for (unsigned int col = 0; col < bmp_h.width; ++col)
            {
                if (dst_x + col >= 0 && dst_x + col < canvas_w && src[col] < 128)
                {
                    dst[col] = COLOR_BLACK;
                }
            }
        }

        // 绘制坐标轴（灰色）
        if (origin_y_h >= 0 && origin_y_h < canvas_h)
        {
            uint32_t *line = (uint32_t *)(canvas_h_bmp.buffer + origin_y_h * pitch);
            for (int x = 0; x < canvas_w; ++x)
                line[x] = COLOR_AXIS;
        }
        if (origin_x_h >= 0 && origin_x_h < canvas_w)
        {
            for (int y = 0; y < canvas_h; ++y)
            {
                uint32_t *pixel =
                    (uint32_t *)(canvas_h_bmp.buffer + y * pitch) + origin_x_h;
                *pixel = COLOR_AXIS;
            }
        }
        // 原点标记
        for (int dy = -2; dy <= 2; ++dy)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                if (dx * dx + dy * dy <= 4)
                {
                    int x = origin_x_h + dx, y = origin_y_h + dy;
                    if (x >= 0 && x < canvas_w && y >= 0 && y < canvas_h)
                        ((uint32_t *)(canvas_h_bmp.buffer + y * pitch))[x] = COLOR_AXIS;
                }
            }
        }

        // 从原点向正方向画箭头
        draw_arrow_line_bgra(canvas_h_bmp.buffer, pitch, origin_x_h, origin_y_h,
                             origin_x_h + 25, origin_y_h, COLOR_AXIS, canvas_w, canvas_h,
                             8);
        draw_arrow_line_bgra(canvas_h_bmp.buffer, pitch, origin_x_h, origin_y_h,
                             origin_x_h, origin_y_h - 25, COLOR_AXIS, canvas_w, canvas_h,
                             8);

        // 水平度量值（像素）
        int bearingX = left_h;
        int bearingY = top_h;
        int width = bmp_h.width;
        int height = bmp_h.rows;
        int xMin = left_h;
        int xMax = left_h + width;
        int yMax = top_h;
        int yMin = top_h - height;
        int advance = hmet.horiAdvance / 64;

        int bearingX_h = left_h;
        int bearingY_h = top_h;
        int width_h = bmp_h.width;
        int height_h = bmp_h.rows;
        int xMin_h = left_h;
        int xMax_h = left_h + width_h;
        int yMax_h = top_h;
        int yMin_h = top_h - height_h;
        int advance_h = hmet.horiAdvance / 64;

        std::println("水平度量像素值:");
        std::println("  bearingX (left) = {} px", bearingX_h);
        std::println("  bearingY (top)  = {} px", bearingY_h);
        std::println("  width           = {} px", width_h);
        std::println("  height          = {} px", height_h);
        std::println("  xMin            = {} px", xMin_h);
        std::println("  xMax            = {} px", xMax_h);
        std::println("  yMax            = {} px", yMax_h);
        std::println("  yMin            = {} px", yMin_h);
        std::println("  advance         = {} px", advance_h);

        // 绘制边界框（虚线）
        int rect_x0 = origin_x_h + xMin;
        int rect_y0 = origin_y_h - yMax; // 顶部
        int rect_x1 = origin_x_h + xMax;
        int rect_y1 = origin_y_h - yMin; // 底部
        draw_dashed_line_bgra(canvas_h_bmp.buffer, pitch, rect_x0, rect_y0, rect_x1,
                              rect_y0, COLOR_XMAX, canvas_w, canvas_h);
        draw_dashed_line_bgra(canvas_h_bmp.buffer, pitch, rect_x1, rect_y0, rect_x1,
                              rect_y1, COLOR_YMAX, canvas_w, canvas_h);
        draw_dashed_line_bgra(canvas_h_bmp.buffer, pitch, rect_x0, rect_y1, rect_x1,
                              rect_y1, COLOR_XMIN, canvas_w, canvas_h);
        draw_dashed_line_bgra(canvas_h_bmp.buffer, pitch, rect_x0, rect_y0, rect_x0,
                              rect_y1, COLOR_YMIN, canvas_w, canvas_h);

        // 标注边界框数值
        draw_text_bgra(lib, face_text, "xMin:" + std::to_string(xMin), rect_x0 - 35,
                       rect_y0 + 10, canvas_h_bmp.buffer, pitch, COLOR_XMIN, canvas_w,
                       canvas_h, 32);
        draw_text_bgra(lib, face_text, "xMax:" + std::to_string(xMax), rect_x1 + 5,
                       rect_y0 + 10, canvas_h_bmp.buffer, pitch, COLOR_XMAX, canvas_w,
                       canvas_h, 32);
        draw_text_bgra(lib, face_text, "yMin:" + std::to_string(yMin), rect_x0 - 35,
                       rect_y1 - 5, canvas_h_bmp.buffer, pitch, COLOR_YMIN, canvas_w,
                       canvas_h, 32);
        draw_text_bgra(lib, face_text, "yMax:" + std::to_string(yMax), rect_x0 - 35,
                       rect_y0 - 15, canvas_h_bmp.buffer, pitch, COLOR_YMAX, canvas_w,
                       canvas_h, 32);

        // 绘制 bearingX 线并标注数值
        draw_arrow_line_bgra(canvas_h_bmp.buffer, pitch, origin_x_h, origin_y_h,
                             origin_x_h + bearingX, origin_y_h, COLOR_BEARING_X, canvas_w,
                             canvas_h);
        draw_text_bgra(lib, face_text, std::to_string(bearingX),
                       origin_x_h + bearingX / 2, origin_y_h - 15, canvas_h_bmp.buffer,
                       pitch, COLOR_BEARING_X, canvas_w, canvas_h, 32);

        // 绘制 bearingY 线并标注数值
        draw_arrow_line_bgra(canvas_h_bmp.buffer, pitch, origin_x_h, origin_y_h,
                             origin_x_h, origin_y_h - bearingY, COLOR_BEARING_Y, canvas_w,
                             canvas_h);
        draw_text_bgra(lib, face_text, std::to_string(bearingY), origin_x_h + 5,
                       origin_y_h - bearingY / 2, canvas_h_bmp.buffer, pitch,
                       COLOR_BEARING_Y, canvas_w, canvas_h, 32);

        // 绘制宽度线并标注数值（在字形上方 30 像素处）
        int width_y = origin_y_h - yMax - 30;
        draw_arrow_line_bgra(canvas_h_bmp.buffer, pitch, origin_x_h + xMin, width_y,
                             origin_x_h + xMax, width_y, COLOR_WIDTH, canvas_w, canvas_h);
        draw_text_bgra(lib, face_text, std::to_string(width),
                       origin_x_h + xMin + width / 2, width_y - 15, canvas_h_bmp.buffer,
                       pitch, COLOR_WIDTH, canvas_w, canvas_h, 32);

        // 绘制高度线并标注数值（在字形左侧 30 像素处）
        int height_x = origin_x_h + xMin - 30;
        draw_arrow_line_bgra(canvas_h_bmp.buffer, pitch, height_x, origin_y_h - yMax,
                             height_x, origin_y_h - yMin, COLOR_HEIGHT, canvas_w,
                             canvas_h);
        draw_text_bgra(lib, face_text, std::to_string(height), height_x - 35,
                       origin_y_h - (yMax + yMin) / 2, canvas_h_bmp.buffer, pitch,
                       COLOR_HEIGHT, canvas_w, canvas_h, 32);

        // 绘制 advance 线并标注数值
        draw_arrow_line_bgra(canvas_h_bmp.buffer, pitch, origin_x_h, origin_y_h,
                             origin_x_h + advance, origin_y_h, COLOR_ADVANCE, canvas_w,
                             canvas_h);
        draw_text_bgra(lib, face_text, std::to_string(advance), origin_x_h + advance / 2,
                       origin_y_h - 25, canvas_h_bmp.buffer, pitch, COLOR_ADVANCE,
                       canvas_w, canvas_h, 32);
    }

    // ========== 绘制垂直示意图 ==========
    {
        int dst_x = origin_x_v + left_v_manual;
        int dst_y = origin_y_v + top_v_manual; // 垂直Y向下，字形左上角

        // 复制字形（黑色）
        for (unsigned int row = 0; row < bmp_v.rows; ++row)
        {
            int canvas_row = dst_y + row;
            if (canvas_row < 0 || canvas_row >= canvas_h)
                continue;
            const uint8_t *src = bmp_v.buffer + row * bmp_v.pitch;
            uint32_t *dst =
                (uint32_t *)(canvas_v_bmp.buffer + canvas_row * pitch) + dst_x;
            for (unsigned int col = 0; col < bmp_v.width; ++col)
            {
                if (dst_x + col >= 0 && dst_x + col < canvas_w && src[col] < 128)
                {
                    dst[col] = COLOR_BLACK;
                }
            }
        }

        // 坐标轴（灰色）
        if (origin_y_v >= 0 && origin_y_v < canvas_h)
        {
            uint32_t *line = (uint32_t *)(canvas_v_bmp.buffer + origin_y_v * pitch);
            for (int x = 0; x < canvas_w; ++x)
                line[x] = COLOR_AXIS;
        }
        if (origin_x_v >= 0 && origin_x_v < canvas_w)
        {
            for (int y = 0; y < canvas_h; ++y)
            {
                uint32_t *pixel =
                    (uint32_t *)(canvas_v_bmp.buffer + y * pitch) + origin_x_v;
                *pixel = COLOR_AXIS;
            }
        }
        for (int dy = -2; dy <= 2; ++dy)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                if (dx * dx + dy * dy <= 4)
                {
                    int x = origin_x_v + dx, y = origin_y_v + dy;
                    if (x >= 0 && x < canvas_w && y >= 0 && y < canvas_h)
                        ((uint32_t *)(canvas_v_bmp.buffer + y * pitch))[x] = COLOR_AXIS;
                }
            }
        }

        // 原点方向箭头
        draw_arrow_line_bgra(canvas_v_bmp.buffer, pitch, origin_x_v, origin_y_v,
                             origin_x_v + 25, origin_y_v, COLOR_AXIS, canvas_w, canvas_h,
                             8);
        draw_arrow_line_bgra(canvas_v_bmp.buffer, pitch, origin_x_v, origin_y_v,
                             origin_x_v, origin_y_v + 25, COLOR_AXIS, canvas_w, canvas_h,
                             8);

        // 垂直度量值
        int bearingX = left_v_manual;
        int bearingY = top_v_manual;
        int width = bmp_v.width;
        int height = bmp_v.rows;
        int xMin = left_v_manual;
        int xMax = left_v_manual + width;
        int yMax = top_v_manual;
        int yMin = top_v_manual - height;
        int advance = vmet.vertAdvance / 64;

        {
            int bearingX_v = left_v_manual;
            int bearingY_v = top_v_manual;
            int width_v = bmp_v.width;
            int height_v = bmp_v.rows;
            int xMin_v = left_v_manual;
            int xMax_v = left_v_manual + width_v;
            int yMax_v = top_v_manual;
            int yMin_v = top_v_manual - height_v;
            int advance_v = vmet.vertAdvance / 64;

            std::println("垂直度量像素值 (基于 vertBearing):");
            std::println("  bearingX (left) = {} px", bearingX_v);
            std::println("  bearingY (top)  = {} px", bearingY_v);
            std::println("  width           = {} px", width_v);
            std::println("  height          = {} px", height_v);
            std::println("  xMin            = {} px", xMin_v);
            std::println("  xMax            = {} px", xMax_v);
            std::println("  yMax            = {} px", yMax_v);
            std::println("  yMin            = {} px", yMin_v);
            std::println("  advance         = {} px", advance_v);
        }
        // 边界框
        int rect_x0 = origin_x_v + xMin;
        int rect_y0 = origin_y_v + yMax; // 顶部
        int rect_x1 = origin_x_v + xMax;
        int rect_y1 = origin_y_v + yMin; // 底部
        draw_dashed_line_bgra(canvas_v_bmp.buffer, pitch, rect_x0, rect_y0, rect_x1,
                              rect_y0, COLOR_XMAX, canvas_w, canvas_h);
        draw_dashed_line_bgra(canvas_v_bmp.buffer, pitch, rect_x1, rect_y0, rect_x1,
                              rect_y1, COLOR_YMAX, canvas_w, canvas_h);
        draw_dashed_line_bgra(canvas_v_bmp.buffer, pitch, rect_x0, rect_y1, rect_x1,
                              rect_y1, COLOR_XMIN, canvas_w, canvas_h);
        draw_dashed_line_bgra(canvas_v_bmp.buffer, pitch, rect_x0, rect_y0, rect_x0,
                              rect_y1, COLOR_YMIN, canvas_w, canvas_h);

        draw_text_bgra(lib, face_text, "xMin:" + std::to_string(xMin), rect_x0 - 35,
                       rect_y0 + 10, canvas_v_bmp.buffer, pitch, COLOR_XMIN, canvas_w,
                       canvas_h, 32);
        draw_text_bgra(lib, face_text, "xMax:" + std::to_string(xMax), rect_x1 + 5,
                       rect_y0 + 10, canvas_v_bmp.buffer, pitch, COLOR_XMAX, canvas_w,
                       canvas_h, 32);
        draw_text_bgra(lib, face_text, "yMin:" + std::to_string(yMin), rect_x0 - 35,
                       rect_y1 - 5, canvas_v_bmp.buffer, pitch, COLOR_YMIN, canvas_w,
                       canvas_h, 32);
        draw_text_bgra(lib, face_text, "yMax:" + std::to_string(yMax), rect_x0 - 35,
                       rect_y0 - 15, canvas_v_bmp.buffer, pitch, COLOR_YMAX, canvas_w,
                       canvas_h, 32);

        // bearingX 线（可能向左）
        if (bearingX < 0)
        {
            draw_arrow_line_bgra(canvas_v_bmp.buffer, pitch, origin_x_v + bearingX,
                                 origin_y_v, origin_x_v, origin_y_v, COLOR_BEARING_X,
                                 canvas_w, canvas_h);
            draw_text_bgra(lib, face_text, std::to_string(bearingX),
                           origin_x_v + bearingX / 2, origin_y_v - 15,
                           canvas_v_bmp.buffer, pitch, COLOR_BEARING_X, canvas_w,
                           canvas_h, 32);
        }
        else
        {
            draw_arrow_line_bgra(canvas_v_bmp.buffer, pitch, origin_x_v, origin_y_v,
                                 origin_x_v + bearingX, origin_y_v, COLOR_BEARING_X,
                                 canvas_w, canvas_h);
            draw_text_bgra(lib, face_text, std::to_string(bearingX),
                           origin_x_v + bearingX / 2, origin_y_v - 15,
                           canvas_v_bmp.buffer, pitch, COLOR_BEARING_X, canvas_w,
                           canvas_h, 32);
        }

        // bearingY 线（向下）
        draw_arrow_line_bgra(canvas_v_bmp.buffer, pitch, origin_x_v, origin_y_v,
                             origin_x_v, origin_y_v + bearingY, COLOR_BEARING_Y, canvas_w,
                             canvas_h);
        draw_text_bgra(lib, face_text, std::to_string(bearingY), origin_x_v + 5,
                       origin_y_v + bearingY / 2, canvas_v_bmp.buffer, pitch,
                       COLOR_BEARING_Y, canvas_w, canvas_h, 32);

        // 宽度线（在字形下方 30 像素处）
        int width_y = origin_y_v + yMax + 30;
        draw_arrow_line_bgra(canvas_v_bmp.buffer, pitch, origin_x_v + xMin, width_y,
                             origin_x_v + xMax, width_y, COLOR_WIDTH, canvas_w, canvas_h);
        draw_text_bgra(lib, face_text, std::to_string(width),
                       origin_x_v + xMin + width / 2, width_y - 15, canvas_v_bmp.buffer,
                       pitch, COLOR_WIDTH, canvas_w, canvas_h, 32);

        // 高度线（在字形右侧 30 像素处）
        int height_x = origin_x_v + xMax + 30;
        draw_arrow_line_bgra(canvas_v_bmp.buffer, pitch, height_x, origin_y_v + yMax,
                             height_x, origin_y_v + yMin, COLOR_HEIGHT, canvas_w,
                             canvas_h);
        draw_text_bgra(lib, face_text, std::to_string(height), height_x + 5,
                       origin_y_v + (yMax + yMin) / 2, canvas_v_bmp.buffer, pitch,
                       COLOR_HEIGHT, canvas_w, canvas_h, 32);

        // advance 线（向下）
        draw_arrow_line_bgra(canvas_v_bmp.buffer, pitch, origin_x_v, origin_y_v,
                             origin_x_v, origin_y_v + advance, COLOR_ADVANCE, canvas_w,
                             canvas_h);
        draw_text_bgra(lib, face_text, std::to_string(advance), origin_x_v + 5,
                       origin_y_v + advance / 2, canvas_v_bmp.buffer, pitch,
                       COLOR_ADVANCE, canvas_w, canvas_h, 32);
    }

    // ========== 保存图像 ==========
    std::string base = filename;
    size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos)
        base = base.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos)
        base = base.substr(0, dot);

    save_ft_bitmap(canvas_h_bmp, lib, base + "_h.bmp");
    save_ft_bitmap(canvas_v_bmp, lib, base + "_v.bmp");

    std::println("已保存水平示意图: {}_h.bmp", base);
    std::println("已保存垂直示意图: {}_v.bmp", base);
}

int main()
try
{
    freetype_loader loader{};

    // 测试第二部分第一章（使用中文字体，包含垂直度量）
    // test_chapter8_glyph_metrics(*loader, UNICODE_FONT_PATH);
    // 绘制示意图 水平和垂直的
    // 第一步：“g”示意图，
    // 第二步：将文字注释也写入。先画出“g”菜写注释，注释是要远小于“g”,渲染出示意图
    // 坐标原点和坐标系，bearingX,bearingY,yMax,yMin,xMax,xMin,height,width,advance
    test_chapter_g_glyph_metrics(*loader, "C:/Windows/Fonts/NotoSansSC-VF.ttf");

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