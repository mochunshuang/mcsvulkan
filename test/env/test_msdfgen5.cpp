#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include <hb.h>
#include <hb-ft.h>
#include <msdfgen.h>
#include <msdfgen-ext.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <cuchar>
#include <clocale>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cassert>

namespace fs = std::filesystem;

// -------------------- 常量定义 --------------------
constexpr float FREETYPE_FIXED_SCALE = 64.0f;
constexpr int PGM_MAX_GRAY = 255;
constexpr int DEFAULT_FONT_SIZE = 64;
constexpr int ATLAS_WIDTH = 512;
constexpr int ATLAS_HEIGHT = 512;
constexpr int GLYPH_SIZE = 32;
constexpr double SDF_RANGE = 4.0;

// 字体路径（请根据您的系统修改）
const char *UNICODE_FONT_PATH = "C:/Windows/Fonts/msyh.ttc";   // 中文字体
const char *EMOJI_FONT_PATH = "C:/Windows/Fonts/seguiemj.ttf"; // 彩色表情字体

const std::string INPUT_TEXT = "Hello, 世界! こんにちは 123 😀👍🎉";

// -------------------- UTF-8 转码 --------------------
std::vector<char32_t> utf8_to_codepoints(const std::string &utf8)
{
    std::vector<char32_t> result;
    std::mbstate_t state{};
    const char *ptr = utf8.data();
    const char *end = ptr + utf8.size();
    char32_t cp;
    while (ptr < end)
    {
        size_t rc = std::mbrtoc32(&cp, ptr, end - ptr, &state);
        if (rc == 0)
            break;
        if (rc == (size_t)-1 || rc == (size_t)-2)
            break;
        if (rc == (size_t)-3)
        {
            ptr += 1;
            continue;
        }
        result.push_back(cp);
        ptr += rc;
    }
    return result;
}

// -------------------- 安全的 BGRA 转 RGB --------------------
std::vector<uint8_t> bgra_to_rgb(const std::vector<uint8_t> &bgra, int width, int height)
{
    size_t expected_size = static_cast<size_t>(width) * height * 4;
    if (bgra.size() != expected_size)
    {
        throw std::runtime_error("bgra_to_rgb: input vector size mismatch. Expected " +
                                 std::to_string(expected_size) + ", got " +
                                 std::to_string(bgra.size()));
    }
    std::vector<uint8_t> rgb(width * height * 3);
    for (int i = 0; i < width * height; ++i)
    {
        rgb[i * 3 + 0] = bgra[i * 4 + 2];
        rgb[i * 3 + 1] = bgra[i * 4 + 1];
        rgb[i * 3 + 2] = bgra[i * 4 + 0];
    }
    return rgb;
}

// -------------------- 文件保存函数 --------------------
void savePGM(const msdfgen::Bitmap<float, 1> &bitmap, const std::string &filename)
{
    std::ofstream file(filename, std::ios::binary);
    if (!file)
    {
        std::cerr << "Cannot open " << filename << " for writing.\n";
        return;
    }
    file << "P5\n"
         << bitmap.width() << " " << bitmap.height() << "\n"
         << PGM_MAX_GRAY << "\n";
    for (int y = 0; y < bitmap.height(); ++y)
    {
        for (int x = 0; x < bitmap.width(); ++x)
        {
            float v = *bitmap(x, y);
            float normalized = (v + 1.0f) / 2.0f;
            uint8_t gray = static_cast<uint8_t>(normalized * PGM_MAX_GRAY + 0.5f);
            file.write(reinterpret_cast<const char *>(&gray), 1);
        }
    }
}

void saveGrayPGM(const std::vector<uint8_t> &data, int width, int height,
                 const std::string &filename)
{
    if (data.size() != static_cast<size_t>(width * height))
    {
        std::cerr << "saveGrayPGM: data size mismatch\n";
        return;
    }
    std::ofstream file(filename, std::ios::binary);
    if (!file)
        return;
    file << "P5\n" << width << " " << height << "\n" << PGM_MAX_GRAY << "\n";
    file.write(reinterpret_cast<const char *>(data.data()), data.size());
}

void saveColorPPM(const std::vector<uint8_t> &data, int width, int height,
                  const std::string &filename)
{
    if (data.size() != static_cast<size_t>(width * height * 3))
    {
        std::cerr << "saveColorPPM: data size mismatch\n";
        return;
    }
    std::ofstream file(filename, std::ios::binary);
    if (!file)
        return;
    file << "P6\n" << width << " " << height << "\n255\n";
    file.write(reinterpret_cast<const char *>(data.data()), data.size());
}

std::string safe_filename(char32_t codepoint)
{
    if (codepoint <= 0x7F)
    {
        char ch = static_cast<char>(codepoint);
        if (isalnum(ch))
            return std::string(1, ch);
        static const std::unordered_map<char, std::string> specialMap = {
            {' ', "space"},      {'!', "exclamation"},
            {'"', "quote"},      {'#', "hash"},
            {'$', "dollar"},     {'%', "percent"},
            {'&', "ampersand"},  {'\'', "apostrophe"},
            {'(', "left_paren"}, {')', "right_paren"},
            {'*', "asterisk"},   {'+', "plus"},
            {',', "comma"},      {'-', "minus"},
            {'.', "period"},     {'/', "slash"},
            {':', "colon"},      {';', "semicolon"},
            {'<', "less"},       {'=', "equal"},
            {'>', "greater"},    {'?', "question"},
            {'@', "at"},         {'[', "left_bracket"},
            {'\\', "backslash"}, {']', "right_bracket"},
            {'^', "caret"},      {'_', "underscore"},
            {'`', "backtick"},   {'{', "left_brace"},
            {'|', "pipe"},       {'}', "right_brace"},
            {'~', "tilde"}};
        auto it = specialMap.find(ch);
        if (it != specialMap.end())
            return it->second;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "U+%04X", static_cast<unsigned>(codepoint));
    return std::string(buf);
}

// -------------------- 字符元数据 --------------------
struct GlyphMetrics
{
    uint32_t codepoint = 0;
    uint32_t glyphIndex = 0;
    float width = 0, height = 0; // 轮廓尺寸（像素）
    float bearingX = 0, bearingY = 0;
    float advance = 0; // 水平步进（像素）
};

// 灰度字符（图集内）
struct GrayGlyph : GlyphMetrics
{
    int atlasX = 0, atlasY = 0;
    int atlasW = 0, atlasH = 0; // 图集中实际位图尺寸
    float uMin = 0, vMin = 0, uMax = 0, vMax = 0;
    int bitmap_left = 0, bitmap_top = 0; // 来自 FreeType 渲染
};

// SDF 字符（固定尺寸）
struct SdfGlyph : GlyphMetrics
{
    int atlasX = 0, atlasY = 0;
    int atlasW = 0, atlasH = 0; // 固定为 GLYPH_SIZE
    float uMin = 0, vMin = 0, uMax = 0, vMax = 0;
};

// 彩色表情字符（含位图）
struct ColorGlyph
{
    uint32_t codepoint = 0;
    uint32_t glyphIndex = 0;
    int width = 0, height = 0; // 位图尺寸
    int bitmap_left = 0, bitmap_top = 0;
    int advance_26_6 = 0;        // 26.6 格式的 advance
    std::vector<uint8_t> bitmap; // BGRA 数据
    // 图集信息（可选）
    int atlasX = 0, atlasY = 0;
    float uMin = 0, vMin = 0, uMax = 0, vMax = 0;
};

// -------------------- 字体加载器 --------------------
class FontLoader
{
  public:
    FontLoader(const std::string &filepath, int pixelSize)
    {
        if (FT_Init_FreeType(&m_library))
            throw std::runtime_error("FT_Init_FreeType failed");
        if (FT_New_Face(m_library, filepath.c_str(), 0, &m_face))
        {
            FT_Done_FreeType(m_library);
            throw std::runtime_error("FT_New_Face failed: " + filepath);
        }
        FT_Set_Pixel_Sizes(m_face, 0, pixelSize);
        m_pixelSize = pixelSize;
    }
    ~FontLoader()
    {
        if (m_face)
            FT_Done_Face(m_face);
        if (m_library)
            FT_Done_FreeType(m_library);
    }

    GlyphMetrics getGlyphMetrics(uint32_t codepoint) const
    {
        GlyphMetrics gm;
        gm.codepoint = codepoint;
        gm.glyphIndex = FT_Get_Char_Index(m_face, codepoint);
        if (gm.glyphIndex == 0)
            return gm;
        FT_Load_Glyph(m_face, gm.glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
        gm.width =
            static_cast<float>(m_face->glyph->metrics.width) / FREETYPE_FIXED_SCALE;
        gm.height =
            static_cast<float>(m_face->glyph->metrics.height) / FREETYPE_FIXED_SCALE;
        gm.bearingX = static_cast<float>(m_face->glyph->metrics.horiBearingX) /
                      FREETYPE_FIXED_SCALE;
        gm.bearingY = static_cast<float>(m_face->glyph->metrics.horiBearingY) /
                      FREETYPE_FIXED_SCALE;
        gm.advance =
            static_cast<float>(m_face->glyph->metrics.horiAdvance) / FREETYPE_FIXED_SCALE;
        return gm;
    }

    FT_Outline *getOutline(uint32_t glyphIndex)
    {
        FT_Load_Glyph(m_face, glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
        return &m_face->glyph->outline;
    }

    // 渲染灰度位图，同时返回 left 和 top
    std::vector<uint8_t> renderGrayBitmap(uint32_t glyphIndex, int &width, int &height,
                                          int &left, int &top)
    {
        FT_Error error =
            FT_Load_Glyph(m_face, glyphIndex, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL);
        if (error)
        {
            width = height = left = top = 0;
            return {};
        }
        FT_Bitmap *bitmap = &m_face->glyph->bitmap;
        if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY)
        {
            width = height = left = top = 0;
            return {};
        }
        width = bitmap->width;
        height = bitmap->rows;
        left = m_face->glyph->bitmap_left;
        top = m_face->glyph->bitmap_top;
        return {bitmap->buffer, bitmap->buffer + width * height};
    }

    // 渲染彩色位图（BGRA），若失败则回退到灰度并转换为白色带透明度
    std::vector<uint8_t> renderColorBitmap(uint32_t glyphIndex, int &width, int &height,
                                           int &left, int &top, int &advance_26_6)
    {
        // 先尝试彩色加载
        FT_Error error = FT_Load_Glyph(m_face, glyphIndex, FT_LOAD_COLOR);
        if (!error)
        {
            FT_Bitmap *bitmap = &m_face->glyph->bitmap;
            if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                width = bitmap->width;
                height = bitmap->rows;
                left = m_face->glyph->bitmap_left;
                top = m_face->glyph->bitmap_top;
                advance_26_6 = m_face->glyph->metrics.horiAdvance;
                return {bitmap->buffer, bitmap->buffer + width * height * 4};
            }
        }

        // 如果彩色失败，尝试普通灰度渲染，并转换为伪彩色（白色）
        error = FT_Load_Glyph(m_face, glyphIndex, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL);
        if (!error)
        {
            FT_Bitmap *bitmap = &m_face->glyph->bitmap;
            if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                width = bitmap->width;
                height = bitmap->rows;
                left = m_face->glyph->bitmap_left;
                top = m_face->glyph->bitmap_top;
                advance_26_6 = m_face->glyph->metrics.horiAdvance;
                std::vector<uint8_t> rgba(width * height * 4, 255); // 初始白色不透明
                for (int i = 0; i < width * height; ++i)
                {
                    uint8_t gray = bitmap->buffer[i];
                    rgba[i * 4 + 0] = 255;  // B
                    rgba[i * 4 + 1] = 255;  // G
                    rgba[i * 4 + 2] = 255;  // R
                    rgba[i * 4 + 3] = gray; // Alpha
                }
                return rgba;
            }
        }

        // 完全失败：置零输出参数并返回空向量
        width = 0;
        height = 0;
        left = 0;
        top = 0;
        advance_26_6 = 0;
        return {};
    }

    FT_Face getFace() const
    {
        return m_face;
    }

  private:
    FT_Library m_library = nullptr;
    FT_Face m_face = nullptr;
    int m_pixelSize = 0;
};

// -------------------- SDF生成器 --------------------
class SdfGenerator
{
  public:
    static msdfgen::Bitmap<float, 1> generate(const FT_Outline *outline, int size,
                                              double range)
    {
        msdfgen::Shape shape;
        if (msdfgen::readFreetypeOutline(shape, const_cast<FT_Outline *>(outline)) != 0)
            throw std::runtime_error("readFreetypeOutline failed");
        shape.normalize();
        auto bounds = shape.getBounds();
        double l = bounds.l, r = bounds.r, b = bounds.b, t = bounds.t;
        double w = r - l, h = t - b;
        double scale = (size - 2 * range) / std::max(w, h);
        double tx = (size / 2.0) - (l + w / 2) * scale;
        double ty = (size / 2.0) + (b + h / 2) * scale;
        msdfgen::Bitmap<float, 1> bitmap(size, size);
        msdfgen::generateSDF(bitmap, shape, range, scale, tx, ty);
        return bitmap;
    }
};

// -------------------- 简单图集打包器（行打包） --------------------
template <typename T>
class AtlasPacker
{
  public:
    AtlasPacker(int width, int height) : m_width(width), m_height(height)
    {
        m_data.resize(width * height * sizeof(T), 0);
        m_curX = m_curY = m_rowHeight = 0;
    }

    bool add(int itemWidth, int itemHeight, const uint8_t *itemData, int &outX, int &outY)
    {
        if (itemWidth > m_width || itemHeight > m_height)
            return false;
        if (m_curX + itemWidth > m_width)
        {
            m_curX = 0;
            m_curY += m_rowHeight;
            m_rowHeight = 0;
        }
        if (m_curY + itemHeight > m_height)
            return false;

        size_t bytesPerPixel = sizeof(T);
        for (int y = 0; y < itemHeight; ++y)
        {
            size_t dstOff = ((m_curY + y) * m_width + m_curX) * bytesPerPixel;
            size_t srcOff = y * itemWidth * bytesPerPixel;
            memcpy(m_data.data() + dstOff, itemData + srcOff, itemWidth * bytesPerPixel);
        }

        outX = m_curX;
        outY = m_curY;
        m_curX += itemWidth;
        if (itemHeight > m_rowHeight)
            m_rowHeight = itemHeight;
        return true;
    }

    const std::vector<uint8_t> &getData() const
    {
        return m_data;
    }
    int getWidth() const
    {
        return m_width;
    }
    int getHeight() const
    {
        return m_height;
    }

  private:
    int m_width, m_height;
    std::vector<uint8_t> m_data;
    int m_curX, m_curY, m_rowHeight;
};

// -------------------- 文本整形器 --------------------
class TextShaper
{
  public:
    TextShaper(FT_Face face)
    {
        m_hbFont = hb_ft_font_create(face, nullptr);
        m_buffer = hb_buffer_create();
    }
    ~TextShaper()
    {
        hb_buffer_destroy(m_buffer);
        hb_font_destroy(m_hbFont);
    }

    struct ShapedGlyph
    {
        uint32_t glyphIndex;
        float x_offset, y_offset;
        float x_advance;
    };

    std::vector<ShapedGlyph> shape(const std::string &utf8Text)
    {
        hb_buffer_clear_contents(m_buffer);
        hb_buffer_add_utf8(m_buffer, utf8Text.c_str(), -1, 0, -1);
        hb_buffer_set_direction(m_buffer, HB_DIRECTION_LTR);
        hb_buffer_set_script(m_buffer, HB_SCRIPT_LATIN);
        hb_buffer_set_language(m_buffer, hb_language_from_string("en", -1));
        hb_shape(m_hbFont, m_buffer, nullptr, 0);

        unsigned int count;
        hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(m_buffer, &count);
        hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(m_buffer, &count);
        std::vector<ShapedGlyph> result;
        result.reserve(count);
        for (unsigned i = 0; i < count; ++i)
        {
            ShapedGlyph sg;
            sg.glyphIndex = infos[i].codepoint;
            sg.x_offset = positions[i].x_offset / FREETYPE_FIXED_SCALE;
            sg.y_offset = positions[i].y_offset / FREETYPE_FIXED_SCALE;
            sg.x_advance = positions[i].x_advance / FREETYPE_FIXED_SCALE;
            result.push_back(sg);
        }
        return result;
    }

  private:
    hb_font_t *m_hbFont;
    hb_buffer_t *m_buffer;
};

// -------------------- 灰度布局渲染（单独） --------------------
void renderGrayLayout(const std::string &text,
                      const std::map<uint32_t, GrayGlyph> &grayMap,
                      const AtlasPacker<uint8_t> &grayPacker, FT_Face face,
                      const std::string &outFile)
{
    TextShaper shaper(face);
    auto shaped = shaper.shape(text);
    if (shaped.empty())
        return;

    float totalAdvance = 0;
    float maxHeight = 0;
    for (const auto &sg : shaped)
    {
        const GrayGlyph *g = nullptr;
        for (const auto &p : grayMap)
        {
            if (p.second.glyphIndex == sg.glyphIndex)
            {
                g = &p.second;
                break;
            }
        }
        if (!g)
            continue;
        totalAdvance += g->advance;
        if (g->atlasH > maxHeight)
            maxHeight = static_cast<float>(g->atlasH);
    }

    int margin = 20;
    int imgW = static_cast<int>(totalAdvance) + margin * 2;
    int imgH = static_cast<int>(maxHeight) + margin * 2;
    std::vector<uint8_t> image(imgW * imgH, 255); // 白底

    float cursorX = margin;
    float baselineY = margin + maxHeight;

    for (const auto &sg : shaped)
    {
        const GrayGlyph *g = nullptr;
        for (const auto &p : grayMap)
        {
            if (p.second.glyphIndex == sg.glyphIndex)
            {
                g = &p.second;
                break;
            }
        }
        if (!g)
        {
            cursorX += sg.x_advance;
            continue;
        }

        float x0 = cursorX + sg.x_offset + g->bitmap_left;
        float y0 = baselineY - sg.y_offset - g->bitmap_top;

        for (int y = 0; y < g->atlasH; ++y)
        {
            for (int x = 0; x < g->atlasW; ++x)
            {
                int srcIdx = (g->atlasY + y) * grayPacker.getWidth() + (g->atlasX + x);
                uint8_t val = grayPacker.getData()[srcIdx];
                if (val == 0)
                    continue;
                int dstX = static_cast<int>(x0 + x);
                int dstY = static_cast<int>(y0 + y);
                if (dstX >= 0 && dstX < imgW && dstY >= 0 && dstY < imgH)
                {
                    image[dstY * imgW + dstX] = val;
                }
            }
        }
        cursorX += g->advance;
    }
    saveGrayPGM(image, imgW, imgH, outFile);
    std::cout << "Saved grayscale layout: " << outFile << std::endl;
}

// -------------------- 彩色布局渲染（单独） --------------------
void renderColorLayout(const std::string &text,
                       const std::map<uint32_t, ColorGlyph> &colorMap, // 键为 glyphIndex
                       FT_Face face, const std::string &outFile)
{
    TextShaper shaper(face);
    auto shaped = shaper.shape(text);
    if (shaped.empty())
        return;

    float totalAdvance = 0;
    float maxHeight = 0;
    for (const auto &sg : shaped)
    {
        auto it = colorMap.find(sg.glyphIndex);
        if (it == colorMap.end())
            continue;
        totalAdvance += sg.x_advance;
        if (it->second.height > maxHeight)
            maxHeight = static_cast<float>(it->second.height);
    }

    int margin = 20;
    int imgW = static_cast<int>(totalAdvance) + margin * 2;
    int imgH = static_cast<int>(maxHeight) + margin * 2;
    std::vector<uint8_t> image(imgW * imgH * 4, 0); // BGRA 透明背景

    float cursorX = margin;
    float baselineY = margin + maxHeight;

    for (const auto &sg : shaped)
    {
        auto it = colorMap.find(sg.glyphIndex);
        if (it == colorMap.end())
        {
            cursorX += sg.x_advance;
            continue;
        }
        const ColorGlyph &cg = it->second;

        float x0 = cursorX + sg.x_offset + cg.bitmap_left;
        float y0 = baselineY - sg.y_offset - cg.bitmap_top;

        for (int y = 0; y < cg.height; ++y)
        {
            for (int x = 0; x < cg.width; ++x)
            {
                int srcIdx = (y * cg.width + x) * 4;
                uint8_t b = cg.bitmap[srcIdx + 0];
                uint8_t g = cg.bitmap[srcIdx + 1];
                uint8_t r = cg.bitmap[srcIdx + 2];
                uint8_t a = cg.bitmap[srcIdx + 3];
                if (a == 0)
                    continue;
                int dstX = static_cast<int>(x0 + x);
                int dstY = static_cast<int>(y0 + y);
                if (dstX >= 0 && dstX < imgW && dstY >= 0 && dstY < imgH)
                {
                    int dstIdx = (dstY * imgW + dstX) * 4;
                    image[dstIdx + 0] = b;
                    image[dstIdx + 1] = g;
                    image[dstIdx + 2] = r;
                    image[dstIdx + 3] = a;
                }
            }
        }
        cursorX += sg.x_advance;
    }

    auto rgb = bgra_to_rgb(image, imgW, imgH);
    saveColorPPM(rgb, imgW, imgH, outFile);
    std::cout << "Saved color layout: " << outFile << std::endl;
}

// -------------------- 集成布局渲染（灰度和彩色混合） --------------------
void renderIntegratedLayout(
    const std::string &text, const std::map<uint32_t, GrayGlyph> &grayMap,
    const AtlasPacker<uint8_t> &grayPacker,
    const std::map<uint32_t, ColorGlyph> &colorMap, // 键为 glyphIndex
    FT_Face grayFace, FT_Face colorFace, const std::string &outFile)
{
    // 分别对两个字体的整形
    TextShaper grayShaper(grayFace);
    TextShaper colorShaper(colorFace);
    auto grayShaped = grayShaper.shape(text);
    auto colorShaped = colorShaper.shape(text);

    // 构建 codepoint -> glyphIndex 映射（灰度）
    std::map<uint32_t, uint32_t> grayCpToIdx;
    for (const auto &p : grayMap)
    {
        grayCpToIdx[p.first] = p.second.glyphIndex;
    }
    // 构建 codepoint -> glyphIndex 映射（彩色）
    std::map<uint32_t, uint32_t> colorCpToIdx;
    for (const auto &p : colorMap)
    {
        colorCpToIdx[p.second.codepoint] = p.first;
    }

    // 将整形结果按 glyphIndex 映射，方便查找
    std::map<uint32_t, TextShaper::ShapedGlyph> grayShapedMap;
    for (const auto &sg : grayShaped)
        grayShapedMap[sg.glyphIndex] = sg;
    std::map<uint32_t, TextShaper::ShapedGlyph> colorShapedMap;
    for (const auto &sg : colorShaped)
        colorShapedMap[sg.glyphIndex] = sg;

    // 获取文本的 codepoint 序列
    auto cps = utf8_to_codepoints(text);

    // 估算画布尺寸
    float totalAdvance = 0;
    float maxHeight = 0;
    for (char32_t cp : cps)
    {
        uint32_t ucp = static_cast<uint32_t>(cp);
        // 优先使用灰度字体的 advance
        auto itGray = grayCpToIdx.find(ucp);
        if (itGray != grayCpToIdx.end())
        {
            auto it = grayShapedMap.find(itGray->second);
            if (it != grayShapedMap.end())
            {
                totalAdvance += it->second.x_advance;
                auto git = grayMap.find(ucp);
                if (git != grayMap.end() && git->second.atlasH > maxHeight)
                    maxHeight = static_cast<float>(git->second.atlasH);
            }
            continue;
        }
        auto itColor = colorCpToIdx.find(ucp);
        if (itColor != colorCpToIdx.end())
        {
            auto it = colorShapedMap.find(itColor->second);
            if (it != colorShapedMap.end())
            {
                totalAdvance += it->second.x_advance;
                auto cit = colorMap.find(itColor->second);
                if (cit != colorMap.end() && cit->second.height > maxHeight)
                    maxHeight = static_cast<float>(cit->second.height);
            }
        }
    }

    const int margin = 20;
    int imgW = static_cast<int>(totalAdvance) + margin * 2;
    int imgH = static_cast<int>(maxHeight) + margin * 2;
    std::vector<uint8_t> image(imgW * imgH * 4, 0); // BGRA 透明背景

    float cursorX = margin;
    float baselineY = margin + maxHeight;

    for (char32_t cp : cps)
    {
        uint32_t ucp = static_cast<uint32_t>(cp);
        bool rendered = false;

        // 尝试灰度字体
        auto itGray = grayCpToIdx.find(ucp);
        if (itGray != grayCpToIdx.end())
        {
            uint32_t gidx = itGray->second;
            auto git = grayMap.find(ucp);
            auto sit = grayShapedMap.find(gidx);
            if (git != grayMap.end() && sit != grayShapedMap.end())
            {
                const GrayGlyph &g = git->second;
                const auto &sg = sit->second;

                float x0 = cursorX + sg.x_offset + g.bitmap_left;
                float y0 = baselineY - sg.y_offset - g.bitmap_top;

                // 从灰度图集拷贝，转为白色文字
                for (int y = 0; y < g.atlasH; ++y)
                {
                    for (int x = 0; x < g.atlasW; ++x)
                    {
                        int srcIdx =
                            (g.atlasY + y) * grayPacker.getWidth() + (g.atlasX + x);
                        uint8_t val = grayPacker.getData()[srcIdx];
                        if (val == 0)
                            continue;
                        int dstX = static_cast<int>(x0 + x);
                        int dstY = static_cast<int>(y0 + y);
                        if (dstX >= 0 && dstX < imgW && dstY >= 0 && dstY < imgH)
                        {
                            int dstIdx = (dstY * imgW + dstX) * 4;
                            image[dstIdx + 0] = 255; // B
                            image[dstIdx + 1] = 255; // G
                            image[dstIdx + 2] = 255; // R
                            image[dstIdx + 3] = val; // A
                        }
                    }
                }
                cursorX += g.advance;
                rendered = true;
            }
        }

        if (!rendered)
        {
            // 尝试彩色字体
            auto itColor = colorCpToIdx.find(ucp);
            if (itColor != colorCpToIdx.end())
            {
                uint32_t gidx = itColor->second;
                auto cit = colorMap.find(gidx);
                auto sit = colorShapedMap.find(gidx);
                if (cit != colorMap.end() && sit != colorShapedMap.end())
                {
                    const ColorGlyph &cg = cit->second;
                    const auto &sg = sit->second;

                    float x0 = cursorX + sg.x_offset + cg.bitmap_left;
                    float y0 = baselineY - sg.y_offset - cg.bitmap_top;

                    // 可选：添加调试输出，查看表情的绘制区域
                    // std::cout << "Rendering emoji U+" << std::hex << ucp << std::dec
                    //           << " at (" << x0 << "," << y0 << ") size " << cg.width <<
                    //           "x" << cg.height << "\n";

                    for (int y = 0; y < cg.height; ++y)
                    {
                        for (int x = 0; x < cg.width; ++x)
                        {
                            int srcIdx = (y * cg.width + x) * 4;
                            uint8_t b = cg.bitmap[srcIdx + 0];
                            uint8_t g = cg.bitmap[srcIdx + 1];
                            uint8_t r = cg.bitmap[srcIdx + 2];
                            uint8_t a = cg.bitmap[srcIdx + 3];
                            if (a == 0)
                                continue;
                            int dstX = static_cast<int>(x0 + x);
                            int dstY = static_cast<int>(y0 + y);
                            if (dstX >= 0 && dstX < imgW && dstY >= 0 && dstY < imgH)
                            {
                                int dstIdx = (dstY * imgW + dstX) * 4;
                                image[dstIdx + 0] = b;
                                image[dstIdx + 1] = g;
                                image[dstIdx + 2] = r;
                                image[dstIdx + 3] = a;
                            }
                        }
                    }
                    cursorX += sg.x_advance;
                    rendered = true;
                }
            }
        }

        if (!rendered)
        {
            // 字符不存在于任何字体，简单推进
            cursorX += 20.0f;
        }
    }

    auto rgb = bgra_to_rgb(image, imgW, imgH);
    saveColorPPM(rgb, imgW, imgH, outFile);
    std::cout << "Saved integrated layout: " << outFile << std::endl;
}

// -------------------- 主函数 --------------------
int main()
{
    try
    {
        std::setlocale(LC_ALL, "en_US.UTF-8");

        // ========== 第一部分：处理中文字体（灰度 + SDF）==========
        std::cout << "=== Part 1: Chinese font (msyh.ttc) ===\n";
        if (!fs::exists(UNICODE_FONT_PATH))
        {
            std::cerr << "Font file not found: " << UNICODE_FONT_PATH << "\n";
            return 1;
        }

        FontLoader font(UNICODE_FONT_PATH, DEFAULT_FONT_SIZE);
        FT_Face face = font.getFace();

        std::vector<char32_t> codepointsVec = utf8_to_codepoints(INPUT_TEXT);
        std::set<uint32_t> uniqueSet;
        for (char32_t cp : codepointsVec)
            uniqueSet.insert(static_cast<uint32_t>(cp));
        std::vector<uint32_t> codepoints(uniqueSet.begin(), uniqueSet.end());

        std::cout << "Input text: " << INPUT_TEXT << "\n";
        std::cout << "Unique characters: " << codepoints.size() << "\n";

        std::map<uint32_t, GrayGlyph> grayMap;
        std::map<uint32_t, SdfGlyph> sdfMap;
        std::vector<msdfgen::Bitmap<float, 1>> sdfBitmaps;
        std::vector<std::vector<uint8_t>> grayBitmaps;
        std::vector<std::pair<int, int>> graySizes;          // width, height
        std::map<uint32_t, std::pair<int, int>> grayLeftTop; // 临时存储 left/top

        // 生成灰度位图和SDF
        for (uint32_t cp : codepoints)
        {
            GlyphMetrics gm = font.getGlyphMetrics(cp);
            if (gm.glyphIndex == 0)
            {
                std::cerr << "Warning: codepoint U+" << std::hex << cp << std::dec
                          << " not found, skipped.\n";
                continue;
            }

            // 灰度位图
            int w, h, left, top;
            auto grayData = font.renderGrayBitmap(gm.glyphIndex, w, h, left, top);
            if (!grayData.empty())
            {
                grayBitmaps.push_back(grayData);
                graySizes.emplace_back(w, h);
                grayLeftTop[cp] = {left, top};
                GrayGlyph gg;
                static_cast<GlyphMetrics &>(gg) = gm;
                gg.atlasW = w;
                gg.atlasH = h;
                gg.bitmap_left = left;
                gg.bitmap_top = top;
                grayMap[cp] = gg;
            }
            else
            {
                grayBitmaps.emplace_back();
                graySizes.emplace_back(0, 0);
            }

            // SDF
            FT_Outline *outline = font.getOutline(gm.glyphIndex);
            auto sdfBitmap = SdfGenerator::generate(outline, GLYPH_SIZE, SDF_RANGE);
            sdfBitmaps.push_back(std::move(sdfBitmap));
            SdfGlyph sg;
            static_cast<GlyphMetrics &>(sg) = gm;
            sdfMap[cp] = sg;
        }

        // 打包灰度图集
        AtlasPacker<uint8_t> grayPacker(ATLAS_WIDTH, ATLAS_HEIGHT);
        size_t idx = 0;
        for (uint32_t cp : codepoints)
        {
            auto it = grayMap.find(cp);
            if (it == grayMap.end())
            {
                ++idx;
                continue;
            }
            if (grayBitmaps[idx].empty() || graySizes[idx].first == 0 ||
                graySizes[idx].second == 0)
            {
                ++idx;
                continue;
            }
            int x, y;
            if (!grayPacker.add(graySizes[idx].first, graySizes[idx].second,
                                grayBitmaps[idx].data(), x, y))
            {
                std::cerr << "Gray atlas full at codepoint " << cp << "\n";
                break;
            }
            GrayGlyph &gg = it->second;
            gg.atlasX = x;
            gg.atlasY = y;
            gg.atlasW = graySizes[idx].first;
            gg.atlasH = graySizes[idx].second;
            gg.uMin = static_cast<float>(x) / ATLAS_WIDTH;
            gg.vMin = static_cast<float>(y) / ATLAS_HEIGHT;
            gg.uMax = static_cast<float>(x + gg.atlasW) / ATLAS_WIDTH;
            gg.vMax = static_cast<float>(y + gg.atlasH) / ATLAS_HEIGHT;
            ++idx;
        }

        // 打包SDF图集
        AtlasPacker<uint8_t> sdfPacker(ATLAS_WIDTH, ATLAS_HEIGHT);
        idx = 0;
        for (uint32_t cp : codepoints)
        {
            auto it = sdfMap.find(cp);
            if (it == sdfMap.end())
            {
                ++idx;
                continue;
            }
            const auto &sdfBmp = sdfBitmaps[idx];
            std::vector<uint8_t> grayData(GLYPH_SIZE * GLYPH_SIZE);
            for (int y = 0; y < GLYPH_SIZE; ++y)
                for (int x = 0; x < GLYPH_SIZE; ++x)
                {
                    float v = *sdfBmp(x, y);
                    uint8_t g = static_cast<uint8_t>((v + 1.0f) * 0.5f * PGM_MAX_GRAY);
                    grayData[y * GLYPH_SIZE + x] = g;
                }
            int x, y;
            if (!sdfPacker.add(GLYPH_SIZE, GLYPH_SIZE, grayData.data(), x, y))
            {
                std::cerr << "SDF atlas full at codepoint " << cp << "\n";
                break;
            }
            SdfGlyph &sg = it->second;
            sg.atlasX = x;
            sg.atlasY = y;
            sg.atlasW = GLYPH_SIZE;
            sg.atlasH = GLYPH_SIZE;
            sg.uMin = static_cast<float>(x) / ATLAS_WIDTH;
            sg.vMin = static_cast<float>(y) / ATLAS_HEIGHT;
            sg.uMax = static_cast<float>(x + GLYPH_SIZE) / ATLAS_WIDTH;
            sg.vMax = static_cast<float>(y + GLYPH_SIZE) / ATLAS_HEIGHT;
            ++idx;
        }

        // 保存图集
        saveGrayPGM(grayPacker.getData(), ATLAS_WIDTH, ATLAS_HEIGHT, "gray_atlas.pgm");
        saveGrayPGM(sdfPacker.getData(), ATLAS_WIDTH, ATLAS_HEIGHT, "sdf_atlas.pgm");

        // 保存灰度切片
        for (const auto &p : grayMap)
        {
            const GrayGlyph &gg = p.second;
            std::vector<uint8_t> slice(gg.atlasW * gg.atlasH);
            for (int y = 0; y < gg.atlasH; ++y)
                memcpy(&slice[y * gg.atlasW],
                       &grayPacker.getData()[(gg.atlasY + y) * ATLAS_WIDTH + gg.atlasX],
                       gg.atlasW);
            saveGrayPGM(slice, gg.atlasW, gg.atlasH,
                        safe_filename(gg.codepoint) + ".pgm");
        }

        // 保存SDF切片
        for (const auto &p : sdfMap)
        {
            const SdfGlyph &sg = p.second;
            std::vector<uint8_t> slice(sg.atlasW * sg.atlasH);
            for (int y = 0; y < sg.atlasH; ++y)
                memcpy(&slice[y * sg.atlasW],
                       &sdfPacker.getData()[(sg.atlasY + y) * ATLAS_WIDTH + sg.atlasX],
                       sg.atlasW);
            saveGrayPGM(slice, sg.atlasW, sg.atlasH,
                        safe_filename(sg.codepoint) + "_sdf.pgm");
        }

        // 灰度布局验证
        renderGrayLayout(INPUT_TEXT, grayMap, grayPacker, face, "layout_gray.pgm");

        // ========== 第二部分：处理表情字体（彩色）==========
        std::cout << "\n=== Part 2: Emoji font (seguiemj.ttf) ===\n";
        if (!fs::exists(EMOJI_FONT_PATH))
        {
            std::cerr << "Emoji font not found, skipping.\n";
        }
        else
        {
            FontLoader emojiFont(EMOJI_FONT_PATH, DEFAULT_FONT_SIZE);
            FT_Face emojiFace = emojiFont.getFace();

            // 以 glyphIndex 为键的彩色字形映射
            std::map<uint32_t, ColorGlyph> colorMap;

            std::cout << "Checking emoji support for " << codepoints.size()
                      << " codepoints:\n";
            for (uint32_t cp : codepoints)
            {
                FT_UInt idx = FT_Get_Char_Index(emojiFace, cp);
                std::cout << "  U+" << std::hex << cp << std::dec << " -> glyph index "
                          << idx << "\n";
                if (idx == 0)
                    continue;

                int w, h, left, top, advance_26_6;
                auto bitmap =
                    emojiFont.renderColorBitmap(idx, w, h, left, top, advance_26_6);
                if (bitmap.empty() || w == 0 || h == 0)
                {
                    std::cout << "      renderColorBitmap failed (empty result)\n";
                    continue;
                }

                // 验证返回的 bitmap 大小是否与 w,h 一致
                if (bitmap.size() != static_cast<size_t>(w * h * 4))
                {
                    std::cout << "      error: bitmap size mismatch (expected "
                              << (w * h * 4) << ", got " << bitmap.size() << ")\n";
                    continue;
                }

                ColorGlyph cg;
                cg.codepoint = cp;
                cg.glyphIndex = idx;
                cg.width = w;
                cg.height = h;
                cg.bitmap_left = left;
                cg.bitmap_top = top;
                cg.advance_26_6 = advance_26_6;
                cg.bitmap = std::move(bitmap);
                colorMap[idx] = std::move(cg);

                // 保存单个彩色切片
                try
                {
                    auto rgb = bgra_to_rgb(cg.bitmap, w, h);
                    saveColorPPM(rgb, w, h, safe_filename(cp) + ".ppm");
                    std::cout << "      saved " << safe_filename(cp) << ".ppm\n";
                }
                catch (const std::exception &e)
                {
                    std::cout << "      error saving slice: " << e.what() << "\n";
                }
            }

            if (!colorMap.empty())
            {
                // 打包彩色图集
                AtlasPacker<uint8_t[4]> colorPacker(ATLAS_WIDTH, ATLAS_HEIGHT);
                for (auto &p : colorMap)
                {
                    ColorGlyph &cg = p.second;
                    int x, y;
                    if (!colorPacker.add(cg.width, cg.height, cg.bitmap.data(), x, y))
                    {
                        std::cerr << "Color atlas full, skipping glyphIndex "
                                  << cg.glyphIndex << "\n";
                        continue;
                    }
                    cg.atlasX = x;
                    cg.atlasY = y;
                    cg.uMin = static_cast<float>(x) / ATLAS_WIDTH;
                    cg.vMin = static_cast<float>(y) / ATLAS_HEIGHT;
                    cg.uMax = static_cast<float>(x + cg.width) / ATLAS_WIDTH;
                    cg.vMax = static_cast<float>(y + cg.height) / ATLAS_HEIGHT;
                }

                // 保存彩色图集
                const auto &atlasData = colorPacker.getData();
                if (atlasData.size() ==
                    static_cast<size_t>(ATLAS_WIDTH * ATLAS_HEIGHT * 4))
                {
                    std::vector<uint8_t> atlasRgb(ATLAS_WIDTH * ATLAS_HEIGHT * 3);
                    for (int i = 0; i < ATLAS_WIDTH * ATLAS_HEIGHT; ++i)
                    {
                        atlasRgb[i * 3 + 0] = atlasData[i * 4 + 2];
                        atlasRgb[i * 3 + 1] = atlasData[i * 4 + 1];
                        atlasRgb[i * 3 + 2] = atlasData[i * 4 + 0];
                    }
                    saveColorPPM(atlasRgb, ATLAS_WIDTH, ATLAS_HEIGHT, "emoji_atlas.ppm");
                }
                else
                {
                    std::cerr << "Color atlas data size mismatch, skipping save.\n";
                }

                // 彩色布局验证
                try
                {
                    renderColorLayout(INPUT_TEXT, colorMap, emojiFace,
                                      "layout_color.ppm");
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error in color layout: " << e.what() << "\n";
                }

                // ========== 第三部分：集成布局 ==========
                std::cout << "\n=== Part 3: Integrated layout ===\n";
                try
                {
                    renderIntegratedLayout(INPUT_TEXT, grayMap, grayPacker, colorMap,
                                           face, emojiFace, "layout_integrated.ppm");
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error in integrated layout: " << e.what() << "\n";
                }
            }
            else
            {
                std::cout << "No valid emoji glyphs found in font.\n";
            }
        }

        std::cout << "\nAll files generated. Check:\n"
                  << " - gray_atlas.pgm, sdf_atlas.pgm, emoji_atlas.ppm\n"
                  << " - individual *.pgm, *_sdf.pgm, *.ppm\n"
                  << " - layout_gray.pgm, layout_color.ppm, layout_integrated.ppm\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}