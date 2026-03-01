#include <cuchar>
#include <ft2build.h>
#include FT_FREETYPE_H
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

// -------------------- 常量定义 --------------------
constexpr float FREETYPE_FIXED_SCALE = 64.0f; // FreeType 26.6 定点数转换为像素的除数
constexpr int PGM_MAX_GRAY = 255;             // PGM 格式最大灰度值
constexpr int DEFAULT_FONT_SIZE = 64;         // 字体大小（像素）
constexpr int ATLAS_WIDTH = 512;              // 图集宽度
constexpr int ATLAS_HEIGHT = 512;             // 图集高度
constexpr int GLYPH_SIZE = 32;                // SDF 小图固定尺寸
constexpr double SDF_RANGE = 4.0;             // SDF 距离场范围（像素）
const char *DEFAULT_FONT_PATH = "C:/Windows/Fonts/msyh.ttc"; // 使用支持中文的字体

// 硬编码输入文本（可修改）
const std::string INPUT_TEXT = "Hello, 世界! こんにちは 123";

// -------------------- UTF-8
// 解码器（将UTF-8字符串转换为UTF-32码点向量）--------------------
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
            break; // null character
        if (rc == (size_t)-1)
            break; // invalid sequence
        if (rc == (size_t)-2)
            break; // incomplete sequence
        if (rc == (size_t)-3)
        {
            // previous character repeated, ignore
            ptr += 1;
            continue;
        }
        result.push_back(cp);
        ptr += rc;
    }
    return result;
}

// -------------------- 辅助函数：保存单通道浮点位图为PGM --------------------
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
            float normalized = (v + 1.0f) / 2.0f; // 将 [-1,1] 映射到 [0,1]
            uint8_t gray = static_cast<uint8_t>(normalized * PGM_MAX_GRAY + 0.5f);
            file.write(reinterpret_cast<const char *>(&gray), 1);
        }
    }
}

// 保存灰度缓冲区为PGM
void saveGrayPGM(const std::vector<uint8_t> &data, int width, int height,
                 const std::string &filename)
{
    std::ofstream file(filename, std::ios::binary);
    if (!file)
        return;
    file << "P5\n" << width << " " << height << "\n" << PGM_MAX_GRAY << "\n";
    file.write(reinterpret_cast<const char *>(data.data()), data.size());
}

// 将字符转换为安全的文件名（非ASCII用U+xxxx）
std::string safe_filename(char32_t codepoint)
{
    if (codepoint <= 0x7F)
    {
        char ch = static_cast<char>(codepoint);
        if (isalnum(ch))
            return std::string(1, ch);
        // 特殊字符映射（仅ASCII）
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
    // 非ASCII或未映射的特殊字符使用U+xxxx形式
    char buf[16];
    snprintf(buf, sizeof(buf), "U+%04X", static_cast<unsigned>(codepoint));
    return std::string(buf);
}

// -------------------- 字符元数据 --------------------
struct Glyph
{
    uint32_t codepoint = 0;
    uint32_t glyphIndex = 0;
    float width = 0, height = 0;                  // 字形尺寸（像素）
    float bearingX = 0, bearingY = 0;             // 偏移
    float advance = 0;                            // 前进距离
    int atlasX = 0, atlasY = 0;                   // 在图集中的左上角像素坐标
    int atlasW = 0, atlasH = 0;                   // 在图集中的尺寸（SDF固定）
    float uMin = 0, vMin = 0, uMax = 0, vMax = 0; // 归一化UV
};

// -------------------- 字体加载器（封装FreeType） --------------------
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
            throw std::runtime_error("FT_New_Face failed");
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

    Glyph getGlyphMetrics(uint32_t codepoint) const
    {
        Glyph g;
        g.codepoint = codepoint;
        g.glyphIndex = FT_Get_Char_Index(m_face, codepoint);
        if (g.glyphIndex == 0)
            return g;

        FT_Load_Glyph(m_face, g.glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
        g.width = static_cast<float>(m_face->glyph->metrics.width) / FREETYPE_FIXED_SCALE;
        g.height =
            static_cast<float>(m_face->glyph->metrics.height) / FREETYPE_FIXED_SCALE;
        g.bearingX = static_cast<float>(m_face->glyph->metrics.horiBearingX) /
                     FREETYPE_FIXED_SCALE;
        g.bearingY = static_cast<float>(m_face->glyph->metrics.horiBearingY) /
                     FREETYPE_FIXED_SCALE;
        g.advance =
            static_cast<float>(m_face->glyph->metrics.horiAdvance) / FREETYPE_FIXED_SCALE;
        return g;
    }

    FT_Outline *getOutline(uint32_t glyphIndex)
    {
        FT_Load_Glyph(m_face, glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
        return &m_face->glyph->outline;
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

// -------------------- SDF生成器（封装msdfgen） --------------------
class SdfGenerator
{
  public:
    static msdfgen::Bitmap<float, 1> generate(const FT_Outline *outline, int size,
                                              double range)
    {
        msdfgen::Shape shape;
        if (msdfgen::readFreetypeOutline(shape, const_cast<FT_Outline *>(outline)) != 0)
        {
            throw std::runtime_error("readFreetypeOutline failed");
        }
        shape.normalize();

        auto bounds = shape.getBounds();
        double l = bounds.l, r = bounds.r, b = bounds.b, t = bounds.t;
        double w = r - l, h = t - b;

        double scale = (size - 2 * range) / std::max(w, h);
        double tx = (size / 2.0) - (l + w / 2) * scale;
        double ty = (size / 2.0) + (b + h / 2) * scale; // Y轴向下为正

        msdfgen::Bitmap<float, 1> bitmap(size, size);
        msdfgen::generateSDF(bitmap, shape, range, scale, tx, ty);
        return bitmap;
    }
};

// -------------------- SDF图集打包器（固定尺寸） --------------------
class SdfAtlasPacker
{
  public:
    SdfAtlasPacker(int width, int height, int glyphSize)
        : m_width(width), m_height(height), m_glyphSize(glyphSize)
    {
        m_atlasData.resize(width * height, 0);
        m_nextX = 0;
        m_nextY = 0;
    }

    bool addGlyph(Glyph &glyph, const msdfgen::Bitmap<float, 1> &bitmap)
    {
        if (m_nextY + m_glyphSize > m_height)
            return false;

        int x = m_nextX, y = m_nextY;
        for (int gy = 0; gy < m_glyphSize; ++gy)
        {
            for (int gx = 0; gx < m_glyphSize; ++gx)
            {
                float v = *bitmap(gx, gy);
                uint8_t gray = static_cast<uint8_t>((v + 1.0f) * 0.5f * PGM_MAX_GRAY);
                m_atlasData[(y + gy) * m_width + (x + gx)] = gray;
            }
        }

        glyph.atlasX = x;
        glyph.atlasY = y;
        glyph.atlasW = m_glyphSize;
        glyph.atlasH = m_glyphSize;
        glyph.uMin = static_cast<float>(x) / m_width;
        glyph.vMin = static_cast<float>(y) / m_height;
        glyph.uMax = static_cast<float>(x + m_glyphSize) / m_width;
        glyph.vMax = static_cast<float>(y + m_glyphSize) / m_height;

        m_nextX += m_glyphSize;
        if (m_nextX + m_glyphSize > m_width)
        {
            m_nextX = 0;
            m_nextY += m_glyphSize;
        }
        return true;
    }

    const std::vector<uint8_t> &getAtlasData() const
    {
        return m_atlasData;
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
    int m_glyphSize;
    std::vector<uint8_t> m_atlasData;
    int m_nextX, m_nextY;
};

// -------------------- 普通位图打包器（可变尺寸） --------------------
class RawAtlasPacker
{
  public:
    RawAtlasPacker(int width, int height) : m_width(width), m_height(height)
    {
        m_atlasData.resize(width * height, 0);
        m_curX = 0;
        m_curY = 0;
        m_rowHeight = 0;
    }

    bool addGlyph(int imgWidth, int imgHeight, const uint8_t *data, int &outX, int &outY)
    {
        if (imgWidth > m_width || imgHeight > m_height)
            return false;

        if (m_curX + imgWidth > m_width)
        {
            m_curX = 0;
            m_curY += m_rowHeight;
            m_rowHeight = 0;
        }
        if (m_curY + imgHeight > m_height)
            return false;

        for (int y = 0; y < imgHeight; ++y)
        {
            int dstIdx = (m_curY + y) * m_width + m_curX;
            memcpy(&m_atlasData[dstIdx], data + y * imgWidth, imgWidth);
        }

        outX = m_curX;
        outY = m_curY;

        m_curX += imgWidth;
        if (imgHeight > m_rowHeight)
            m_rowHeight = imgHeight;
        return true;
    }

    const std::vector<uint8_t> &getAtlasData() const
    {
        return m_atlasData;
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
    std::vector<uint8_t> m_atlasData;
    int m_curX, m_curY;
    int m_rowHeight;
};

// -------------------- 文本整形器（封装HarfBuzz，用于演示） --------------------
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

// -------------------- 主函数 --------------------
int main()
{
    try
    {
        // 配置参数
        const char *fontPath = DEFAULT_FONT_PATH;
        const int fontSize = DEFAULT_FONT_SIZE;
        const int atlasWidth = ATLAS_WIDTH;
        const int atlasHeight = ATLAS_HEIGHT;
        const int glyphSize = GLYPH_SIZE;
        const double sdfRange = SDF_RANGE;

        if (!std::filesystem::exists(fontPath))
        {
            std::cerr << "Font file not found: " << fontPath << "\n";
            return 1;
        }

        // 将输入文本转换为UTF-32码点
        std::vector<char32_t> codepointsVec = utf8_to_codepoints(INPUT_TEXT);
        // 提取唯一字符
        std::set<uint32_t> uniqueSet;
        for (char32_t cp : codepointsVec)
        {
            uniqueSet.insert(static_cast<uint32_t>(cp));
        }
        std::vector<uint32_t> codepoints(uniqueSet.begin(), uniqueSet.end());

        std::cout << "Input text: " << INPUT_TEXT << "\n";
        std::cout << "Unique characters: " << codepoints.size() << "\n";

        // 1. 初始化字体加载器
        FontLoader font(fontPath, fontSize);

        // 存储每个字符的元数据
        std::map<uint32_t, Glyph> glyphMap;
        std::vector<msdfgen::Bitmap<float, 1>> sdfBitmaps;
        std::vector<std::vector<uint8_t>> rawBitmaps;
        std::vector<std::pair<int, int>> rawSizes;

        // 2. 遍历字符集，生成SDF和普通位图
        for (uint32_t cp : codepoints)
        {
            Glyph g = font.getGlyphMetrics(cp);
            if (g.glyphIndex == 0)
            {
                std::cerr << "Warning: codepoint U+" << std::hex << cp << std::dec
                          << " not found in font, skipped.\n";
                continue;
            }

            // ---- 生成SDF ----
            FT_Outline *outline = font.getOutline(g.glyphIndex);
            auto sdfBitmap = SdfGenerator::generate(outline, glyphSize, sdfRange);
            sdfBitmaps.push_back(std::move(sdfBitmap));

            // ---- 渲染普通灰度位图 ----
            FT_Load_Glyph(font.getFace(), g.glyphIndex, FT_LOAD_RENDER);
            FT_Bitmap *ftBitmap = &font.getFace()->glyph->bitmap;
            if (ftBitmap->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                std::vector<uint8_t> rawData(ftBitmap->width * ftBitmap->rows);
                for (unsigned int y = 0; y < ftBitmap->rows; ++y)
                {
                    memcpy(&rawData[y * ftBitmap->width],
                           ftBitmap->buffer + y * ftBitmap->pitch, ftBitmap->width);
                }
                rawBitmaps.push_back(std::move(rawData));
                rawSizes.emplace_back(ftBitmap->width, ftBitmap->rows);
            }
            else
            {
                // 非灰度位图（如空位图）
                rawBitmaps.emplace_back();
                rawSizes.emplace_back(0, 0);
            }

            glyphMap[cp] = g;
        }

        if (glyphMap.empty())
        {
            std::cerr << "No valid glyphs found.\n";
            return 1;
        }

        // 3. 打包SDF图集
        SdfAtlasPacker sdfPacker(atlasWidth, atlasHeight, glyphSize);
        size_t idx = 0;
        for (uint32_t cp : codepoints)
        {
            auto it = glyphMap.find(cp);
            if (it == glyphMap.end())
                continue;
            Glyph &g = it->second;
            if (!sdfPacker.addGlyph(g, sdfBitmaps[idx++]))
            {
                std::cerr << "SDF atlas full, stopping at codepoint " << cp << "\n";
                break;
            }
        }

        // 4. 打包普通灰度图集
        RawAtlasPacker rawPacker(atlasWidth, atlasHeight);
        std::map<uint32_t, std::pair<int, int>> rawAtlasPos;
        idx = 0;
        for (uint32_t cp : codepoints)
        {
            auto it = glyphMap.find(cp);
            if (it == glyphMap.end())
                continue;
            if (rawBitmaps[idx].empty() || rawSizes[idx].first == 0 ||
                rawSizes[idx].second == 0)
            {
                ++idx;
                continue;
            }
            int x, y;
            if (!rawPacker.addGlyph(rawSizes[idx].first, rawSizes[idx].second,
                                    rawBitmaps[idx].data(), x, y))
            {
                std::cerr << "Raw atlas full, stopping at codepoint " << cp << "\n";
                break;
            }
            rawAtlasPos[cp] = {x, y};
            ++idx;
        }

        // 5. 保存图集大图
        saveGrayPGM(sdfPacker.getAtlasData(), sdfPacker.getWidth(), sdfPacker.getHeight(),
                    "sdf_atlas.pgm");
        saveGrayPGM(rawPacker.getAtlasData(), rawPacker.getWidth(), rawPacker.getHeight(),
                    "raw_atlas.pgm");

        // 6. 保存每个字符的SDF小图（从SDF图集中切割）
        for (const auto &pair : glyphMap)
        {
            const Glyph &g = pair.second;
            std::vector<uint8_t> glyphImg(g.atlasW * g.atlasH);
            for (int y = 0; y < g.atlasH; ++y)
            {
                for (int x = 0; x < g.atlasW; ++x)
                {
                    int srcIdx = (g.atlasY + y) * sdfPacker.getWidth() + (g.atlasX + x);
                    glyphImg[y * g.atlasW + x] = sdfPacker.getAtlasData()[srcIdx];
                }
            }
            std::string fname = safe_filename(g.codepoint) + "_sdf.pgm";
            saveGrayPGM(glyphImg, g.atlasW, g.atlasH, fname);
        }

        // 7. 保存每个字符的普通灰度小图（从Raw图集中切割）
        for (const auto &pair : rawAtlasPos)
        {
            uint32_t cp = pair.first;
            int x = pair.second.first;
            int y = pair.second.second;
            auto it = glyphMap.find(cp);
            if (it == glyphMap.end())
                continue;
            // 需要知道原始位图尺寸，这里通过存储的rawSizes获取（注意索引对应）
            // 为了简单，我们在循环中重新计算：由于rawBitmaps顺序与codepoints一致，但rawAtlasPos只包含成功添加的，需要找到对应索引
            // 重新遍历一次找到索引
            int w = 0, h = 0;
            idx = 0;
            for (uint32_t code : codepoints)
            {
                if (code == cp)
                {
                    w = rawSizes[idx].first;
                    h = rawSizes[idx].second;
                    break;
                }
                ++idx;
            }
            if (w == 0 || h == 0)
                continue;

            std::vector<uint8_t> glyphImg(w * h);
            for (int gy = 0; gy < h; ++gy)
            {
                int srcIdx = (y + gy) * rawPacker.getWidth() + x;
                memcpy(&glyphImg[gy * w], &rawPacker.getAtlasData()[srcIdx], w);
            }
            std::string fname = safe_filename(cp) + ".pgm";
            saveGrayPGM(glyphImg, w, h, fname);
        }

        // 8. 输出元数据（控制台）
        std::cout << "SDF Atlas size: " << sdfPacker.getWidth() << "x"
                  << sdfPacker.getHeight() << "\n";
        std::cout << "Raw Atlas size: " << rawPacker.getWidth() << "x"
                  << rawPacker.getHeight() << "\n";
        std::cout << "\nGlyph metadata:\n";
        for (const auto &pair : glyphMap)
        {
            const Glyph &g = pair.second;
            std::string name = safe_filename(g.codepoint);
            std::cout << name << " (U+" << std::hex << g.codepoint << std::dec << ")"
                      << " sdf atlas pos: (" << g.atlasX << "," << g.atlasY << ")"
                      << " uv: (" << g.uMin << "," << g.vMin << ")-(" << g.uMax << ","
                      << g.vMax << ")"
                      << " advance: " << g.advance << "\n";
        }

        // 9. 演示整形（可选）
        TextShaper shaper(font.getFace());
        auto shaped = shaper.shape(INPUT_TEXT);
        std::cout << "\nShaped text: " << INPUT_TEXT << "\n";
        for (const auto &sg : shaped)
        {
            std::cout << "Glyph index: " << sg.glyphIndex << " offset: (" << sg.x_offset
                      << "," << sg.y_offset << ")"
                      << " advance: " << sg.x_advance << "\n";
        }

        std::cout << "\nAll files generated. Please check:\n"
                  << " - sdf_atlas.pgm (SDF atlas)\n"
                  << " - raw_atlas.pgm (normal grayscale atlas)\n"
                  << " - *_sdf.pgm (individual SDF glyphs)\n"
                  << " - *.pgm (individual normal glyphs)\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}