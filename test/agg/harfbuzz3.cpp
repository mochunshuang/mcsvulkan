// g++ -o render_g_metrics render_g_metrics.cpp -lfreetype -lharfbuzz -lagg
// 运行生成 render_g_metrics.ppm

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

// 不需要 AGG 字体引擎头文件了

const int WIDTH = 800;
const int HEIGHT = 600;
const int BYTES_PER_PIXEL = 3;

// ---------- 辅助函数：绘制带箭头的线段 ----------
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

// ---------- 将 FreeType 轮廓点转换为 AGG 路径的回调 ----------
struct OutlineToAgg
{
    agg::path_storage &path;
    double offset_x, offset_y; // 屏幕偏移
    double scale;              // 1.0 表示像素坐标

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

// ---------- 自定义标签绘制函数：使用 FreeType 渲染文本，不依赖 AGG 字体引擎 ----------
void draw_text(
    FT_Face face, agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl,
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> &ren_solid,
    double x, double y, agg::rgba8 color, const std::string &text, double size = 14.0)
{
    // 临时设置像素大小
    FT_Set_Pixel_Sizes(face, 0, size);

    double cur_x = x;
    double cur_y = y;
    for (char c : text)
    {
        FT_UInt glyph_index = FT_Get_Char_Index(face, c);
        if (!glyph_index)
            continue;

        if (FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP))
            continue;

        FT_GlyphSlot slot = face->glyph;
        FT_Outline *outline = &slot->outline;

        // 获取字形度量用于步进
        double advance = slot->metrics.horiAdvance / 64.0;

        // 使用 OutlineToAgg 转换该字符的轮廓
        agg::path_storage char_path;
        OutlineToAgg converter(char_path, cur_x, cur_y, 1.0);
        FT_Outline_Funcs funcs;
        funcs.move_to = OutlineToAgg::move_to;
        funcs.line_to = OutlineToAgg::line_to;
        funcs.conic_to = OutlineToAgg::conic_to;
        funcs.cubic_to = OutlineToAgg::cubic_to;
        funcs.shift = 0;
        funcs.delta = 0;
        FT_Outline_Decompose(outline, &funcs, &converter);

        // 曲线细分
        agg::conv_curve<agg::path_storage> curved_char(char_path);
        curved_char.approximation_scale(4.0);

        // 渲染
        ras.add_path(curved_char);
        ren_solid.color(color);
        agg::render_scanlines(ras, sl, ren_solid);

        // 移动到下一个字符位置（注意：水平方向）
        cur_x += advance;
    }
}

// ---------- 带标签的箭头类（自定义，不使用 AGG 字体引擎）----------
class ArrowLabel
{
  public:
    ArrowLabel(FT_Face face, double x1, double y1, double x2, double y2,
               agg::rgba8 line_color, agg::rgba8 text_color, double line_width,
               double arrow_size, const std::string &text)
        : m_face(face), m_x1(x1), m_y1(y1), m_x2(x2), m_y2(y2), m_line_color(line_color),
          m_text_color(text_color), m_line_width(line_width), m_arrow_size(arrow_size),
          m_text(text)
    {
    }

    void set_position(double t)
    {
        m_t = t;
    }
    void set_offset(double offset)
    {
        m_offset = offset;
    }

    void draw(agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl,
              agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>>
                  &ren_solid) const
    {
        double dx = m_x2 - m_x1;
        double dy = m_y2 - m_y1;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6)
            return;
        double angle = std::atan2(dy, dx);
        double nx = -std::sin(angle);
        double ny = std::cos(angle);

        // 绘制线段
        agg::path_storage line_path;
        line_path.move_to(m_x1, m_y1);
        line_path.line_to(m_x2, m_y2);
        agg::conv_stroke<agg::path_storage> stroke(line_path);
        stroke.width(m_line_width);
        ras.reset();
        ras.add_path(stroke);
        ren_solid.color(m_line_color);
        agg::render_scanlines(ras, sl, ren_solid);

        // 绘制箭头
        double xa1 = m_x2 + m_arrow_size * std::cos(angle + agg::pi * 5.0 / 6.0);
        double ya1 = m_y2 + m_arrow_size * std::sin(angle + agg::pi * 5.0 / 6.0);
        double xa2 = m_x2 + m_arrow_size * std::cos(angle - agg::pi * 5.0 / 6.0);
        double ya2 = m_y2 + m_arrow_size * std::sin(angle - agg::pi * 5.0 / 6.0);
        agg::path_storage arrow_path;
        arrow_path.move_to(m_x2, m_y2);
        arrow_path.line_to(xa1, ya1);
        arrow_path.line_to(xa2, ya2);
        arrow_path.close_polygon();
        ras.reset();
        ras.add_path(arrow_path);
        ren_solid.color(m_line_color);
        agg::render_scanlines(ras, sl, ren_solid);

        if (m_text.empty())
            return;

        // 计算标签位置
        double cx = m_x1 + m_t * dx + nx * m_offset;
        double cy = m_y1 + m_t * dy + ny * m_offset;

        // 使用 draw_text 绘制标签
        draw_text(m_face, ras, sl, ren_solid, cx, cy, m_text_color, m_text, 14.0);
    }

  private:
    FT_Face m_face;
    double m_x1, m_y1, m_x2, m_y2;
    agg::rgba8 m_line_color, m_text_color;
    double m_line_width, m_arrow_size;
    std::string m_text;
    double m_t = 0.5;
    double m_offset = 0;
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

    // 2. 初始化 FreeType 和字体
    const char *font_path = "TiroBangla-Regular.ttf"; // 请替换为实际路径
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

    double font_size = 250.0; // 主字形大小
    FT_Set_Pixel_Sizes(ft_face, 0, font_size);

    // 3. HarfBuzz 整形
    hb_font_t *hb_font = hb_ft_font_create(ft_face, nullptr);
    hb_buffer_t *hb_buffer = hb_buffer_create();
    hb_buffer_add_utf8(hb_buffer, "g", 1, 0, 1);
    hb_buffer_set_direction(hb_buffer, HB_DIRECTION_LTR);
    hb_buffer_set_script(hb_buffer, HB_SCRIPT_LATIN);
    hb_buffer_set_language(hb_buffer, hb_language_from_string("en", -1));
    hb_shape(hb_font, hb_buffer, nullptr, 0);

    unsigned int glyph_count = hb_buffer_get_length(hb_buffer);
    if (glyph_count == 0)
    {
        std::println(stderr, "HarfBuzz 未生成字形");
        return 1;
    }
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(hb_buffer, nullptr);
    hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(hb_buffer, nullptr);

    unsigned int glyph_index = glyph_info[0].codepoint;
    double x_offset = glyph_pos[0].x_offset / 64.0;
    double y_offset = glyph_pos[0].y_offset / 64.0;

    // 4. 加载字形轮廓
    if (FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING))
    {
        std::println(stderr, "FT_Load_Glyph failed");
        return 1;
    }

    FT_GlyphSlot slot = ft_face->glyph;
    FT_Outline *outline = &slot->outline;

    // 5. 提取度量值
    FT_Glyph_Metrics &metrics = slot->metrics;
    double bearingX = metrics.horiBearingX / 64.0;
    double bearingY = metrics.horiBearingY / 64.0;
    double width = metrics.width / 64.0;
    double height = metrics.height / 64.0;
    double advance = metrics.horiAdvance / 64.0;
    double ascender = bearingY;
    double descender = (metrics.horiBearingY - metrics.height) / 64.0;
    double xMin = bearingX;
    double xMax = bearingX + width;
    double yMin = descender;
    double yMax = ascender;

    double bbox_center_x = (xMin + xMax) / 2.0;
    double bbox_center_y = (yMin + yMax) / 2.0;

    // 6. 计算基线原点
    double base_origin_x = WIDTH / 2.0 - bbox_center_x;
    double base_origin_y = HEIGHT / 2.0 + bbox_center_y; // AGG Y 向下为正
    double origin_x = base_origin_x + x_offset;
    double origin_y = base_origin_y - y_offset;

    // 7. 渲染字形
    agg::path_storage path;
    OutlineToAgg converter(path, origin_x, origin_y, 1.0);
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

    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_u8 sl;
    ras.add_path(curved_path);
    ren_solid.color(agg::rgba8(0, 0, 0));
    agg::render_scanlines(ras, sl, ren_solid);

    // 8. 计算屏幕边界
    double screen_left = origin_x + xMin;
    double screen_right = origin_x + xMax;
    double screen_top = origin_y - yMax; // 注意：AGG Y 向下，顶部 y 较小
    double screen_bottom = origin_y - yMin;

    // 定义颜色
    agg::rgba8 color_axis(80, 80, 80);
    agg::rgba8 color_bearingX(255, 0, 0);
    agg::rgba8 color_bearingY(0, 200, 0);
    agg::rgba8 color_x(0, 0, 255);
    agg::rgba8 color_y(255, 165, 0);
    agg::rgba8 color_height(128, 0, 128);
    agg::rgba8 color_width(0, 200, 200);
    agg::rgba8 color_advance(139, 69, 19);
    agg::rgba8 color_origin(0, 150, 0);

    // 绘制坐标系
    draw_line_with_stroke(ras, sl, ren_solid, 0, origin_y, WIDTH, origin_y, color_axis,
                          1.5);
    draw_line_with_stroke(ras, sl, ren_solid, origin_x, 0, origin_x, HEIGHT, color_axis,
                          1.5);
    draw_arrow(ras, sl, ren_solid, origin_x + xMax - 20, origin_y, origin_x + xMax,
               origin_y, color_axis, 8);
    draw_arrow(ras, sl, ren_solid, origin_x, origin_y - yMax + 20, origin_x,
               origin_y - yMax, color_axis, 8);

    // 绘制原点标记
    ren_solid.color(color_origin);
    ras.reset();
    ras.move_to_d(origin_x - 3, origin_y - 3);
    ras.line_to_d(origin_x + 3, origin_y + 3);
    ras.move_to_d(origin_x + 3, origin_y - 3);
    ras.line_to_d(origin_x - 3, origin_y + 3);
    agg::render_scanlines(ras, sl, ren_solid);
    // 原点标签也使用 draw_text
    draw_text(ft_face, ras, sl, ren_solid, origin_x + 10, origin_y - 10,
              agg::rgba8(0, 0, 0), "(0,0)", 14.0);

    auto to_str = [](double v) {
        return std::to_string(v).substr(0, 6);
    };

    // 标注 bearingX
    ArrowLabel arrow_bearingX(ft_face, origin_x, origin_y, origin_x + bearingX, origin_y,
                              color_bearingX, color_bearingX, 2.0, 12.0,
                              "bearingX " + to_str(bearingX));
    arrow_bearingX.set_position(0.7);
    arrow_bearingX.set_offset(15.0);
    arrow_bearingX.draw(ras, sl, ren_solid);

    // bearingY
    ArrowLabel arrow_bearingY(ft_face, origin_x, origin_y, origin_x, screen_top,
                              color_bearingY, color_bearingY, 2.0, 12.0,
                              "bearingY " + to_str(bearingY));
    arrow_bearingY.set_position(0.7);
    arrow_bearingY.set_offset(15.0);
    arrow_bearingY.draw(ras, sl, ren_solid);

    // xMin
    ArrowLabel arrow_xMin(ft_face, screen_left, screen_top, screen_left, screen_bottom,
                          color_x, color_x, 2.0, 12.0, "xMin " + to_str(xMin));
    arrow_xMin.set_position(0.5);
    arrow_xMin.set_offset(-15.0);
    arrow_xMin.draw(ras, sl, ren_solid);

    // xMax
    ArrowLabel arrow_xMax(ft_face, screen_right, screen_top, screen_right, screen_bottom,
                          color_x, color_x, 2.0, 12.0, "xMax " + to_str(xMax));
    arrow_xMax.set_position(0.5);
    arrow_xMax.set_offset(15.0);
    arrow_xMax.draw(ras, sl, ren_solid);

    // ascender
    ArrowLabel arrow_ascender(ft_face, screen_left, screen_top, screen_right, screen_top,
                              color_y, color_y, 2.0, 12.0,
                              "ascender " + to_str(ascender));
    arrow_ascender.set_position(0.5);
    arrow_ascender.set_offset(15.0);
    arrow_ascender.draw(ras, sl, ren_solid);

    // descender
    ArrowLabel arrow_descender(ft_face, screen_left, screen_bottom, screen_right,
                               screen_bottom, color_y, color_y, 2.0, 12.0,
                               "descender " + to_str(descender));
    arrow_descender.set_position(0.5);
    arrow_descender.set_offset(-15.0);
    arrow_descender.draw(ras, sl, ren_solid);

    // height
    double h_x = screen_right + 50;
    ArrowLabel arrow_height(ft_face, h_x, screen_bottom, h_x, screen_top, color_height,
                            color_height, 2.0, 12.0, "height " + to_str(height));
    arrow_height.set_position(0.5);
    arrow_height.set_offset(15.0);
    arrow_height.draw(ras, sl, ren_solid);

    // width
    double w_y = screen_bottom + 50;
    ArrowLabel arrow_width(ft_face, screen_left, w_y, screen_right, w_y, color_width,
                           color_width, 2.0, 12.0, "width " + to_str(width));
    arrow_width.set_position(0.5);
    arrow_width.set_offset(15.0);
    arrow_width.draw(ras, sl, ren_solid);

    // advance
    double adv_x = origin_x + advance;
    double adv_y = origin_y + 70;
    ArrowLabel arrow_advance(ft_face, origin_x, adv_y, adv_x, adv_y, color_advance,
                             color_advance, 2.0, 12.0, "advance " + to_str(advance));
    arrow_advance.set_position(0.5);
    arrow_advance.set_offset(15.0);
    arrow_advance.draw(ras, sl, ren_solid);

    // 输出 PPM
    FILE *fd = fopen("harfbuzz3.ppm", "wb");
    if (fd)
    {
        std::fprintf(fd, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
        fwrite(buffer.data(), 1, buffer.size(), fd);
        fclose(fd);
        std::println("已生成 harfbuzz_metrics_noaggfont.ppm");
    }
    else
    {
        std::println(stderr, "无法写入文件");
    }

    // 清理
    hb_buffer_destroy(hb_buffer);
    hb_font_destroy(hb_font);
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft_library);

    return 0;
}