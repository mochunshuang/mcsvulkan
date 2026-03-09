#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <optional>
#include <cctype>

// FreeType
#include <ft2build.h>
#include FT_FREETYPE_H

// rectpack2D (header-only)
#include <rectpack2D/finders_interface.h>

// stb_image_write
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// nlohmann/json
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <cuchar> // std::mbrtoc32, std::mbstate_t

struct GlyphInfo
{
    uint32_t unicode;
    int width, height;
    int bearingX, bearingY;
    int advance;                 // 像素
    std::vector<uint8_t> bitmap; // RGBA
};

// ---------- rectpack2D 辅助类型 ----------
constexpr bool ALLOW_FLIP = false;
using spaces_type =
    rectpack2D::empty_spaces<ALLOW_FLIP, rectpack2D::default_empty_spaces>;
using rect_type = rectpack2D::output_rect_t<spaces_type>;

struct PackableGlyph
{
    rect_type rect;
    const GlyphInfo *glyph;
    int padded_w, padded_h;
    PackableGlyph(const GlyphInfo *g, int padding)
        : glyph(g), padded_w(g->width + 2 * padding), padded_h(g->height + 2 * padding)
    {
        rect.w = padded_w;
        rect.h = padded_h;
    }
    auto &get_rect()
    {
        return rect;
    }
    const auto &get_rect() const
    {
        return rect;
    }
};

// 尝试固定尺寸打包
bool tryPackFixedSize(const std::vector<GlyphInfo> &glyphs, int padding, int width,
                      int height, std::vector<PackableGlyph> &packed)
{
    std::vector<PackableGlyph> candidates;
    candidates.reserve(glyphs.size());
    for (const auto &g : glyphs)
        candidates.emplace_back(&g, padding);

    std::sort(candidates.begin(), candidates.end(),
              [](const PackableGlyph &a, const PackableGlyph &b) {
                  return (a.padded_w * a.padded_h) > (b.padded_w * b.padded_h);
              });

    spaces_type root({width, height});
    root.flipping_mode = rectpack2D::flipping_option::DISABLED;

    for (auto &cand : candidates)
    {
        if (auto inserted = root.insert({cand.padded_w, cand.padded_h}))
            cand.rect = *inserted;
        else
            return false;
    }
    packed = std::move(candidates);
    return true;
}

// 自动寻找最小尺寸
std::optional<std::pair<int, int>> findMinAtlasSize(
    const std::vector<GlyphInfo> &glyphs, int padding, int maxSize,
    std::vector<PackableGlyph> &out_packed)
{
    std::vector<PackableGlyph> candidates;
    candidates.reserve(glyphs.size());
    for (const auto &g : glyphs)
        candidates.emplace_back(&g, padding);

    const int discard_step = -4; // 精细搜索
    auto result_size = rectpack2D::find_best_packing<spaces_type>(
        candidates,
        rectpack2D::make_finder_input(
            maxSize, discard_step,
            [](rect_type &) { return rectpack2D::callback_result::CONTINUE_PACKING; },
            [](rect_type &) { return rectpack2D::callback_result::ABORT_PACKING; },
            rectpack2D::flipping_option::DISABLED));

    if (result_size.w == 0 || result_size.h == 0)
        return std::nullopt;

    out_packed = std::move(candidates);
    return std::make_pair(result_size.w, result_size.h);
}

// ---------- UTF-8 辅助函数 ----------
// 将 UTF-8 字符串的第一个字符转换为 Unicode 码点，并返回码点和消耗的字节数
std::optional<uint32_t> utf8_to_codepoint(const char *&str)
{
    if (!str || !*str)
        return std::nullopt;
    std::mbstate_t state{};
    char32_t cp = 0;
    // 假设字符串以 null 结尾，因此剩余长度可用 strlen 或已知大小
    std::size_t result = std::mbrtoc32(&cp, str, 5, &state); // 5 > 最大 UTF-8 长度
    if (result == (std::size_t)-1 || result == (std::size_t)-3)
    {
        return std::nullopt; // 编码错误或不完整
    }
    if (result == (std::size_t)-2)
    {
        // 输入不足，但字符串以 null 结尾，通常不会发生
        return std::nullopt;
    }
    str += (result > 0 ? result : 1); // result 为 0 表示空字符，移动 1 字节
    return static_cast<uint32_t>(cp);
}

// 读取字符集（精简后）
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
            if (auto cp = utf8_to_codepoint(str))
            {
                result.push_back(*cp);
            }
            else
            {
                std::cerr << "Warning: invalid UTF-8 inside quotes: " << line << "\n";
            }
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
            if (auto cp = utf8_to_codepoint(str))
            {
                result.push_back(*cp);
            }
            else
            {
                std::cerr << "Warning: invalid UTF-8: " << line << "\n";
            }
        }
    }
    return result;
}

int main(int argc, char *argv[])
{
    std::ignore = std::setlocale(LC_ALL, "en_US.utf8");
    // 默认参数
    // std::string fontPath = "C:/Windows/Fonts/seguiemj.ttf";
    // auto pre = std::string{"E:/0_github_project/mcsvulkan/assets/charset"};
    // std::string charsetFile = pre + "/small_emoji.txt";
    // std::string outputPrefix = "emoji";

    std::string fontPath;
    auto pre = std::string{};
    std::string charsetFile;
    std::string file_name;

    // 默认参数
    constexpr int DEFAULT_FONT_SIZE = 64;
    constexpr int DEFAULT_PADDING = 1;
    int MAX_SIZE = 4096; // NOLINT
    int fontSize = DEFAULT_FONT_SIZE;
    int padding = DEFAULT_PADDING;
    int fixedWidth = -1;
    int fixedHeight = -1;

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
        else if (arg == "-file_name" && i + 1 < argc)
        {
            file_name = argv[++i];
        }
        else if (arg == "-dimensions" && i + 2 < argc)
        {
            fixedWidth = std::stoi(argv[++i]);
            fixedHeight = std::stoi(argv[++i]);
        }
        else if (arg == "-max-size" && i + 1 < argc)
        {
            MAX_SIZE = std::stoi(argv[++i]);
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

    // ---------- 打包图集 ----------
    std::vector<PackableGlyph> packedGlyphs;
    int w, h;

    if (fixedWidth > 0 && fixedHeight > 0)
    {
        // 用户指定了固定尺寸，直接尝试在该尺寸下打包
        if (!tryPackFixedSize(glyphs, padding, fixedWidth, fixedHeight, packedGlyphs))
        {
            std::cerr << "Cannot fit glyphs into specified dimensions: " << fixedWidth
                      << "x" << fixedHeight << "\n";
            FT_Done_Face(face);
            FT_Done_FreeType(ft);
            return 1;
        }
        w = fixedWidth;
        h = fixedHeight;
    }
    else
    {
        // 自动寻找最小尺寸
        auto atlasSize = findMinAtlasSize(glyphs, padding, MAX_SIZE, packedGlyphs);
        if (!atlasSize)
        {
            std::cerr << "Cannot fit glyphs within " << MAX_SIZE << "x" << MAX_SIZE
                      << "\n";
            FT_Done_Face(face);
            FT_Done_FreeType(ft);
            return 1;
        }
        w = atlasSize->first;
        h = atlasSize->second;
    }

    std::cout << "Atlas size: " << w << "x" << h << "\n";

    // ---------- 创建图集缓冲区 ----------
    std::vector<uint8_t> atlas(w * h * 4, 0);
    for (const auto &p : packedGlyphs)
    {
        const auto &r = p.rect;   // 包含 padding 的矩形
        const auto &g = *p.glyph; // 原始字形数据
        int dx = r.x + padding;
        int dy = r.y + padding;
        // 复制位图数据（RGBA）
        for (int y = 0; y < g.height; ++y)
        {
            ::memcpy(atlas.data() + (dy + y) * w * 4 + dx * 4,
                     g.bitmap.data() + y * g.width * 4, g.width * 4);
        }
    }

    // 保存 PNG
    std::string pngFile = file_name + ".png";
    stbi_write_png(pngFile.c_str(), w, h, 4, atlas.data(), w * 4);
    std::cout << "Saved " << pngFile << "\n";

    // ---------- 生成 JSON ----------
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

    for (const auto &p : packedGlyphs)
    {
        const auto &r = p.rect;
        const auto &g = *p.glyph;
        int left = r.x + padding;
        int right = left + g.width - 1;
        int bottom = h - (r.y + padding + g.height); // 转换为底部原点
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

    // Kerning（与原代码相同）
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

    std::string jsonFile = file_name + ".json";
    std::ofstream out(jsonFile);
    // out << j.dump(2);
    out << j.dump(); // 无缩进，紧凑格式
    std::cout << "Saved " << jsonFile << "\n";

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    return 0;
}