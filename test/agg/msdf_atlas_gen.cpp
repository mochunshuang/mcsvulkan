#include <iostream>
#include <fstream>
#include <string_view>
#include <vector>
#include <string>
#include <filesystem>
#include <cstring>
#include <map>

// msdfgen 核心和扩展
#include <msdfgen.h>
#include <msdfgen-ext.h>

// stb_image 用于加载 PNG（仅保留加载部分）
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// nlohmann/json
#include <nlohmann/json.hpp>
using json = nlohmann::json;

std::string unicodeToUTF8(uint32_t cp)
{
    std::string result;
    if (cp < 0x80)
    {
        result += static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return result;
}

int main()
{
    auto pre = std::string{MSDF_OUTPUT_DIR};
    auto atlasPath = pre + "/msyh_chinese.png";
    auto jsonPath = pre + "/msyh_chinese.json";
    auto outDir = "glyphs";

    std::filesystem::create_directories(outDir);

    // 加载图集 PNG（强制 RGB 三通道）
    int w, h, comp;
    unsigned char *atlasPixels = stbi_load(atlasPath.data(), &w, &h, &comp, 3);
    if (!atlasPixels)
    {
        std::cerr << "Failed to load atlas image: " << atlasPath << std::endl;
        return 1;
    }
    std::cout << "Atlas loaded: " << w << "x" << h << ", channels=" << comp << std::endl;

    // 解析 JSON
    std::ifstream f(jsonPath);
    if (!f.is_open())
    {
        std::cerr << "Failed to open JSON file: " << jsonPath << std::endl;
        stbi_image_free(atlasPixels);
        return 1;
    }
    json data = json::parse(f);

    int atlasW = data["atlas"]["width"];
    int atlasH = data["atlas"]["height"];
    std::string yOrigin = data["atlas"]["yOrigin"]; // 应为 "bottom"

    if (w != atlasW || h != atlasH)
    {
        std::cerr << "Warning: PNG dimensions (" << w << "x" << h
                  << ") differ from JSON atlas dimensions (" << atlasW << "x" << atlasH
                  << ")" << std::endl;
    }

    // 遍历所有字符，裁剪并保存
    for (const auto &glyph : data["glyphs"])
    {
        uint32_t unicode = glyph["unicode"];
        float left = glyph["atlasBounds"]["left"];
        float bottom = glyph["atlasBounds"]["bottom"];
        float right = glyph["atlasBounds"]["right"];
        float top = glyph["atlasBounds"]["top"];

        int x0 = static_cast<int>(left);
        int y0 = static_cast<int>(bottom);
        int x1 = static_cast<int>(right);
        int y1 = static_cast<int>(top);
        int glyphW = x1 - x0 + 1;
        int glyphH = y1 - y0 + 1;

        if (glyphW <= 0 || glyphH <= 0)
        {
            std::cerr << "Invalid bounds for U+" << std::hex << unicode << std::dec
                      << std::endl;
            continue;
        }

        std::vector<unsigned char> glyphPixels(glyphW * glyphH * 3);

        // Y轴翻转（JSON左下原点 → 内存左上原点）
        for (int gy = 0; gy < glyphH; ++gy)
        {
            int srcY = y0 + gy;
            int memoryY = atlasH - 1 - srcY; // 转换为内存行（顶部为0）

            if (memoryY < 0 || memoryY >= atlasH)
                continue;

            const unsigned char *srcRow = atlasPixels + (memoryY * atlasW + x0) * 3;
            unsigned char *dstRow = glyphPixels.data() + gy * glyphW * 3;
            memcpy(dstRow, srcRow, glyphW * 3);
        }

        // 生成输出文件名（用 Unicode 字符映射）
        std::string utf8char = unicodeToUTF8(unicode);
        std::map<std::string, std::string> name_map = {
            {"一", "1"}, {"不", "2"}, {"在", "3"}, {"是", "4"}, {"的", "5"}};
        std::string filename = std::string(outDir) + "/" + name_map.at(utf8char) + ".png";

        // 使用 msdfgen::savePng 保存
        msdfgen::Bitmap<msdfgen::byte, 3> bitmap(glyphW, glyphH,
                                                 msdfgen::YAxisOrientation::Y_UPWARD);
        // 将 glyphPixels 数据按行复制到 bitmap 中
        for (int y = 0; y < glyphH; ++y)
        {
            msdfgen::byte *dstRow = bitmap(0, y);
            const unsigned char *srcRow = glyphPixels.data() + y * glyphW * 3;
            std::memcpy(dstRow, srcRow, glyphW * 3);
        }
        if (msdfgen::savePng(bitmap, filename.c_str()))
        {
            std::cout << "Saved " << filename << std::endl;
        }
        else
        {
            std::cerr << "Failed to save " << filename << std::endl;
        }
    }

    stbi_image_free(atlasPixels);
    std::cout << "All glyphs extracted." << std::endl;
    return 0;
}