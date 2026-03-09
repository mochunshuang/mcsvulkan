#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <filesystem>
#include <cstring>
#include <optional>
#include <cmath>

// FreeType
#include <ft2build.h>
#include FT_FREETYPE_H

// stb_rect_pack
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

// stb_image_write 用于保存 PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// nlohmann/json
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// 每个字符的渲染像素高度（FreeType 将字体缩放到此高度）
const int FONT_PIXEL_HEIGHT = 64;
// 字符之间的间距（像素），避免相邻字符采样时的混叠
const int PADDING = 1;
// 初始图集尺寸（最小尝试值）
const int INITIAL_SIZE = 128;
// 最大图集尺寸限制（防止无限循环）
const int MAX_SIZE = 4096;

// 尺寸约束类型（模仿 msdf-atlas-gen）
enum class DimensionsConstraint
{
    SQUARE,              // 正方形，尺寸为整数
    POWER_OF_TWO_SQUARE, // 2 的幂正方形
    // 可根据需要添加更多
};

struct GlyphInfo
{
    uint32_t unicode;            // Unicode 码点
    int width, height;           // 位图尺寸
    int bearingX, bearingY;      // 左和上的偏移量（来自 FreeType）
    int advance;                 // 水平步进（像素，即 advance * 64？注意单位）
    std::vector<uint8_t> bitmap; // RGBA 位图数据
};

// 尝试打包，返回 true 表示全部成功，并在 packedRects 中存储每个矩形的位置（含 padding）
bool tryPack(const std::vector<stbrp_rect> &rects, int width, int height,
             std::vector<stbrp_rect> &packedRects)
{
    packedRects = rects; // 拷贝以便修改
    stbrp_context ctx;
    // 节点数量一般等于图集宽度（每列一个节点），也可以更大，但这样通常足够
    std::vector<stbrp_node> nodes(width);
    stbrp_init_target(&ctx, width, height, nodes.data(), nodes.size());
    stbrp_pack_rects(&ctx, packedRects.data(), (int)packedRects.size());

    for (auto &r : packedRects)
    {
        if (!r.was_packed)
            return false;
    }
    return true;
}

// 自适应寻找最小图集尺寸，返回 (width, height) 或空（失败）
std::optional<std::pair<int, int>> findMinAtlasSize(
    const std::vector<GlyphInfo> &glyphs, int padding,
    DimensionsConstraint constraint = DimensionsConstraint::POWER_OF_TWO_SQUARE,
    int initialSize = INITIAL_SIZE, int maxSize = MAX_SIZE)
{
    // 准备矩形列表（每个字形对应一个带 padding 的矩形）
    std::vector<stbrp_rect> rects;
    for (size_t i = 0; i < glyphs.size(); ++i)
    {
        stbrp_rect r;
        r.id = (int)i;
        r.w = glyphs[i].width + 2 * padding;
        r.h = glyphs[i].height + 2 * padding;
        rects.push_back(r);
    }

    int width = initialSize;
    int height = initialSize;
    std::vector<stbrp_rect> packedRects;

    while (width <= maxSize && height <= maxSize)
    {
        if (tryPack(rects, width, height, packedRects))
        {
            return std::make_pair(width, height);
        }

        // 根据约束扩大尺寸
        if (constraint == DimensionsConstraint::POWER_OF_TWO_SQUARE)
        {
            // 取当前最大边的下一个 2 的幂
            int next = std::max(width, height) * 2;
            width = next;
            height = next;
        }
        else if (constraint == DimensionsConstraint::SQUARE)
        {
            // 简单翻倍
            width *= 2;
            height *= 2;
        }
        // 可添加其他约束...
    }
    return std::nullopt; // 超过最大尺寸仍失败
}

int main()
{
    // 1. 初始化 FreeType
    FT_Library ft;
    if (FT_Init_FreeType(&ft))
    {
        std::cerr << "FT_Init_FreeType failed\n";
        return 1;
    }

    // 2. 加载字体文件（请修改为实际路径）
    std::string fontPath = "C:/Windows/Fonts/seguiemj.ttf";
    FT_Face face;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face))
    {
        std::cerr << "FT_New_Face failed for " << fontPath << "\n";
        FT_Done_FreeType(ft);
        return 1;
    }

    // 设置像素大小
    FT_Set_Pixel_Sizes(face, 0, FONT_PIXEL_HEIGHT);

    // 3. 定义需要包含的字符集（示例：常用表情符号）
    std::vector<uint32_t> charset = {
        U'😀', // 😀
        U'😁', // 😁
        U'😂', // 😂
        U'😃', // 😃
        U'😄', // 😄
        U'😅', // 😅
        U'😆', // 😆
        // 可以继续添加，或从文件读取
    };

    std::vector<GlyphInfo> glyphs;
    glyphs.reserve(charset.size());

    // 4. 为每个字符生成 RGBA 位图
    for (uint32_t ch : charset)
    {
        FT_UInt glyphIndex = FT_Get_Char_Index(face, ch);
        if (glyphIndex == 0)
        {
            std::cerr << "Glyph not found for U+" << std::hex << ch << std::dec << "\n";
            continue;
        }

        // 加载彩色字形（FT_LOAD_COLOR 是关键）
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_COLOR | FT_LOAD_RENDER))
        {
            std::cerr << "FT_Load_Glyph failed for U+" << std::hex << ch << std::dec
                      << "\n";
            continue;
        }

        FT_Bitmap &bitmap = face->glyph->bitmap;
        if (bitmap.pixel_mode != FT_PIXEL_MODE_BGRA)
        {
            std::cerr << "Unexpected pixel mode for U+" << std::hex << ch << std::dec
                      << "\n";
            continue;
        }

        GlyphInfo info;
        info.unicode = ch;
        info.width = bitmap.width;
        info.height = bitmap.rows;
        info.bearingX = face->glyph->bitmap_left;
        info.bearingY = face->glyph->bitmap_top; // 相对于基线的上偏移（正值在上方）
        info.advance =
            static_cast<int>(face->glyph->metrics.horiAdvance / 64); // 转换为像素

        // 复制位图数据（BGRA -> RGBA）
        info.bitmap.resize(info.width * info.height * 4);
        uint8_t *dst = info.bitmap.data();
        uint8_t *src = bitmap.buffer;
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

    // 5. 自适应寻找最小图集尺寸
    auto atlasSizeOpt =
        findMinAtlasSize(glyphs, PADDING, DimensionsConstraint::POWER_OF_TWO_SQUARE);
    if (!atlasSizeOpt)
    {
        std::cerr << "Failed to find suitable atlas size within " << MAX_SIZE << "x"
                  << MAX_SIZE << "\n";
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return 1;
    }
    int atlasWidth = atlasSizeOpt->first;
    int atlasHeight = atlasSizeOpt->second;
    std::cout << "Determined atlas size: " << atlasWidth << "x" << atlasHeight << "\n";

    // 6. 使用最终尺寸进行打包（再次调用 tryPack 以获取最终位置）
    std::vector<stbrp_rect> rects;
    for (size_t i = 0; i < glyphs.size(); ++i)
    {
        stbrp_rect r;
        r.id = (int)i;
        r.w = glyphs[i].width + 2 * PADDING;
        r.h = glyphs[i].height + 2 * PADDING;
        rects.push_back(r);
    }
    std::vector<stbrp_rect> packedRects;
    if (!tryPack(rects, atlasWidth, atlasHeight, packedRects))
    {
        std::cerr << "Packing failed unexpectedly after size determination.\n";
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return 1;
    }

    // 7. 创建图集缓冲区并绘制每个字形
    std::vector<uint8_t> atlas(atlasWidth * atlasHeight * 4, 0); // 初始全透明
    for (size_t i = 0; i < packedRects.size(); ++i)
    {
        const auto &r = packedRects[i];
        const auto &glyph = glyphs[r.id];
        int destX = r.x + PADDING;
        int destY = r.y + PADDING;

        for (int y = 0; y < glyph.height; ++y)
        {
            int srcRow = y * glyph.width * 4;
            int dstRow = (destY + y) * atlasWidth * 4 + destX * 4;
            memcpy(atlas.data() + dstRow, glyph.bitmap.data() + srcRow, glyph.width * 4);
        }
    }

    // 8. 保存图集为 PNG
    std::string pngPath = "emoji_atlas2.png";
    stbi_write_png(pngPath.c_str(), atlasWidth, atlasHeight, 4, atlas.data(),
                   atlasWidth * 4);
    std::cout << "Saved atlas to " << pngPath << "\n";

    // 9. 生成 JSON 文件（模仿 msdf-atlas-gen 格式）
    json j;
    j["atlas"] = {
        {"type", "bitmap"},          {"distanceRange", 0}, // 无距离场，保持字段兼容
        {"size", FONT_PIXEL_HEIGHT}, {"width", atlasWidth},
        {"height", atlasHeight},     {"yOrigin", "bottom"} // 约定左下原点
    };

    // 字体度量
    j["metrics"] = {
        {"emSize", static_cast<double>(face->units_per_EM)},
        {"lineHeight", static_cast<double>(face->height) / face->units_per_EM},
        {"ascender", static_cast<double>(face->ascender) / face->units_per_EM},
        {"descender", static_cast<double>(face->descender) / face->units_per_EM},
        {"underlineY",
         static_cast<double>(face->underline_position) / face->units_per_EM},
        {"underlineThickness",
         static_cast<double>(face->underline_thickness) / face->units_per_EM}};

    j["glyphs"] = json::array();
    for (size_t i = 0; i < packedRects.size(); ++i)
    {
        const auto &r = packedRects[i];
        const auto &glyph = glyphs[r.id];

        // 计算左下原点的 atlasBounds
        int left = r.x + PADDING;
        int right = left + glyph.width - 1;
        int bottom = atlasHeight - (r.y + PADDING + glyph.height);
        int top = bottom + glyph.height - 1;

        // 平面边界（EM 单位）
        double emScale = 1.0 / FONT_PIXEL_HEIGHT; // 修正点
        double planeLeft = glyph.bearingX * emScale;
        double planeRight = (glyph.bearingX + glyph.width) * emScale;
        double planeBottom = (glyph.bearingY - glyph.height) * emScale;
        double planeTop = glyph.bearingY * emScale;

        json glyphObj;
        glyphObj["unicode"] = glyph.unicode;
        glyphObj["advance"] = glyph.advance * emScale; // advance 也要修正
        glyphObj["planeBounds"] = {{"left", planeLeft},
                                   {"bottom", planeBottom},
                                   {"right", planeRight},
                                   {"top", planeTop}};
        glyphObj["atlasBounds"] = {
            {"left", left}, {"bottom", bottom}, {"right", right}, {"top", top}};

        j["glyphs"].push_back(glyphObj);
    }
    // 在生成 glyphs 之后，准备 JSON 之前添加 kerning 提取

    // 计算 kerning（仅当字符集不太大时适用，表情符号通常足够小）
    json kerningArray = json::array();
    double emScale = 1.0 / face->units_per_EM; // 与 glyph advance 相同缩放

    for (size_t i = 0; i < glyphs.size(); ++i)
    {
        uint32_t leftChar = glyphs[i].unicode;
        FT_UInt leftIndex = FT_Get_Char_Index(face, leftChar);
        if (!leftIndex)
            continue;

        for (size_t j = 0; j < glyphs.size(); ++j)
        {
            if (i == j)
                continue; // 自身对通常无 kerning，但也可检查（一般忽略）
            uint32_t rightChar = glyphs[j].unicode;
            FT_UInt rightIndex = FT_Get_Char_Index(face, rightChar);
            if (!rightIndex)
                continue;

            FT_Vector kerning;
            if (FT_Get_Kerning(face, leftIndex, rightIndex, FT_KERNING_DEFAULT,
                               &kerning) == 0)
            {
                double kernPixels = kerning.x / 64.0; // 转换为像素
                if (kernPixels != 0.0)
                {
                    json kernObj;
                    kernObj["unicode1"] = leftChar;
                    kernObj["unicode2"] = rightChar;
                    kernObj["advance"] = kernPixels * emScale; // 转换为 EM 单位
                    kerningArray.push_back(kernObj);
                }
            }
        }
    }

    // 如果 kerningArray 不为空，则添加到 JSON
    if (!kerningArray.empty())
    {
        j["kerning"] = kerningArray;
    }

    std::string jsonPath = "emoji_atlas2.json";
    std::ofstream out(jsonPath);
    out << j.dump(2);
    std::cout << "Saved JSON to " << jsonPath << "\n";

    // 清理
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    return 0;
}