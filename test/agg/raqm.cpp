// compile: g++ -std=c++23 -o raqm_text_layout raqm_text_layout.cpp -lfreetype -lharfbuzz
// -lraqm -lSheenBidi -lagg run: ./raqm_text_layout output: raqm_text.ppm

#include <cstdio>
#include <cstring>
#include <vector>
#include <print>
#include <cmath>
#include <string>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ft.h>

#include <raqm.h> // libraqm 头文件

#include <agg_basics.h>
#include <agg_pixfmt_rgb.h>
#include <agg_rendering_buffer.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_scanline_u.h>
#include <agg_renderer_scanline.h>
#include <agg_path_storage.h>
#include <agg_conv_curve.h>
#include <agg_conv_stroke.h>
#include <agg_trans_affine.h>

const int WIDTH = 1200;
const int HEIGHT = 400;
const int BYTES_PER_PIXEL = 3;

// ---------- 辅助函数（保持不变）----------
void draw_line_with_stroke(
    agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl,
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> &ren_solid,
    double x1, double y1, double x2, double y2, agg::rgba8 color, double width)
{
    agg::path_storage path;
    path.move_to(x1, y1);
    path.line_to(x2, y2);
    agg::conv_stroke<agg::path_storage> stroke(path);
    stroke.width(width);
    ras.reset();
    ras.add_path(stroke);
    ren_solid.color(color);
    agg::render_scanlines(ras, sl, ren_solid);
}

void draw_arrow(
    agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl,
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> &ren_solid,
    double x1, double y1, double x2, double y2, agg::rgba8 color, double head_size = 8.0)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6)
        return;
    double angle = std::atan2(dy, dx);
    double arrow_angle1 = angle + agg::pi * 5.0 / 6.0;
    double arrow_angle2 = angle - agg::pi * 5.0 / 6.0;
    double xa1 = x2 + head_size * std::cos(arrow_angle1);
    double ya1 = y2 + head_size * std::sin(arrow_angle1);
    double xa2 = x2 + head_size * std::cos(arrow_angle2);
    double ya2 = y2 + head_size * std::sin(arrow_angle2);
    agg::path_storage arrow_path;
    arrow_path.move_to(x2, y2);
    arrow_path.line_to(xa1, ya1);
    arrow_path.line_to(xa2, ya2);
    arrow_path.close_polygon();
    ras.reset();
    ras.add_path(arrow_path);
    ren_solid.color(color);
    agg::render_scanlines(ras, sl, ren_solid);
}

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
        double y = self->offset_y - to->y * self->scale / 64.0;
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

// ---------- 主函数 ----------
int main()
{
    // 1. 创建画布
    std::vector<unsigned char> buffer(WIDTH * HEIGHT * BYTES_PER_PIXEL, 255);
    agg::rendering_buffer rbuf(buffer.data(), WIDTH, HEIGHT, WIDTH * BYTES_PER_PIXEL);
    agg::pixfmt_rgb24 pixf(rbuf);
    agg::renderer_base<agg::pixfmt_rgb24> ren_base(pixf);
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> ren_solid(
        ren_base);
    ren_base.clear(agg::rgba8(255, 255, 255));

    // 2. 初始化 FreeType
    FT_Library ft_library;
    if (FT_Init_FreeType(&ft_library))
    {
        std::println(stderr, "FT_Init_FreeType failed");
        return 1;
    }

    // const char *font_path = "TiroBangla-Regular.ttf"; // 请确保字体文件存在
    const char *font_path = "C:/Windows/Fonts/msyh.ttc"; // NOTE: 世界 需要
    FT_Face ft_face;
    if (FT_New_Face(ft_library, font_path, 0, &ft_face))
    {
        std::println(stderr, "FT_New_Face failed: {}", font_path);
        FT_Done_FreeType(ft_library);
        return 1;
    }

    double font_size = 48.0;
    FT_Set_Pixel_Sizes(ft_face, 0, font_size);

    // 3. 创建 raqm 对象并进行文本布局
    const char *text = "Hello 世界 1234567890"; // 要渲染的文本（UTF-8）
    size_t text_len = strlen(text);

    raqm_t *rq = raqm_create();
    if (!rq)
    {
        std::println(stderr, "raqm_create failed");
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 设置文本（UTF-8）—— 必须传递正确的长度
    if (!raqm_set_text_utf8(rq, text, text_len))
    {
        std::println(stderr, "raqm_set_text_utf8 failed");
        raqm_destroy(rq);
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 设置字体
    if (!raqm_set_freetype_face(rq, ft_face))
    {
        std::println(stderr, "raqm_set_freetype_face failed");
        raqm_destroy(rq);
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 设置段落方向
    raqm_set_par_direction(rq, RAQM_DIRECTION_LTR);

    // 设置语言（同样需要正确的长度）
    if (!raqm_set_language(rq, "en", 0, text_len))
    {
        std::println(stderr, "raqm_set_language failed");
        raqm_destroy(rq);
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 执行布局
    if (!raqm_layout(rq))
    {
        std::println(stderr, "raqm_layout failed");
        raqm_destroy(rq);
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 获取布局结果
    size_t glyph_count;
    raqm_glyph_t *glyphs = raqm_get_glyphs(rq, &glyph_count);
    if (!glyphs || glyph_count == 0)
    {
        std::println(stderr, "raqm_get_glyphs returned empty");
        raqm_destroy(rq);
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 4. 确定起始绘制位置
    double pen_x = 50.0;
    double pen_y = HEIGHT / 2.0;

    // 5. 准备 AGG 渲染器
    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_u8 sl;

    // 6. 遍历每个字形并渲染
    for (size_t i = 0; i < glyph_count; ++i)
    {
        raqm_glyph_t &g = glyphs[i];

        FT_Face current_face = (FT_Face)g.ftface;
        if (!current_face)
            current_face = ft_face;

        if (FT_Load_Glyph(current_face, g.index, FT_LOAD_NO_BITMAP))
        {
            std::println(stderr, "FT_Load_Glyph failed for glyph {}", g.index);
            continue;
        }

        FT_Outline *outline = &current_face->glyph->outline;

        double render_x = pen_x + g.x_offset / 64.0;
        double render_y = pen_y - g.y_offset / 64.0;

        agg::path_storage path;
        OutlineToAgg converter(path, render_x, render_y, 1.0);
        FT_Outline_Funcs funcs;
        funcs.move_to = OutlineToAgg::move_to;
        funcs.line_to = OutlineToAgg::line_to;
        funcs.conic_to = OutlineToAgg::conic_to;
        funcs.cubic_to = OutlineToAgg::cubic_to;
        funcs.shift = 0;
        funcs.delta = 0;
        FT_Outline_Decompose(outline, &funcs, &converter);

        agg::conv_curve<agg::path_storage> curved_path(path);
        curved_path.approximation_scale(4.0);

        ras.reset();
        ras.add_path(curved_path);
        ren_solid.color(agg::rgba8(0, 0, 0));
        agg::render_scanlines(ras, sl, ren_solid);

        pen_x += g.x_advance / 64.0;
        pen_y -= g.y_advance / 64.0;
    }

    std::println("Rendered {} glyphs with libraqm.", glyph_count);

    // 7. 保存为 PPM 文件
    FILE *fd = fopen("raqm_text.ppm", "wb");
    if (fd)
    {
        std::fprintf(fd, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
        fwrite(buffer.data(), 1, buffer.size(), fd);
        fclose(fd);
        std::println("Saved raqm_text.ppm");
    }
    else
    {
        std::println(stderr, "Failed to write PPM file");
    }

    // 8. 清理
    raqm_destroy(rq);
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft_library);

    return 0;
}