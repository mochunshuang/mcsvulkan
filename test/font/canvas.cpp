#include <cstdint>
#include <span>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <print>

#include <fstream>

// 包含 FreeType 头文件（略，您已有）
#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftbitmap.h>

// ========== 画布：固定 BGRA 格式 ==========
class Canvas
{
  public:
    struct Pixel
    {
        uint8_t b, g, r, a; // BGRA 顺序
    };
    static_assert(sizeof(Pixel) == 4);

    Canvas(int width, int height)
        : width_(width), height_(height), pixels_(width * height, Pixel{0, 0, 0, 255})
    {
    }

    Pixel &at(int x, int y)
    {
        return pixels_[y * width_ + x];
    }
    const Pixel &at(int x, int y) const
    {
        return pixels_[y * width_ + x];
    }

    // 清除画布为指定颜色
    void clear(Pixel color = {0, 0, 0, 255})
    {
        std::fill(pixels_.begin(), pixels_.end(), color);
    }

    // 获取原始数据（用于保存 BMP 等）
    uint8_t *data()
    {
        return reinterpret_cast<uint8_t *>(pixels_.data());
    }
    const uint8_t *data() const
    {
        return reinterpret_cast<const uint8_t *>(pixels_.data());
    }

    int width() const
    {
        return width_;
    }
    int height() const
    {
        return height_;
    }

  private:
    int width_, height_;
    std::vector<Pixel> pixels_;
};

// ========== 画笔：将 FT_Bitmap 绘制到画布 ==========
class FreeTypePen
{
  public:
    // 前景色（用于灰度位图），背景色通常来自画布现有内容
    FreeTypePen(Canvas::Pixel foreground = {0, 0, 0, 255}) : fg_(foreground) {}

    // 绘制 FT_Bitmap 到画布的 (destX, destY) 位置
    void draw(Canvas &canvas, const FT_Bitmap &bitmap, int destX, int destY) const
    {
        if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.buffer == nullptr)
            return;

        switch (bitmap.pixel_mode)
        {
        case FT_PIXEL_MODE_GRAY:
            drawGray(canvas, bitmap, destX, destY);
            break;
        case FT_PIXEL_MODE_BGRA:
            drawBGRA(canvas, bitmap, destX, destY);
            break;
        case FT_PIXEL_MODE_MONO:
            drawMono(canvas, bitmap, destX, destY);
            break;
        case FT_PIXEL_MODE_GRAY2:
            drawGray2(canvas, bitmap, destX, destY);
            break;
        case FT_PIXEL_MODE_GRAY4:
            drawGray4(canvas, bitmap, destX, destY);
            break;
        case FT_PIXEL_MODE_LCD:
            drawLCD(canvas, bitmap, destX, destY);
            break;
        case FT_PIXEL_MODE_LCD_V:
            drawLCD_V(canvas, bitmap, destX, destY);
            break;
        default:
            std::println("Unsupported pixel mode: {}", bitmap.pixel_mode);
        }
    }

  private:
    Canvas::Pixel fg_; // 前景色，用于灰度/黑白位图

    // ---------- 辅助：alpha 混合（源像素带 alpha，目标为画布像素） ----------
    static void blendPixel(Canvas::Pixel &dst, Canvas::Pixel src, uint8_t alpha)
    {
        if (alpha == 0)
            return;
        if (alpha == 255)
        {
            dst = src;
            return;
        }
        // 标准 alpha 混合： dst = src * alpha + dst * (255-alpha)
        dst.r = (src.r * alpha + dst.r * (255 - alpha)) / 255;
        dst.g = (src.g * alpha + dst.g * (255 - alpha)) / 255;
        dst.b = (src.b * alpha + dst.b * (255 - alpha)) / 255;
        dst.a = 255; // 画布保持不透明
    }

    // 处理 8 位灰度（最常见）
    void drawGray(Canvas &canvas, const FT_Bitmap &bitmap, int dx, int dy) const
    {
        for (unsigned int y = 0; y < bitmap.rows; ++y)
        {
            int canvasY = dy + y;
            if (canvasY < 0 || canvasY >= canvas.height())
                continue;
            const uint8_t *src = bitmap.buffer + y * bitmap.pitch;
            for (unsigned int x = 0; x < bitmap.width; ++x)
            {
                int canvasX = dx + x;
                if (canvasX < 0 || canvasX >= canvas.width())
                    continue;
                uint8_t alpha = src[x];
                if (alpha == 0)
                    continue;
                blendPixel(canvas.at(canvasX, canvasY), fg_, alpha);
            }
        }
    }

    // 处理 BGRA 彩色位图（直接复制，保留原色和 alpha）
    void drawBGRA(Canvas &canvas, const FT_Bitmap &bitmap, int dx, int dy) const
    {
        for (unsigned int y = 0; y < bitmap.rows; ++y)
        {
            int canvasY = dy + y;
            if (canvasY < 0 || canvasY >= canvas.height())
                continue;
            const uint8_t *src = bitmap.buffer + y * bitmap.pitch;
            for (unsigned int x = 0; x < bitmap.width; ++x)
            {
                int canvasX = dx + x;
                if (canvasX < 0 || canvasX >= canvas.width())
                    continue;
                const uint8_t *pixel = src + x * 4;
                Canvas::Pixel srcColor = {pixel[0], pixel[1], pixel[2], pixel[3]};
                blendPixel(canvas.at(canvasX, canvasY), srcColor, srcColor.a);
            }
        }
    }

    // 处理 1 位黑白：将每个 bit 转换为 0 或 255 的 alpha，用前景色
    void drawMono(Canvas &canvas, const FT_Bitmap &bitmap, int dx, int dy) const
    {
        for (unsigned int y = 0; y < bitmap.rows; ++y)
        {
            int canvasY = dy + y;
            if (canvasY < 0 || canvasY >= canvas.height())
                continue;
            const uint8_t *src = bitmap.buffer + y * bitmap.pitch;
            for (unsigned int x = 0; x < bitmap.width; ++x)
            {
                int canvasX = dx + x;
                if (canvasX < 0 || canvasX >= canvas.width())
                    continue;
                // 第 x 个像素对应 src[x/8] 的第 (7 - x%8) 位
                int byteIdx = x / 8;
                int bitIdx = 7 - (x % 8); // FT 中 MSB 是第一个像素
                uint8_t bit = (src[byteIdx] >> bitIdx) & 1;
                uint8_t alpha = bit ? 255 : 0;
                if (alpha == 0)
                    continue;
                blendPixel(canvas.at(canvasX, canvasY), fg_, alpha);
            }
        }
    }

    // 2 位灰度：将 0-3 映射到 0,85,170,255
    void drawGray2(Canvas &canvas, const FT_Bitmap &bitmap, int dx, int dy) const
    {
        // 2 位灰度每个字节存储 4 个像素（假设 bitmap.pitch 可能已对齐）
        for (unsigned int y = 0; y < bitmap.rows; ++y)
        {
            int canvasY = dy + y;
            if (canvasY < 0 || canvasY >= canvas.height())
                continue;
            const uint8_t *src = bitmap.buffer + y * bitmap.pitch;
            for (unsigned int x = 0; x < bitmap.width; ++x)
            {
                int canvasX = dx + x;
                if (canvasX < 0 || canvasX >= canvas.width())
                    continue;
                int byteIdx = x / 4;
                int shift = 2 * (3 - (x % 4)); // 高位在前，所以 3,2,1,0
                uint8_t val = (src[byteIdx] >> shift) & 0x03;
                uint8_t alpha = val * 85; // 0->0, 1->85, 2->170, 3->255
                if (alpha == 0)
                    continue;
                blendPixel(canvas.at(canvasX, canvasY), fg_, alpha);
            }
        }
    }

    // 4 位灰度：每个字节存储两个像素
    void drawGray4(Canvas &canvas, const FT_Bitmap &bitmap, int dx, int dy) const
    {
        for (unsigned int y = 0; y < bitmap.rows; ++y)
        {
            int canvasY = dy + y;
            if (canvasY < 0 || canvasY >= canvas.height())
                continue;
            const uint8_t *src = bitmap.buffer + y * bitmap.pitch;
            for (unsigned int x = 0; x < bitmap.width; ++x)
            {
                int canvasX = dx + x;
                if (canvasX < 0 || canvasX >= canvas.width())
                    continue;
                int byteIdx = x / 2;
                int shift = 4 * (1 - (x % 2)); // 高位在前
                uint8_t val = (src[byteIdx] >> shift) & 0x0F;
                uint8_t alpha = val * 17; // 0->0, 15->255
                if (alpha == 0)
                    continue;
                blendPixel(canvas.at(canvasX, canvasY), fg_, alpha);
            }
        }
    }

    // LCD 水平子像素：宽度是实际 RGB 像素的 3 倍，每 3 字节为一个 RGB 像素
    void drawLCD(Canvas &canvas, const FT_Bitmap &bitmap, int dx, int dy) const
    {
        unsigned int rgbWidth = bitmap.width / 3;
        for (unsigned int y = 0; y < bitmap.rows; ++y)
        {
            int canvasY = dy + y;
            if (canvasY < 0 || canvasY >= canvas.height())
                continue;
            const uint8_t *src = bitmap.buffer + y * bitmap.pitch;
            for (unsigned int x = 0; x < rgbWidth; ++x)
            {
                int canvasX = dx + x;
                if (canvasX < 0 || canvasX >= canvas.width())
                    continue;
                // FreeType 的 LCD 顺序是 R,G,B（从左到右）
                uint8_t r = src[x * 3 + 0];
                uint8_t g = src[x * 3 + 1];
                uint8_t b = src[x * 3 + 2];
                // 可以简单地将三个子像素合成为一个彩色像素（无额外 alpha）
                // 这里我们假设前景色已被忽略，直接使用位图颜色
                Canvas::Pixel srcColor = {b, g, r, 255}; // 转为 BGRA
                blendPixel(canvas.at(canvasX, canvasY), srcColor, 255);
            }
        }
    }

    // LCD_V 垂直子像素：高度是实际 RGB 像素的 3 倍
    void drawLCD_V(Canvas &canvas, const FT_Bitmap &bitmap, int dx, int dy) const
    {
        unsigned int rgbHeight = bitmap.rows / 3;
        for (unsigned int y = 0; y < rgbHeight; ++y)
        {
            int canvasY = dy + y;
            if (canvasY < 0 || canvasY >= canvas.height())
                continue;
            // 每个 RGB 像素由三行中的同一列组成
            const uint8_t *row_r = bitmap.buffer + (y * 3 + 0) * bitmap.pitch;
            const uint8_t *row_g = bitmap.buffer + (y * 3 + 1) * bitmap.pitch;
            const uint8_t *row_b = bitmap.buffer + (y * 3 + 2) * bitmap.pitch;
            for (unsigned int x = 0; x < bitmap.width; ++x)
            {
                int canvasX = dx + x;
                if (canvasX < 0 || canvasX >= canvas.width())
                    continue;
                uint8_t r = row_r[x];
                uint8_t g = row_g[x];
                uint8_t b = row_b[x];
                Canvas::Pixel srcColor = {b, g, r, 255};
                blendPixel(canvas.at(canvasX, canvasY), srcColor, 255);
            }
        }
    }
};

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

// ========== 全局像素混合函数 ==========
inline void blend_pixel(Canvas::Pixel &dst, Canvas::Pixel src, uint8_t alpha)
{
    if (alpha == 0)
        return;
    if (alpha == 255)
    {
        dst = src;
        return;
    }
    // 标准 alpha 混合： dst = src * alpha + dst * (255-alpha)
    dst.r = (src.r * alpha + dst.r * (255 - alpha)) / 255;
    dst.g = (src.g * alpha + dst.g * (255 - alpha)) / 255;
    dst.b = (src.b * alpha + dst.b * (255 - alpha)) / 255;
    dst.a = 255; // 画布保持不透明
}

// ========== 基本绘图函数（点、线、圆） ==========
void draw_point(Canvas &canvas, int x, int y, Canvas::Pixel color)
{
    if (x >= 0 && x < canvas.width() && y >= 0 && y < canvas.height())
    {
        blend_pixel(canvas.at(x, y), color, color.a);
    }
}

// Bresenham 画线算法
void draw_line(Canvas &canvas, int x1, int y1, int x2, int y2, Canvas::Pixel color)
{
    int dx = abs(x2 - x1);
    int dy = -abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy; // 误差项
    while (true)
    {
        draw_point(canvas, x1, y1, color);
        if (x1 == x2 && y1 == y2)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
        { // 水平步进
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx)
        { // 垂直步进
            err += dx;
            y1 += sy;
        }
    }
}

// 中点圆算法（绘制空心圆）
void draw_circle(Canvas &canvas, int cx, int cy, int r, Canvas::Pixel color)
{
    int x = 0;
    int y = r;
    int d = 1 - r; // 决策参数
    while (x <= y)
    {
        // 八个对称点
        draw_point(canvas, cx + x, cy + y, color);
        draw_point(canvas, cx - x, cy + y, color);
        draw_point(canvas, cx + x, cy - y, color);
        draw_point(canvas, cx - x, cy - y, color);
        draw_point(canvas, cx + y, cy + x, color);
        draw_point(canvas, cx - y, cy + x, color);
        draw_point(canvas, cx + y, cy - x, color);
        draw_point(canvas, cx - y, cy - x, color);

        if (d < 0)
        {
            d += 2 * x + 3;
        }
        else
        {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

// ========== 示例：使用画布和画笔 ==========
int main()
{
    // 初始化 FreeType 和字体（复用您的代码）
    freetype_loader loader;
    freetype_face face(*loader, "TiroDevanagariHindi-Regular.ttf");

    // 创建画布 600x400，白色背景
    Canvas canvas(600, 400);
    canvas.clear({255, 255, 255, 255}); // 白色

    // 创建画笔，前景色为黑色
    FreeTypePen pen({0, 0, 0, 255}); // 黑色

    // 渲染字符 'g' 到画布位置 (50, 50)
    FT_Set_Pixel_Sizes(*face, 0, 48);
    FT_UInt glyph_index = FT_Get_Char_Index(*face, 'g');
    FT_Load_Glyph(*face, glyph_index, FT_LOAD_RENDER | FT_LOAD_COLOR);
    FT_Bitmap &bitmap = (*face)->glyph->bitmap;

    pen.draw(canvas, bitmap, 50, 50);

    {
        // 测试绘制点、线、圆
        Canvas::Pixel red = {0, 0, 255, 255};   // BGRA 红色
        Canvas::Pixel green = {0, 255, 0, 255}; // BGRA 绿色
        Canvas::Pixel blue = {255, 0, 0, 255};  // BGRA 蓝色

        draw_line(canvas, 50, 50, 200, 150, red);
        draw_circle(canvas, 300, 200, 80, green);
        draw_point(canvas, 400, 100, blue);
    }

    // 保存画布为 BMP（复用之前的 save_ft_bitmap，需构造临时 FT_Bitmap）
    FT_Bitmap canvasBitmap;
    canvasBitmap.width = canvas.width();
    canvasBitmap.rows = canvas.height();
    canvasBitmap.pitch = canvas.width() * 4;
    canvasBitmap.pixel_mode = FT_PIXEL_MODE_BGRA;
    canvasBitmap.buffer = canvas.data();
    canvasBitmap.num_grays = 0;
    save_ft_bitmap(canvasBitmap, *loader, "canvas_output.bmp");

    return 0;
}