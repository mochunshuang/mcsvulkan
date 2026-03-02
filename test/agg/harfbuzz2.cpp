// g++ -o render_g_harfbuzz render_g_harfbuzz.cpp -lfreetype -lharfbuzz -lagg
// 运行生成 render_g_harfbuzz.ppm

#include <cstdio>
#include <cstring>
#include <vector>
#include <print>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ft.h>

#include <agg_basics.h>
#include <agg_pixfmt_rgb.h>
#include <agg_rendering_buffer.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_scanline_u.h>
#include <agg_renderer_scanline.h>
#include <agg_path_storage.h>

#include <agg_conv_curve.h>

const int WIDTH = 400;
const int HEIGHT = 300;
const int BYTES_PER_PIXEL = 3; // RGB

// 将 FreeType 轮廓点转换为 AGG 路径的回调
struct OutlineToAgg
{
    agg::path_storage &path;
    double offset_x, offset_y; // 屏幕偏移（包含字形位置和 HarfBuzz 位移）
    double scale;              // 1.0 表示像素坐标

    OutlineToAgg(agg::path_storage &p, double ox, double oy, double s)
        : path(p), offset_x(ox), offset_y(oy), scale(s)
    {
    }

    static int move_to(const FT_Vector *to, void *user)
    {
        auto self = static_cast<OutlineToAgg *>(user);
        double x = self->offset_x + to->x * self->scale / 64.0;
        double y = self->offset_y -
                   to->y * self->scale / 64.0; // 翻转 Y：FreeType 向上为正，AGG 向下为正
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
    // 1. 分配画布内存并填充白色
    std::vector<unsigned char> buffer(WIDTH * HEIGHT * BYTES_PER_PIXEL, 255);

    // 2. 创建 AGG 渲染缓冲区 (步幅为正，Y 向下)
    agg::rendering_buffer rbuf(buffer.data(), WIDTH, HEIGHT, WIDTH * BYTES_PER_PIXEL);
    agg::pixfmt_rgb24 pixf(rbuf);
    agg::renderer_base<agg::pixfmt_rgb24> ren_base(pixf);
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> ren_solid(
        ren_base);
    ren_base.clear(agg::rgba8(255, 255, 255)); // 白色背景

    // 3. 初始化 FreeType
    const char *font_path =
        "TiroBangla-Regular.ttf"; // 请替换为实际存在的字体文件（例如
                                  // /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf）
    FT_Library ft_library;
    if (FT_Init_FreeType(&ft_library))
    {
        std::println(stderr, "FT_Init_FreeType failed");
        return 1;
    }
    FT_Face ft_face;
    if (FT_New_Face(ft_library, font_path, 0, &ft_face))
    {
        std::println(stderr, "FT_New_Face failed");
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 设置像素大小
    double font_size = 100.0;
    FT_Set_Pixel_Sizes(ft_face, 0, font_size);

    // 4. 初始化 HarfBuzz
    hb_font_t *hb_font = hb_ft_font_create(ft_face, nullptr);
    hb_buffer_t *hb_buffer = hb_buffer_create();

    // 添加字符 'g'
    hb_buffer_add_utf8(hb_buffer, "g", 1, 0, 1);
    hb_buffer_set_direction(hb_buffer, HB_DIRECTION_LTR);
    hb_buffer_set_script(hb_buffer, HB_SCRIPT_LATIN);
    hb_buffer_set_language(hb_buffer, hb_language_from_string("en", -1));

    // 执行整形
    hb_shape(hb_font, hb_buffer, nullptr, 0);

    // 获取结果（这里只有一个字形）
    unsigned int glyph_count = hb_buffer_get_length(hb_buffer);
    if (glyph_count == 0)
    {
        std::println(stderr, "HarfBuzz 未生成字形");
        return 1;
    }
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(hb_buffer, nullptr);
    hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(hb_buffer, nullptr);

    unsigned int glyph_index = glyph_info[0].codepoint;
    double x_offset = glyph_pos[0].x_offset / 64.0; // HarfBuzz 位置为 26.6 单位
    double y_offset = glyph_pos[0].y_offset / 64.0;

    // 5. 用 FreeType 加载该字形的轮廓
    if (FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING))
    {
        std::println(stderr, "FT_Load_Glyph failed");
        return 1;
    }

    FT_GlyphSlot slot = ft_face->glyph;
    FT_Outline *outline = &slot->outline;

    // 6. 计算字形边界（用于居中）
    FT_Glyph_Metrics &metrics = slot->metrics;
    double bbox_x_min = metrics.horiBearingX / 64.0;
    double bbox_x_max = (metrics.horiBearingX + metrics.width) / 64.0;
    double bbox_y_min = (metrics.horiBearingY - metrics.height) / 64.0; // 下边界（负值）
    double bbox_y_max = metrics.horiBearingY / 64.0;                    // 上边界（正值）

    double bbox_center_x = (bbox_x_min + bbox_x_max) / 2.0;
    double bbox_center_y = (bbox_y_min + bbox_y_max) / 2.0;

    // 7. 计算基础偏移量，使字形居中于画布（不考虑 HarfBuzz 偏移）
    double base_origin_x = WIDTH / 2.0 - bbox_center_x;
    double base_origin_y =
        HEIGHT / 2.0 +
        bbox_center_y; // 注意：AGG Y 向下为正，所以用加法（bbox_center_y 可能为负）

    // 8. 加入 HarfBuzz 的偏移
    //    HarfBuzz 的偏移是相对于基线的位移，方向与 FreeType 一致（向上为正）。
    //    在 AGG 屏幕中，y 轴向下为正，因此 y_offset 需要减去。
    double final_origin_x = base_origin_x + x_offset;
    double final_origin_y = base_origin_y - y_offset;

    // 9. 将 FreeType 轮廓转换为 AGG 路径
    agg::path_storage path;
    OutlineToAgg converter(path, final_origin_x, final_origin_y, 1.0);
    FT_Outline_Funcs funcs;
    funcs.move_to = OutlineToAgg::move_to;
    funcs.line_to = OutlineToAgg::line_to;
    funcs.conic_to = OutlineToAgg::conic_to;
    funcs.cubic_to = OutlineToAgg::cubic_to;
    funcs.shift = 0;
    funcs.delta = 0;

    FT_Outline_Decompose(outline, &funcs, &converter);

    // ★★★ 在这里插入以下两行代码 ★★★
    agg::conv_curve<agg::path_storage> curved_path(path);
    curved_path.approximation_scale(4.0); // 设置细分精度，可调节

    // 10. 使用 AGG 渲染路径
    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_u8 sl;
    ras.add_path(curved_path); // <-- 改为添加 curved_path，而不是原始的 path
    ren_solid.color(agg::rgba8(0, 0, 0));
    agg::render_scanlines(ras, sl, ren_solid);

    // 11. 输出 PPM 文件
    FILE *fd = fopen("harfbuzz2.ppm", "wb");
    if (fd)
    {
        std::fprintf(fd, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
        fwrite(buffer.data(), 1, buffer.size(), fd);
        fclose(fd);
        std::println("已生成 render_g_harfbuzz.ppm");
    }
    else
    {
        std::println(stderr, "无法写入文件");
    }

    // 12. 清理
    hb_buffer_destroy(hb_buffer);
    hb_font_destroy(hb_font);
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft_library);

    return 0;
}