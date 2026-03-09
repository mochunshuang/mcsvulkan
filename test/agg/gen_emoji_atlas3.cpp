#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <optional>
#include <cctype>

#include <algorithm> // std::sort
#include <utility>   // std::swap

// FreeType
#include <ft2build.h>
#include FT_FREETYPE_H

// stb_rect_pack
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

// stb_image_write
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// nlohmann/json
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// 默认参数
static const int DEFAULT_FONT_SIZE = 64;
static const int DEFAULT_PADDING = 1;
static const int INITIAL_SIZE = 128;
static const int MAX_SIZE = 4096;

// 尺寸约束
enum class DimensionsConstraint
{
    POWER_OF_TWO_SQUARE,
    SQUARE,
    NONE
};

struct GlyphInfo
{
    uint32_t unicode;
    int width, height;
    int bearingX, bearingY;
    int advance;                 // 像素
    std::vector<uint8_t> bitmap; // RGBA
};

// 从文件读取字符集
std::vector<uint32_t> readCharset(const std::string &filename)
{
    std::vector<uint32_t> result;
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Cannot open charset file: " + filename);
    std::string line;
    while (std::getline(file, line))
    {
        // 去除首尾空白
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        if (line.empty() || line[0] == '#')
            continue;

        // 处理带单引号的字符：如 'A' 或 '😀'
        if (line.size() >= 2 && line[0] == '\'' && line.back() == '\'')
        {
            std::string inside = line.substr(1, line.size() - 2);
            const char *str = inside.c_str();
            unsigned int cp = 0;
            if ((str[0] & 0x80) == 0)
            {
                cp = str[0];
            }
            else if ((str[0] & 0xE0) == 0xC0 && (str[1] & 0xC0) == 0x80)
            {
                cp = ((str[0] & 0x1F) << 6) | (str[1] & 0x3F);
            }
            else if ((str[0] & 0xF0) == 0xE0 && (str[1] & 0xC0) == 0x80 &&
                     (str[2] & 0xC0) == 0x80)
            {
                cp = ((str[0] & 0x0F) << 12) | ((str[1] & 0x3F) << 6) | (str[2] & 0x3F);
            }
            else if ((str[0] & 0xF8) == 0xF0 && (str[1] & 0xC0) == 0x80 &&
                     (str[2] & 0xC0) == 0x80 && (str[3] & 0xC0) == 0x80)
            {
                cp = ((str[0] & 0x07) << 18) | ((str[1] & 0x3F) << 12) |
                     ((str[2] & 0x3F) << 6) | (str[3] & 0x3F);
            }
            else
            {
                std::cerr << "Warning: invalid UTF-8 inside quotes: " << line
                          << std::endl;
                continue;
            }
            result.push_back(cp);
            continue;
        }

        // 尝试解析为整数（十进制/十六进制）
        char *endptr;
        long val = strtol(line.c_str(), &endptr, 0);
        if (endptr != line.c_str() && *endptr == '\0')
        {
            result.push_back(static_cast<uint32_t>(val));
        }
        else
        {
            // 否则当作裸 UTF-8 字符（假设一行一个字符）
            const char *str = line.c_str();
            unsigned int cp = 0;
            if ((str[0] & 0x80) == 0)
            {
                cp = str[0];
            }
            else if ((str[0] & 0xE0) == 0xC0 && (str[1] & 0xC0) == 0x80)
            {
                cp = ((str[0] & 0x1F) << 6) | (str[1] & 0x3F);
            }
            else if ((str[0] & 0xF0) == 0xE0 && (str[1] & 0xC0) == 0x80 &&
                     (str[2] & 0xC0) == 0x80)
            {
                cp = ((str[0] & 0x0F) << 12) | ((str[1] & 0x3F) << 6) | (str[2] & 0x3F);
            }
            else if ((str[0] & 0xF8) == 0xF0 && (str[1] & 0xC0) == 0x80 &&
                     (str[2] & 0xC0) == 0x80 && (str[3] & 0xC0) == 0x80)
            {
                cp = ((str[0] & 0x07) << 18) | ((str[1] & 0x3F) << 12) |
                     ((str[2] & 0x3F) << 6) | (str[3] & 0x3F);
            }
            else
            {
                std::cerr << "Warning: invalid UTF-8: " << line << std::endl;
                continue;
            }
            result.push_back(cp);
        }
    }
    return result;
}

// 放在 stb_rect_pack.h 包含之后
namespace std
{
    template <>
    void swap(stbrp_rect &a, stbrp_rect &b) noexcept
    {
        stbrp_rect tmp = a;
        a = b;
        b = tmp;
    }
} // namespace std
// 尝试打包
bool tryPack(const std::vector<stbrp_rect> &rects, int width, int height,
             std::vector<stbrp_rect> &packedRects)
{
    // 先复制一份用于排序
    std::vector<stbrp_rect> sortedRects = rects;
    // 按面积降序排序（大矩形优先）
    std::sort(sortedRects.begin(), sortedRects.end(),
              [](const stbrp_rect &a, const stbrp_rect &b) {
                  return (a.w * a.h) > (b.w * b.h);
              });

    packedRects = sortedRects; // 仍保留排序后顺序（但 id 不变）
    stbrp_context ctx;
    std::vector<stbrp_node> nodes(width);
    stbrp_init_target(&ctx, width, height, nodes.data(), nodes.size());
    stbrp_pack_rects(&ctx, packedRects.data(), (int)packedRects.size());
    for (auto &r : packedRects)
        if (!r.was_packed)
            return false;
    return true;
}

// 自适应寻找最小图集尺寸
std::optional<std::pair<int, int>> findMinAtlasSize(const std::vector<GlyphInfo> &glyphs,
                                                    int padding,
                                                    DimensionsConstraint constraint,
                                                    int initialSize, int maxSize,
                                                    int fixedWidth, int fixedHeight)
{
    if (fixedWidth > 0 && fixedHeight > 0)
    {
        std::vector<stbrp_rect> rects;
        for (size_t i = 0; i < glyphs.size(); ++i)
        {
            stbrp_rect r;
            r.id = (int)i;
            r.w = glyphs[i].width + 2 * padding;
            r.h = glyphs[i].height + 2 * padding;
            rects.push_back(r);
        }
        std::vector<stbrp_rect> packedRects;
        if (tryPack(rects, fixedWidth, fixedHeight, packedRects))
            return std::make_pair(fixedWidth, fixedHeight);
        return std::nullopt;
    }

    int width = initialSize, height = initialSize;
    std::vector<stbrp_rect> rects;
    for (size_t i = 0; i < glyphs.size(); ++i)
    {
        stbrp_rect r;
        r.id = (int)i;
        r.w = glyphs[i].width + 2 * padding;
        r.h = glyphs[i].height + 2 * padding;
        rects.push_back(r);
    }
    std::vector<stbrp_rect> packedRects;
    while (width <= maxSize && height <= maxSize)
    {
        if (tryPack(rects, width, height, packedRects))
            return std::make_pair(width, height);
        if (constraint == DimensionsConstraint::POWER_OF_TWO_SQUARE)
        {
            int next = std::max(width, height) * 2;
            width = next, height = next;
        }
        else if (constraint == DimensionsConstraint::SQUARE)
        {
            width *= 2, height *= 2;
        }
        else
        {
            width += 64, height += 64;
        }
    }
    return std::nullopt;
}

int main(int argc, char *argv[])
{
    // 默认参数
    std::string fontPath = "C:/Windows/Fonts/seguiemj.ttf";
    // CHASET_INPUT_DIR FONT_INPUT_DIR
    auto pre = std::string{CHASET_INPUT_DIR};
    std::string charsetFile = pre + "/small_emoji.txt";
    std::string outputPrefix = "emoji";
    int fontSize = DEFAULT_FONT_SIZE;
    int padding = DEFAULT_PADDING;
    int fixedWidth = -1, fixedHeight = -1;
    DimensionsConstraint constraint = DimensionsConstraint::POWER_OF_TWO_SQUARE;

    // 解析命令行
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-font" && i + 1 < argc)
        {
            fontPath = argv[++i];
        }
        else if (arg == "-charset" && i + 1 < argc)
        {
            charsetFile = argv[++i];
        }
        else if (arg == "-size" && i + 1 < argc)
        {
            fontSize = std::stoi(argv[++i]);
        }
        else if (arg == "-padding" && i + 1 < argc)
        {
            padding = std::stoi(argv[++i]);
        }
        else if (arg == "-output" && i + 1 < argc)
        {
            outputPrefix = argv[++i];
        }
        else if (arg == "-dimensions" && i + 2 < argc)
        {
            fixedWidth = std::stoi(argv[++i]);
            fixedHeight = std::stoi(argv[++i]);
        }
        else if (arg == "-constraint" && i + 1 < argc)
        {
            std::string c = argv[++i];
            if (c == "pow2")
                constraint = DimensionsConstraint::POWER_OF_TWO_SQUARE;
            else if (c == "square")
                constraint = DimensionsConstraint::SQUARE;
            else if (c == "none")
                constraint = DimensionsConstraint::NONE;
            else
            {
                std::cerr << "Unknown constraint: " << c << "\n";
                return 1;
            }
        }
        else if (arg == "-help" || arg == "--help")
        {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  -font <path>          Font file\n"
                      << "  -charset <file>       Character set file\n"
                      << "  -size <pixels>        Render size (default: 64)\n"
                      << "  -padding <pixels>     Padding (default: 1)\n"
                      << "  -output <prefix>      Output prefix (default: emoji)\n"
                      << "  -dimensions <w> <h>   Fixed atlas size (default: auto)\n"
                      << "  -constraint <type>    Size constraint: pow2 (default), "
                         "square, none\n"
                      << "  -help                 This help\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 1;
        }
    }

    if (fontPath.empty() || charsetFile.empty())
    {
        std::cerr << "Both -font and -charset are required.\n";
        return 1;
    }

    // 读取字符集
    std::vector<uint32_t> charset;
    try
    {
        charset = readCharset(charsetFile);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error reading charset: " << e.what() << "\n";
        return 1;
    }
    if (charset.empty())
    {
        std::cerr << "Charset is empty.\n";
        return 1;
    }

    // 初始化 FreeType
    FT_Library ft;
    if (FT_Init_FreeType(&ft))
    {
        std::cerr << "FT_Init_FreeType failed\n";
        return 1;
    }
    FT_Face face;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face))
    {
        std::cerr << "Failed to load font: " << fontPath << "\n";
        FT_Done_FreeType(ft);
        return 1;
    }
    FT_Set_Pixel_Sizes(face, 0, fontSize);

    // 加载每个字形
    std::vector<GlyphInfo> glyphs;
    for (uint32_t ch : charset)
    {
        FT_UInt idx = FT_Get_Char_Index(face, ch);
        if (idx == 0)
        {
            std::cerr << "Glyph not found: U+" << std::hex << ch << std::dec << "\n";
            continue;
        }
        if (FT_Load_Glyph(face, idx, FT_LOAD_COLOR | FT_LOAD_RENDER))
        {
            std::cerr << "Failed to load glyph: U+" << std::hex << ch << std::dec << "\n";
            continue;
        }
        FT_Bitmap &bmp = face->glyph->bitmap;
        if (bmp.pixel_mode != FT_PIXEL_MODE_BGRA)
        {
            std::cerr << "Skipping non-color glyph: U+" << std::hex << ch << std::dec
                      << "\n";
            continue;
        }

        GlyphInfo info;
        info.unicode = ch;
        info.width = bmp.width;
        info.height = bmp.rows;
        info.bearingX = face->glyph->bitmap_left;
        info.bearingY = face->glyph->bitmap_top;
        info.advance = static_cast<int>(face->glyph->metrics.horiAdvance / 64);

        info.bitmap.resize(info.width * info.height * 4);
        uint8_t *dst = info.bitmap.data();
        uint8_t *src = bmp.buffer;
        for (int i = 0; i < info.width * info.height; ++i)
        {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
            dst += 4;
            src += 4;
        }
        glyphs.push_back(std::move(info));
    }

    if (glyphs.empty())
    {
        std::cerr << "No glyphs loaded.\n";
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return 1;
    }

    // 确定图集尺寸
    auto atlasSize = findMinAtlasSize(glyphs, padding, constraint, INITIAL_SIZE, MAX_SIZE,
                                      fixedWidth, fixedHeight);
    if (!atlasSize)
    {
        std::cerr << "Cannot fit glyphs within " << MAX_SIZE << "x" << MAX_SIZE << "\n";
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return 1;
    }
    int w = atlasSize->first, h = atlasSize->second;
    std::cout << "Atlas size: " << w << "x" << h << "\n";

    // 最终打包
    std::vector<stbrp_rect> rects;
    for (size_t i = 0; i < glyphs.size(); ++i)
    {
        stbrp_rect r;
        r.id = (int)i;
        r.w = glyphs[i].width + 2 * padding;
        r.h = glyphs[i].height + 2 * padding;
        rects.push_back(r);
    }
    std::vector<stbrp_rect> packed;
    if (!tryPack(rects, w, h, packed))
    {
        std::cerr << "Packing failed.\n";
        return 1;
    }

    // 创建图集缓冲区
    std::vector<uint8_t> atlas(w * h * 4, 0);
    for (size_t i = 0; i < packed.size(); ++i)
    {
        const auto &r = packed[i];
        const auto &g = glyphs[r.id];
        int dx = r.x + padding;
        int dy = r.y + padding;
        for (int y = 0; y < g.height; ++y)
        {
            memcpy(atlas.data() + (dy + y) * w * 4 + dx * 4,
                   g.bitmap.data() + y * g.width * 4, g.width * 4);
        }
    }

    // 保存 PNG
    std::string pngFile = outputPrefix + ".png";
    stbi_write_png(pngFile.c_str(), w, h, 4, atlas.data(), w * 4);
    std::cout << "Saved " << pngFile << "\n";

    // 生成 JSON
    json j;
    j["atlas"] = {{"type", "bitmap"}, {"distanceRange", 0}, {"size", fontSize},
                  {"width", w},       {"height", h},        {"yOrigin", "bottom"}};
    j["metrics"] = {
        {"emSize", static_cast<double>(face->units_per_EM)},
        {"lineHeight", static_cast<double>(face->height) / face->units_per_EM},
        {"ascender", static_cast<double>(face->ascender) / face->units_per_EM},
        {"descender", static_cast<double>(face->descender) / face->units_per_EM},
        {"underlineY",
         static_cast<double>(face->underline_position) / face->units_per_EM},
        {"underlineThickness",
         static_cast<double>(face->underline_thickness) / face->units_per_EM}};

    double emScale = 1.0 / fontSize;
    j["glyphs"] = json::array();
    for (size_t i = 0; i < packed.size(); ++i)
    {
        const auto &r = packed[i];
        const auto &g = glyphs[r.id];
        int left = r.x + padding;
        int right = left + g.width - 1;
        int bottom = h - (r.y + padding + g.height);
        int top = bottom + g.height - 1;

        json glyphObj;
        glyphObj["unicode"] = g.unicode;
        glyphObj["advance"] = g.advance * emScale;
        glyphObj["planeBounds"] = {{"left", g.bearingX * emScale},
                                   {"bottom", (g.bearingY - g.height) * emScale},
                                   {"right", (g.bearingX + g.width) * emScale},
                                   {"top", g.bearingY * emScale}};
        glyphObj["atlasBounds"] = {
            {"left", left}, {"bottom", bottom}, {"right", right}, {"top", top}};
        j["glyphs"].push_back(glyphObj);
    }

    // Kerning
    json kerningArray = json::array();
    double kernScale = 1.0 / fontSize;
    for (size_t i = 0; i < glyphs.size(); ++i)
    {
        uint32_t lc = glyphs[i].unicode;
        FT_UInt li = FT_Get_Char_Index(face, lc);
        if (!li)
            continue;
        for (size_t j = 0; j < glyphs.size(); ++j)
        {
            if (i == j)
                continue;
            uint32_t rc = glyphs[j].unicode;
            FT_UInt ri = FT_Get_Char_Index(face, rc);
            if (!ri)
                continue;
            FT_Vector kern;
            if (FT_Get_Kerning(face, li, ri, FT_KERNING_DEFAULT, &kern) == 0)
            {
                double kp = kern.x / 64.0;
                if (kp != 0.0)
                {
                    kerningArray.push_back({{"unicode1", lc},
                                            {"unicode2", rc},
                                            {"advance", kp * kernScale}});
                }
            }
        }
    }
    if (!kerningArray.empty())
        j["kerning"] = kerningArray;

    std::string jsonFile = outputPrefix + ".json";
    std::ofstream out(jsonFile);
    out << j.dump(2);
    std::cout << "Saved " << jsonFile << "\n";

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    return 0;
}