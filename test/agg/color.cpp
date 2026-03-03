// g++ -o colordemo colordemo.cpp -lfreetype -lagg
// 运行生成 colordemo.ppm，应显示一个彩色红心（U+2764）

#include <fstream>
#include <vector>
#include <cmath>
#include <iostream>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_COLOR_H // 彩色字体支持

#include <agg_basics.h>
#include <agg_pixfmt_rgb.h>
#include <agg_rendering_buffer.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_scanline_u.h>
#include <agg_renderer_scanline.h>
#include <agg_path_storage.h>
#include <agg_conv_curve.h>

const unsigned WIDTH = 400;
const unsigned HEIGHT = 400;
const unsigned BYTES_PER_PIXEL = 3;

// 将 FreeType 轮廓转换为 AGG 路径
struct OutlineToAgg
{
    agg::path_storage &path;
    double offset_x, offset_y;
    double scale;

    OutlineToAgg(agg::path_storage &p, double ox, double oy, double s)
        : path(p), offset_x(ox), offset_y(oy), scale(s)
    {
    }

    static int move_to(const FT_Vector *to, void *user)
    {
        auto self = static_cast<OutlineToAgg *>(user);
        double x = self->offset_x + to->x * self->scale / 64.0;
        double y = self->offset_y - to->y * self->scale / 64.0; // 翻转 Y
        self->path.move_to(x, y);
        return 0;
    }
    static int line_to(const FT_Vector *to, void *user)
    {
        auto self = static_cast<OutlineToAgg *>(user);
        double x = self->offset_x + to->x * self->scale / 64.0;
        double y = self->offset_y - to->y * self->scale / 64.0;
        self->path.line_to(x, y);
        return 0;
    }
    static int conic_to(const FT_Vector *ctrl, const FT_Vector *to, void *user)
    {
        auto self = static_cast<OutlineToAgg *>(user);
        double cx = self->offset_x + ctrl->x * self->scale / 64.0;
        double cy = self->offset_y - ctrl->y * self->scale / 64.0;
        double tx = self->offset_x + to->x * self->scale / 64.0;
        double ty = self->offset_y - to->y * self->scale / 64.0;
        self->path.curve3(cx, cy, tx, ty);
        return 0;
    }
    static int cubic_to(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to,
                        void *user)
    {
        auto self = static_cast<OutlineToAgg *>(user);
        double c1x = self->offset_x + c1->x * self->scale / 64.0;
        double c1y = self->offset_y - c1->y * self->scale / 64.0;
        double c2x = self->offset_x + c2->x * self->scale / 64.0;
        double c2y = self->offset_y - c2->y * self->scale / 64.0;
        double tx = self->offset_x + to->x * self->scale / 64.0;
        double ty = self->offset_y - to->y * self->scale / 64.0;
        self->path.curve4(c1x, c1y, c2x, c2y, tx, ty);
        return 0;
    }
};

int main()
{
    // 1. 初始化 FreeType
    FT_Library ft_library;
    if (FT_Init_FreeType(&ft_library))
    {
        std::cerr << "FT_Init_FreeType failed\n";
        return 1;
    }

    // Windows 系统彩色 emoji 字体
    const char *font_path = "C:/Windows/Fonts/seguiemj.ttf";
    FT_Face face;
    if (FT_New_Face(ft_library, font_path, 0, &face))
    {
        std::cerr << "加载字体失败: " << font_path << "\n";
        FT_Done_FreeType(ft_library);
        return 1;
    }

    double font_size = 100.0;
    FT_Set_Pixel_Sizes(face, 0, font_size);

    // 2. 选择要渲染的彩色字符（例如 “❤️” 红心，U+2764）
    // 或者更复杂的 “👩‍❤️‍👩” 但需要
    // shaping，这里简化：直接用单个码点 U+2764
    FT_ULong charcode = 0x2764;
    FT_UInt glyph_index = FT_Get_Char_Index(face, charcode);
    if (!glyph_index)
    {
        std::cerr << "字形不存在 (U+2764)\n";
        FT_Done_Face(face);
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 3. 加载字形（启用彩色）

    // 1. 尝试加载彩色字形
    if (FT_Load_Glyph(face, glyph_index, FT_LOAD_COLOR | FT_LOAD_NO_BITMAP))
    {
        printf("FT_Load_Glyph (with color) failed.\n");
    }
    else
    {
        printf("FT_Load_Glyph (with color) succeeded.\n");

        // 2. 检查是否有颜色
        if (FT_HAS_COLOR(face))
        {
            printf("Font has color. Attempting to get palette...\n");

            // 3. 获取调色板
            FT_Color *palette = nullptr;
            if (FT_Palette_Select(face, 0, &palette) != 0)
            {
                printf("FT_Palette_Select failed.\n");
            }
            else
            {
                printf("FT_Palette_Select succeeded. Palette address: %p\n", palette);

                // 4. 遍历图层
                FT_LayerIterator iterator;
                FT_UInt layer_glyph;
                FT_UInt layer_color_index;
                iterator.p = nullptr;
                int layer_count = 0;

                while (FT_Get_Color_Glyph_Layer(face, glyph_index, &layer_glyph,
                                                &layer_color_index, &iterator))
                {
                    layer_count++;
                    printf("  Layer %d: glyph index = %d, color index = %d\n",
                           layer_count, layer_glyph, layer_color_index);

                    // 5. 检查是否能从这个索引拿到颜色
                    if (palette)
                    {
                        FT_Color color = palette[layer_color_index];
                        printf("    Color from palette: R=%d, G=%d, B=%d, A=%d\n",
                               color.red, color.green, color.blue, color.alpha);
                    }
                }
                if (layer_count == 0)
                {
                    printf("FT_Get_Color_Glyph_Layer returned no layers.\n");
                }
            }
        }
        else
        {
            printf("Font does NOT have color.\n");
        }
    }

    // 4. 检查是否为彩色
    if (!FT_HAS_COLOR(face))
    {
        std::cerr << "该字形不是彩色\n";
        FT_Done_Face(face);
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 5. 获取调色板（通常索引0）
    FT_Color *palette = nullptr;
    if (FT_Palette_Select(face, 0, &palette) != 0)
    {
        std::cerr << "FT_Palette_Select 失败\n";
        FT_Done_Face(face);
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 6. 创建 AGG 画布
    std::vector<unsigned char> buffer(WIDTH * HEIGHT * BYTES_PER_PIXEL, 255);
    agg::rendering_buffer rbuf(buffer.data(), WIDTH, HEIGHT, WIDTH * BYTES_PER_PIXEL);
    agg::pixfmt_rgb24 pixf(rbuf);
    agg::renderer_base<agg::pixfmt_rgb24> ren_base(pixf);
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> ren_solid(
        ren_base);
    ren_base.clear(agg::rgba8(255, 255, 255));

    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_u8 sl;

    // 计算居中位置
    FT_Glyph_Metrics &metrics = face->glyph->metrics;
    double pen_x = (WIDTH - metrics.width / 64.0) / 2.0;
    double pen_y = (HEIGHT + metrics.height / 64.0) / 2.0; // 基线

    // 7. 遍历所有图层
    FT_LayerIterator iterator;
    FT_UInt layer_glyph;
    FT_UInt layer_color_index;
    iterator.p = nullptr;

    while (FT_Get_Color_Glyph_Layer(face, glyph_index, &layer_glyph, &layer_color_index,
                                    &iterator))
    {
        if (FT_Load_Glyph(face, layer_glyph, FT_LOAD_NO_BITMAP))
            continue;

        // 获取颜色（16位转8位）
        FT_Color color = palette[layer_color_index];
        agg::rgba8 render_color(color.red, color.green, color.blue, 255);
        FT_Outline *outline = &face->glyph->outline;

        agg::path_storage path;
        OutlineToAgg converter(path, pen_x, pen_y, 1.0);
        FT_Outline_Funcs funcs = {OutlineToAgg::move_to,
                                  OutlineToAgg::line_to,
                                  OutlineToAgg::conic_to,
                                  OutlineToAgg::cubic_to,
                                  0,
                                  0};
        FT_Outline_Decompose(outline, &funcs, &converter);

        agg::conv_curve<agg::path_storage> curved(path);
        curved.approximation_scale(4.0);

        ras.reset();
        ras.add_path(curved);
        ren_solid.color(render_color);
        agg::render_scanlines(ras, sl, ren_solid);
    }

    // 8. 输出 PPM
    std::ofstream out("colordemo.ppm", std::ios::binary);
    out << "P6\n" << WIDTH << " " << HEIGHT << "\n255\n";
    out.write(reinterpret_cast<char *>(buffer.data()), buffer.size());
    out.close();

    std::cout << "已生成 colordemo.ppm\n";

    FT_Done_Face(face);
    FT_Done_FreeType(ft_library);
    return 0;
}