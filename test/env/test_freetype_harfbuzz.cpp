#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <hb.h>
#include <hb-ft.h>

#include <iostream>
#include <string>
#include <vector>
#include <cstring>

// 尝试在常见系统路径中查找字体文件
std::string find_font_file()
{
    std::vector<std::string> candidates = {
#ifdef _WIN32
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/seguiemj.ttf", // Windows 10 表情字体
#elif __APPLE__
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
#else // Linux / Unix
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
#endif
    };
    for (const auto &path : candidates)
    {
        FILE *f = fopen(path.c_str(), "rb");
        if (f)
        {
            fclose(f);
            return path;
        }
    }
    return "";
}

int main()
{
    // 1. 初始化 FreeType
    FT_Library ft_library;
    FT_Error error = FT_Init_FreeType(&ft_library);
    if (error)
    {
        std::cerr << "Failed to init FreeType\n";
        return 1;
    }

    // 2. 加载字体文件
    std::string font_path = find_font_file();
    if (font_path.empty())
    {
        std::cerr << "Could not find a suitable font file.\n"
                  << "Please place a font at a known location or modify the code.\n";
        return 1;
    }
    std::cout << "Using font: " << font_path << "\n";

    FT_Face ft_face;
    error = FT_New_Face(ft_library, font_path.c_str(), 0, &ft_face);
    if (error)
    {
        std::cerr << "Failed to load font face\n";
        return 1;
    }

    // 设置字体大小（例如 16pt）
    FT_Set_Pixel_Sizes(ft_face, 0, 16);

    // 3. 创建 HarfBuzz 字体对象（基于 FreeType face）
    hb_font_t *hb_font = hb_ft_font_create(ft_face, nullptr);
    if (hb_font == nullptr)
    {
        std::cerr << "Failed to create HarfBuzz font from FreeType face\n";
        return 1;
    }
    // 4. 准备 Unicode 文本（混合英语和阿拉伯语）
    const char *text = "Hello, 世界! مرحبا بالعالم";
    std::cout << "Text: " << text << "\n\n";

    // 创建 HarfBuzz 缓冲区
    hb_buffer_t *buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(buffer);

    // 5. 执行整形
    hb_shape(hb_font, buffer, nullptr, 0);

    // 6. 获取整形结果
    unsigned int glyph_count;
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    // 检查整形结果
    if (glyph_count == 0)
    {
        std::cerr << "Shaping produced no glyphs\n";
        return 1;
    }
    hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buffer, &glyph_count);

    std::cout << "Glyphs after shaping:\n";
    for (unsigned int i = 0; i < glyph_count; ++i)
    {
        std::cout << "  glyph[" << i << "] = " << glyph_info[i].codepoint << " (cluster "
                  << glyph_info[i].cluster << ")"
                  << " pos: (" << glyph_pos[i].x_advance << ", " << glyph_pos[i].y_advance
                  << ")"
                  << "\n";
    }

    // 7. 用 FreeType 加载每个字形的轮廓（验证数据一致）
    std::cout << "\nFreeType glyph metrics:\n";
    for (unsigned int i = 0; i < glyph_count; ++i)
    {
        FT_UInt glyph_index = glyph_info[i].codepoint;
        // 检查 FT_Load_Glyph 是否成功
        FT_Error error = FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);
        if (error != 0)
        {
            std::cerr << "Failed to load glyph " << glyph_index << "\n";
        }
        FT_GlyphSlot slot = ft_face->glyph;

        std::cout << "  glyph index " << glyph_index
                  << "  width = " << slot->metrics.width / 64.0
                  << "  height = " << slot->metrics.height / 64.0 << "\n";
    }

    // 清理
    hb_buffer_destroy(buffer);
    hb_font_destroy(hb_font);
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft_library);

    return 0;
}