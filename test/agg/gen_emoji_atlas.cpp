#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <filesystem>
#include <cstring>

// FreeType
#include <ft2build.h>
#include FT_FREETYPE_H

// stb_rect_pack
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

// stb_image_write 用于保存 PNG（也可用 libpng，这里用 stb 更简单）
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// nlohmann/json
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// 图集尺寸（可根据需要调整，建议 1024x1024 或 2048x2048）
const int ATLAS_WIDTH = 1024;
const int ATLAS_HEIGHT = 1024;
// 每个字符的渲染像素高度（FreeType 将字体缩放到此高度）
const int FONT_PIXEL_HEIGHT = 64;
// 字符之间的间距（像素），避免相邻字符采样时的混叠
const int PADDING = 1;

struct GlyphInfo
{
    uint32_t unicode;            // Unicode 码点
    int width, height;           // 位图尺寸
    int bearingX, bearingY;      // 左和上的偏移量（来自 FreeType）
    int advance;                 // 水平步进（像素，即 advance * 64？注意单位）
    std::vector<uint8_t> bitmap; // RGBA 位图数据
};

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
        0x1F600, // 😀
        0x1F601, // 😁
        0x1F602, // 😂
        0x1F603, // 😃
        0x1F604, // 😄
        0x1F605, // 😅
        0x1F606, // 😆
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

        // 创建 GlyphInfo
        GlyphInfo info;
        info.unicode = ch;
        info.width = bitmap.width;
        info.height = bitmap.rows;
        info.bearingX = face->glyph->bitmap_left;
        info.bearingY =
            face->glyph->bitmap_top; // 注意：这是相对于基线的上偏移量（正值表示在上方）
        info.advance =
            static_cast<int>(face->glyph->metrics.horiAdvance / 64); // 转换为像素

        // 复制位图数据（FT_Bitmap 是 BGRA，我们保留为 RGBA 方便后续处理）
        info.bitmap.resize(info.width * info.height * 4);
        uint8_t *dst = info.bitmap.data();
        uint8_t *src = bitmap.buffer;
        for (int i = 0; i < info.width * info.height; ++i)
        {
            // BGRA -> RGBA
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

    // 5. 使用 stb_rect_pack 打包矩形
    std::vector<stbrp_rect> rects(glyphs.size());
    for (size_t i = 0; i < glyphs.size(); ++i)
    {
        rects[i].id = static_cast<int>(i);
        rects[i].w = glyphs[i].width + 2 * PADDING; // 添加 padding
        rects[i].h = glyphs[i].height + 2 * PADDING;
        rects[i].x = 0;
        rects[i].y = 0;
        rects[i].was_packed = 0;
    }

    stbrp_context pack_ctx;
    std::vector<stbrp_node> nodes(ATLAS_WIDTH); // 每个节点代表一行，数量可调整
    stbrp_init_target(&pack_ctx, ATLAS_WIDTH, ATLAS_HEIGHT, nodes.data(), nodes.size());
    stbrp_pack_rects(&pack_ctx, rects.data(), static_cast<int>(rects.size()));

    // 检查是否有矩形未打包（图集可能太小）
    bool allPacked = true;
    for (auto &r : rects)
    {
        if (!r.was_packed)
        {
            std::cerr << "Rect for glyph " << r.id << " not packed. Atlas too small.\n";
            allPacked = false;
        }
    }
    if (!allPacked)
    {
        std::cerr << "Consider increasing ATLAS_WIDTH/HEIGHT.\n";
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return 1;
    }

    // 6. 创建图集缓冲区并绘制每个字形
    std::vector<uint8_t> atlas(ATLAS_WIDTH * ATLAS_HEIGHT * 4, 0); // 初始全透明
    for (size_t i = 0; i < rects.size(); ++i)
    {
        const auto &r = rects[i];
        const auto &glyph = glyphs[r.id];
        int destX = r.x + PADDING;
        int destY = r.y + PADDING;

        // 逐行复制位图
        for (int y = 0; y < glyph.height; ++y)
        {
            int srcRow = y * glyph.width * 4;
            int dstRow = (destY + y) * ATLAS_WIDTH * 4 + destX * 4;
            memcpy(atlas.data() + dstRow, glyph.bitmap.data() + srcRow, glyph.width * 4);
        }
    }

    // 7. 保存图集为 PNG（使用 stb_image_write）
    std::string pngPath = "emoji_atlas.png";
    stbi_write_png(pngPath.c_str(), ATLAS_WIDTH, ATLAS_HEIGHT, 4, atlas.data(),
                   ATLAS_WIDTH * 4);
    std::cout << "Saved atlas to " << pngPath << "\n";

    // 8. 生成 JSON 文件（模仿 msdf-atlas-gen 格式）
    json j;
    j["atlas"] = {
        {"type", "bitmap"},          {"distanceRange", 0}, // 无距离场，但保持字段兼容
        {"size", FONT_PIXEL_HEIGHT}, {"width", ATLAS_WIDTH}, {"height", ATLAS_HEIGHT},
        {"yOrigin", "bottom"} // 我们的图集原点在左上，但 JSON 约定左下，我们会在 UV
                              // 计算中处理
    };

    // 添加字体度量（可从 face 获取）
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
    for (size_t i = 0; i < rects.size(); ++i)
    {
        const auto &r = rects[i];
        const auto &glyph = glyphs[r.id];

        // 像素坐标的边界（不含 padding，即实际图像区域）
        int left = r.x + PADDING;
        int bottom = r.y + PADDING;
        int right = left + glyph.width - 1;
        int top = bottom + glyph.height - 1;

        // 计算平面边界（相对于 EM 单位）
        double emScale = 1.0 / face->units_per_EM;
        double planeLeft = glyph.bearingX * emScale;
        double planeRight = (glyph.bearingX + glyph.width) * emScale;
        double planeBottom =
            (glyph.bearingY - glyph.height) * emScale; // bearingY 是到基线的上偏移
        double planeTop = glyph.bearingY * emScale;

        // JSON 中需要保留整数像素坐标
        json glyphObj;
        glyphObj["unicode"] = glyph.unicode;
        glyphObj["advance"] = glyph.advance * emScale; // 转换为 EM 单位
        glyphObj["planeBounds"] = {{"left", planeLeft},
                                   {"bottom", planeBottom},
                                   {"right", planeRight},
                                   {"top", planeTop}};
        glyphObj["atlasBounds"] = {
            {"left", left}, {"bottom", bottom}, {"right", right}, {"top", top}};

        j["glyphs"].push_back(glyphObj);
    }

    std::string jsonPath = "emoji_atlas.json";
    std::ofstream out(jsonPath);
    out << j.dump(2);
    std::cout << "Saved JSON to " << jsonPath << "\n";

    // 清理
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    return 0;
}