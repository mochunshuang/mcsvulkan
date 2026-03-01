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
#include <cmath>
#include <filesystem> // C++17 文件系统

// 将浮点距离场保存为 PGM 灰度图像（P2 格式，方便阅读）
void savePGM(const msdfgen::Bitmap<float, 1> &bitmap, const std::string &filename)
{
    std::ofstream file(filename);
    if (!file)
    {
        std::cerr << "Failed to open file for writing: " << filename << "\n";
        return;
    }
    file << "P2\n" << bitmap.width() << " " << bitmap.height() << "\n255\n";
    for (int y = 0; y < bitmap.height(); ++y)
    {
        for (int x = 0; x < bitmap.width(); ++x)
        {
            // msdfgen::Bitmap::operator() 返回指向像素数据的指针（多通道时为首地址）
            float v = *bitmap(x, y);              // 解引用获取第一个通道的值
            float normalized = (v + 1.0f) / 2.0f; // 映射到 [0,1]
            int gray = static_cast<int>(normalized * 255 + 0.5f);
            gray = std::max(0, std::min(255, gray));
            file << gray << (x + 1 < bitmap.width() ? " " : "\n");
        }
    }
}

int main()
{
    // Windows 10 硬编码路径（使用系统自带的 Arial 字体）
    const char *fontPath = "C:/Windows/Fonts/arial.ttf";
    const char *text = "Hello, 世界";

    // 检查字体文件是否存在
    if (!std::filesystem::exists(fontPath))
    {
        std::cerr << "Font file not found: " << fontPath << "\n";
        return 1;
    }

    // 1. 初始化 FreeType
    FT_Library ft;
    if (FT_Init_FreeType(&ft))
    {
        std::cerr << "Failed to initialize FreeType\n";
        return 1;
    }

    FT_Face face;
    if (FT_New_Face(ft, fontPath, 0, &face))
    {
        std::cerr << "Failed to load font: " << fontPath << "\n";
        FT_Done_FreeType(ft);
        return 1;
    }
    // 设置字符大小（用于 HarfBuzz 度量）
    FT_Set_Pixel_Sizes(face, 0, 64); // 64 像素高

    // 2. 创建 HarfBuzz 字体和缓冲区
    hb_font_t *hb_font = hb_ft_font_create(face, nullptr);
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text, -1, 0, -1);
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
    hb_buffer_set_language(buf, hb_language_from_string("en", -1));

    // 3. 执行整形
    hb_shape(hb_font, buf, nullptr, 0);

    unsigned int glyph_count;
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
    hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

    std::cout << "Shaped text: " << text << "\n";
    std::cout << "Glyph count: " << glyph_count << "\n\n";

    // 4. 对每个字形生成 SDF
    for (unsigned int i = 0; i < glyph_count; ++i)
    {
        unsigned int glyph_index = glyph_info[i].codepoint;

        // 加载字形轮廓（无位图，无微调）
        FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
        FT_Outline *outline = &face->glyph->outline;

        // 转换为 msdfgen Shape（使用 readFreetypeOutline，而非 loadFreetypeOutline）
        msdfgen::Shape shape;
        if (msdfgen::readFreetypeOutline(shape, outline) != 0)
        {
            std::cerr << "Failed to load outline for glyph " << glyph_index << "\n";
            continue;
        }
        shape.normalize();

        // 计算轮廓边界（新版 msdfgen 使用 shape.getBounds() 返回 Bounds 对象）
        msdfgen::Shape::Bounds bounds = shape.getBounds();
        double l = bounds.l, r = bounds.r, b = bounds.b, t = bounds.t;
        double w = r - l, h = t - b;

        // 设置 SDF 参数
        const int size = 32;      // 输出图像大小 32x32
        const double range = 4.0; // 距离场覆盖范围（像素）
        double scale = (size - 2 * range) / std::max(w, h);
        double translateX = (size / 2.0) - (l + w / 2) * scale;
        double translateY = (size / 2.0) + (b + h / 2) * scale; // Y 轴向下为正

        // 生成 SDF
        msdfgen::Bitmap<float, 1> bitmap(size, size);
        msdfgen::generateSDF(bitmap, shape, range, scale, translateX, translateY);

        // 保存为 PGM
        char filename[256];
        snprintf(filename, sizeof(filename), "glyph_%u.pgm", glyph_index);
        savePGM(bitmap, filename);
        std::cout << "Saved SDF for glyph " << glyph_index << " to " << filename << "\n";

        // 输出 HarfBuzz 位置信息（可选）
        std::cout << "  position: x_advance=" << glyph_pos[i].x_advance / 64.0
                  << " y_advance=" << glyph_pos[i].y_advance / 64.0
                  << " x_offset=" << glyph_pos[i].x_offset / 64.0
                  << " y_offset=" << glyph_pos[i].y_offset / 64.0 << "\n";
    }

    // 5. 清理
    hb_buffer_destroy(buf);
    hb_font_destroy(hb_font);
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    return 0;
}