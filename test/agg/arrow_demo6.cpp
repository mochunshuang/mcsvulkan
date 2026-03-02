// g++ -o glyph_metrics_clean glyph_metrics_clean.cpp -lagg -lfreetype
// 运行后生成 glyph_metrics_clean.ppm 并输出度量比较

#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <iostream>
#include <iomanip>
#include <agg_basics.h>
#include <agg_pixfmt_rgb.h>
#include <agg_rendering_buffer.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_scanline_u.h>
#include <agg_renderer_scanline.h>
#include <agg_path_storage.h>
#include <agg_conv_stroke.h>
#include <agg_conv_transform.h>
#include <agg_conv_curve.h>
#include <agg_font_freetype.h>
#include <agg_math.h>
#include <agg_trans_affine.h>
#include <ft2build.h>
#include FT_FREETYPE_H

const unsigned WIDTH = 800;
const unsigned HEIGHT = 600;

// 度量结构体（仅用于验证比较，不再用于主绘制）
struct GlyphMetrics
{
    double ascender;
    double descender;
    double bearingX;
    double bearingY;
    double xMin, xMax;
    double yMax, yMin;
    double width, height;
    double advance;
};

// ---------- 辅助函数（保持不变）----------
double get_text_width(agg::font_cache_manager<agg::font_engine_freetype_int32> &fman,
                      const std::string &text)
{
    double w = 0;
    for (char c : text)
    {
        const agg::glyph_cache *glyph = fman.glyph(c);
        if (glyph)
            w += glyph->advance_x;
    }
    return w;
}

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

void draw_label(
    agg::font_engine_freetype_int32 &feng,
    agg::font_cache_manager<agg::font_engine_freetype_int32> &fman,
    agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl,
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> &ren_solid,
    double x, double y, agg::rgba8 color, const std::string &text)
{
    feng.height(14.0);
    feng.width(14.0);
    feng.flip_y(true);
    feng.hinting(false);
    double sx = x;
    double sy = y;
    const char *p = text.c_str();
    while (*p)
    {
        const agg::glyph_cache *glyph = fman.glyph(*p);
        if (glyph)
        {
            fman.init_embedded_adaptors(glyph, sx, sy);
            if (glyph->data_type == agg::glyph_data_outline)
            {
                ras.reset();
                agg::conv_curve<agg::font_cache_manager<
                    agg::font_engine_freetype_int32>::path_adaptor_type>
                    curves(fman.path_adaptor());
                ras.add_path(curves);
                ren_solid.color(color);
                agg::render_scanlines(ras, sl, ren_solid);
            }
            sx += glyph->advance_x;
            sy += glyph->advance_y;
        }
        ++p;
    }
}

class ArrowLabel
{
  public:
    ArrowLabel(agg::font_engine_freetype_int32 &feng,
               agg::font_cache_manager<agg::font_engine_freetype_int32> &fman, double x1,
               double y1, double x2, double y2, agg::rgba8 line_color,
               agg::rgba8 text_color, double line_width, double arrow_size,
               const std::string &text)
        : m_feng(feng), m_fman(fman), m_x1(x1), m_y1(y1), m_x2(x2), m_y2(y2),
          m_line_color(line_color), m_text_color(text_color), m_line_width(line_width),
          m_arrow_size(arrow_size), m_text(text)
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

        // 绘制标签文字（沿线段方向放置）
        m_feng.height(16.0);
        m_feng.width(16.0);
        m_feng.flip_y(true);
        double cx = m_x1 + m_t * dx + nx * m_offset;
        double cy = m_y1 + m_t * dy + ny * m_offset;
        double text_width = get_text_width(m_fman, m_text);

        agg::trans_affine mtx_total;
        mtx_total.reset();
        mtx_total *= agg::trans_affine_translation(-text_width / 2.0, 0.0);
        mtx_total *= agg::trans_affine_rotation(angle);
        mtx_total *= agg::trans_affine_translation(cx, cy);

        double cur_x = 0.0;
        for (char c : m_text)
        {
            const agg::glyph_cache *glyph = m_fman.glyph(c);
            if (!glyph || glyph->data_type != agg::glyph_data_outline)
            {
                if (glyph)
                    cur_x += glyph->advance_x;
                continue;
            }
            m_fman.init_embedded_adaptors(glyph, 0, 0);
            typedef agg::conv_curve<agg::font_cache_manager<
                agg::font_engine_freetype_int32>::path_adaptor_type>
                curve_t;
            curve_t curve(m_fman.path_adaptor());
            agg::trans_affine mtx_char;
            mtx_char.reset();
            mtx_char *= agg::trans_affine_translation(cur_x, 0.0);
            mtx_char *= mtx_total;
            typedef agg::conv_transform<curve_t, agg::trans_affine> transform_t;
            transform_t trans(curve, mtx_char);
            ras.reset();
            ras.add_path(trans);
            ren_solid.color(m_text_color);
            agg::render_scanlines(ras, sl, ren_solid);
            cur_x += glyph->advance_x;
        }
    }

  private:
    agg::font_engine_freetype_int32 &m_feng;
    agg::font_cache_manager<agg::font_engine_freetype_int32> &m_fman;
    double m_x1, m_y1, m_x2, m_y2;
    agg::rgba8 m_line_color, m_text_color;
    double m_line_width, m_arrow_size;
    std::string m_text;
    double m_t = 0.5;
    double m_offset = 0;
};

// ---------- 验证函数：比较 AGG 和原生 FreeType 的度量（保持不变）----------
void compare_metrics(agg::font_engine_freetype_int32 &feng,
                     agg::font_cache_manager<agg::font_engine_freetype_int32> &fman,
                     double desired_height, const char *font_path)
{
    bool old_flip = feng.flip_y();
    feng.flip_y(false);
    const agg::glyph_cache *glyph = fman.glyph('g');
    if (!glyph)
    {
        std::cerr << "AGG: 无法获取字形\n";
        return;
    }
    double x1 = glyph->bounds.x1, x2 = glyph->bounds.x2;
    double y1 = glyph->bounds.y1, y2 = glyph->bounds.y2;
    double advance = glyph->advance_x;
    feng.flip_y(old_flip);

    GlyphMetrics agg_metrics;
    agg_metrics.ascender = y2;
    agg_metrics.descender = y1;
    agg_metrics.bearingX = x1;
    agg_metrics.bearingY = y2;
    agg_metrics.xMin = x1;
    agg_metrics.xMax = x2;
    agg_metrics.yMax = y2;
    agg_metrics.yMin = y1;
    agg_metrics.width = x2 - x1;
    agg_metrics.height = y2 - y1;
    agg_metrics.advance = advance;

    FT_Library ft_library;
    FT_Face ft_face;
    if (FT_Init_FreeType(&ft_library))
    {
        std::cerr << "FT_Init_FreeType 失败\n";
        return;
    }
    if (FT_New_Face(ft_library, font_path, 0, &ft_face))
    {
        std::cerr << "FT_New_Face 失败: " << font_path << "\n";
        FT_Done_FreeType(ft_library);
        return;
    }
    FT_Set_Pixel_Sizes(ft_face, (FT_UInt)desired_height, (FT_UInt)desired_height);
    if (FT_Load_Char(ft_face, 'g', FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING))
    {
        std::cerr << "FT_Load_Char 失败\n";
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_library);
        return;
    }

    FT_Glyph_Metrics &ft_metrics = ft_face->glyph->metrics;
    double fx1 = ft_metrics.horiBearingX / 64.0;
    double fx2 = (ft_metrics.horiBearingX + ft_metrics.width) / 64.0;
    double fy1 = (ft_metrics.horiBearingY - ft_metrics.height) / 64.0;
    double fy2 = ft_metrics.horiBearingY / 64.0;
    double fadv = ft_metrics.horiAdvance / 64.0;

    GlyphMetrics ft_metrics_px;
    ft_metrics_px.ascender = fy2;
    ft_metrics_px.descender = fy1;
    ft_metrics_px.bearingX = fx1;
    ft_metrics_px.bearingY = fy2;
    ft_metrics_px.xMin = fx1;
    ft_metrics_px.xMax = fx2;
    ft_metrics_px.yMax = fy2;
    ft_metrics_px.yMin = fy1;
    ft_metrics_px.width = fx2 - fx1;
    ft_metrics_px.height = fy2 - fy1;
    ft_metrics_px.advance = fadv;

    // 原始浮点像素比较（原有）
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "====== 度量比较 (AGG vs FreeType) ======\n";
    std::cout << "字段\t\tAGG\t\tFreeType\t差异\n";
    auto cmp = [](const char *name, double a, double b) {
        std::cout << name << "\t\t" << a << "\t\t" << b << "\t\t" << (a - b) << "\n";
    };
    cmp("ascender", agg_metrics.ascender, ft_metrics_px.ascender);
    cmp("descender", agg_metrics.descender, ft_metrics_px.descender);
    cmp("bearingX", agg_metrics.bearingX, ft_metrics_px.bearingX);
    cmp("bearingY", agg_metrics.bearingY, ft_metrics_px.bearingY);
    cmp("xMin", agg_metrics.xMin, ft_metrics_px.xMin);
    cmp("xMax", agg_metrics.xMax, ft_metrics_px.xMax);
    cmp("yMax", agg_metrics.yMax, ft_metrics_px.yMax);
    cmp("yMin", agg_metrics.yMin, ft_metrics_px.yMin);
    cmp("width", agg_metrics.width, ft_metrics_px.width);
    cmp("height", agg_metrics.height, ft_metrics_px.height);
    cmp("advance", agg_metrics.advance, ft_metrics_px.advance);
    std::cout << "=========================================\n";

    // ★★★ 新增：原始 26.6 单位比较 ★★★
    std::cout << "\n--- 原始 26.6 单位比较 (AGG*64 vs FreeType) ---\n";
    std::cout << "字段        AGG*64      FreeType(26.6)  差异(1/64)\n";
    auto cmp_raw = [&](const char *name, double agg_val, FT_Pos ft_val) {
        long agg_26 = static_cast<long>(std::round(agg_val * 64.0));
        long diff = agg_26 - ft_val;
        std::cout << std::setw(12) << name << ": " << std::setw(10) << agg_26 << "  "
                  << std::setw(15) << ft_val << "  " << std::setw(10) << diff << "\n";
    };
    cmp_raw("ascender", agg_metrics.ascender, ft_metrics.horiBearingY);
    cmp_raw("descender", agg_metrics.descender,
            ft_metrics.horiBearingY - ft_metrics.height);
    cmp_raw("bearingX", agg_metrics.bearingX, ft_metrics.horiBearingX);
    cmp_raw("bearingY", agg_metrics.bearingY, ft_metrics.horiBearingY);
    cmp_raw("xMin", agg_metrics.xMin, ft_metrics.horiBearingX);
    cmp_raw("xMax", agg_metrics.xMax, ft_metrics.horiBearingX + ft_metrics.width);
    cmp_raw("yMax", agg_metrics.yMax, ft_metrics.horiBearingY);
    cmp_raw("yMin", agg_metrics.yMin, ft_metrics.horiBearingY - ft_metrics.height);
    cmp_raw("width", agg_metrics.width, ft_metrics.width);
    cmp_raw("height", agg_metrics.height, ft_metrics.height);
    cmp_raw("advance", agg_metrics.advance, ft_metrics.horiAdvance);
    std::cout << "=========================================\n";

    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft_library);
}
// ---------- 主函数（简化优化后）----------
int main()
{
    // 创建图像缓冲区
    std::vector<unsigned char> buffer(WIDTH * HEIGHT * 3, 255);
    agg::rendering_buffer rbuf(&buffer[0], WIDTH, HEIGHT, WIDTH * 3);
    agg::pixfmt_rgb24 pixf(rbuf);
    agg::renderer_base<agg::pixfmt_rgb24> ren_base(pixf);
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> ren_solid(
        ren_base);
    ren_base.clear(agg::rgba8(255, 255, 255));

    // 初始化字体引擎
    agg::font_engine_freetype_int32 feng;
    agg::font_cache_manager<agg::font_engine_freetype_int32> fman(feng);
    const char *font_paths[] = {
        "TiroBangla-Regular.ttf", "C:/Windows/Fonts/times.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSerif-Italic.ttf", "times.ttf"};
    const char *used_font_path = nullptr;
    for (const char *path : font_paths)
    {
        if (feng.load_font(path, 0, agg::glyph_ren_outline))
        {
            std::cout << "成功加载字体: " << path << std::endl;
            used_font_path = path;
            break;
        }
    }
    if (!used_font_path)
    {
        std::cerr << "无法加载字体，退出。\n";
        return 1;
    }

    double desired_height = HEIGHT * 0.7;
    feng.height(desired_height);
    feng.width(desired_height);
    feng.flip_y(true); // 渲染时 Y 轴向下
    feng.hinting(false);

    // 度量验证（使用 flip_y(false) 获取原始度量）
    compare_metrics(feng, fman, desired_height, used_font_path);

    // ---------- 获取字形（此时 flip_y=true）----------
    const agg::glyph_cache *glyph = fman.glyph('g');
    if (!glyph)
    {
        std::cerr << "无法获取字形 'g'\n";
        return 1;
    }

    // 相对坐标边界 (flip_y=true 下: yMin 为负上升部分, yMax 为正下降部分)
    double xMin = glyph->bounds.x1;
    double xMax = glyph->bounds.x2;
    double yMin = glyph->bounds.y1; // 顶部（负值）
    double yMax = glyph->bounds.y2; // 底部（正值）
    double advance = glyph->advance_x;

    // 计算真实度量值（用于标注文字）
    double ascender = -yMin;    // 上升高度（正）
    double descender = yMax;    // 下降深度（正）
    double bearingX = xMin;     // 左轴承（可能为负）
    double bearingY = ascender; // 上轴承
    double width = xMax - xMin;
    double height = ascender + descender; // 或 yMax - yMin

    // 计算屏幕原点，使字形居中
    double bbox_center_x = (xMin + xMax) / 2.0;
    double bbox_center_y = (yMin + yMax) / 2.0;
    double centerX = WIDTH / 2.0;
    double centerY = HEIGHT / 2.0;
    double origin_x = centerX - bbox_center_x;
    double origin_y = centerY - bbox_center_y; // 注意是减号

    // 计算屏幕上的边界框
    double screen_left = origin_x + xMin;
    double screen_right = origin_x + xMax;
    double screen_top = origin_y + yMin;    // yMin 为负，得到上方坐标
    double screen_bottom = origin_y + yMax; // yMax 为正，得到下方坐标

    // 边界检查，确保字形不贴边
    const double margin = 20;
    if (screen_left < margin)
        origin_x = margin - xMin;
    if (screen_right > WIDTH - margin)
        origin_x = WIDTH - margin - xMax;
    if (screen_top < margin)
        origin_y = margin - yMin; // 由 screen_top = origin_y + yMin < margin 推出
    if (screen_bottom > HEIGHT - margin)
        origin_y = HEIGHT - margin -
                   yMax; // 由 screen_bottom = origin_y + yMax > HEIGHT-margin 推出

    // 重新计算屏幕边界（基于调整后的原点）
    screen_left = origin_x + xMin;
    screen_right = origin_x + xMax;
    screen_top = origin_y + yMin;
    screen_bottom = origin_y + yMax;

    // 渲染字形
    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_u8 sl;
    fman.init_embedded_adaptors(glyph, origin_x, origin_y);
    agg::conv_curve<
        agg::font_cache_manager<agg::font_engine_freetype_int32>::path_adaptor_type>
        curves(fman.path_adaptor());
    ras.reset();
    ras.add_path(curves);
    ren_solid.color(agg::rgba8(0, 0, 0));
    agg::render_scanlines(ras, sl, ren_solid);

    // 定义颜色
    agg::rgba8 color_origin(0, 150, 0);
    agg::rgba8 color_axis(80, 80, 80);
    agg::rgba8 color_bearingX(255, 0, 0);
    agg::rgba8 color_bearingY(0, 200, 0);
    agg::rgba8 color_x(0, 0, 255);
    agg::rgba8 color_y(255, 165, 0);
    agg::rgba8 color_height(128, 0, 128);
    agg::rgba8 color_width(0, 200, 200);
    agg::rgba8 color_advance(139, 69, 19);

    // 绘制坐标系（基线 + 垂直轴）
    draw_line_with_stroke(ras, sl, ren_solid, 0, origin_y, WIDTH, origin_y, color_axis,
                          1.5);
    draw_line_with_stroke(ras, sl, ren_solid, origin_x, 0, origin_x, HEIGHT, color_axis,
                          1.5);
    draw_arrow(ras, sl, ren_solid, origin_x + xMax - 20, origin_y, origin_x + xMax,
               origin_y, color_axis, 8);
    draw_arrow(ras, sl, ren_solid, origin_x, origin_y + yMin + 20, origin_x,
               origin_y + yMin, color_axis, 8); // 修正箭头方向

    // 绘制原点十字
    ren_solid.color(color_origin);
    ras.reset();
    ras.move_to_d(origin_x - 3, origin_y - 3);
    ras.line_to_d(origin_x + 3, origin_y + 3);
    ras.move_to_d(origin_x + 3, origin_y - 3);
    ras.line_to_d(origin_x - 3, origin_y + 3);
    agg::render_scanlines(ras, sl, ren_solid);
    draw_label(feng, fman, ras, sl, ren_solid, origin_x + 10, origin_y - 10,
               agg::rgba8(0, 0, 0), "(0,0)");

    // ----- 绘制各个度量线段 -----
    // bearingX (红)
    ArrowLabel arrow_bearingX(feng, fman, origin_x, origin_y, origin_x + bearingX,
                              origin_y, color_bearingX, color_bearingX, 2.0, 12.0,
                              "bearingX " + std::to_string(bearingX).substr(0, 6));
    arrow_bearingX.set_position(0.7);
    arrow_bearingX.set_offset(15.0);
    arrow_bearingX.draw(ras, sl, ren_solid);

    // bearingY (绿) 终点为上边界 screen_top
    ArrowLabel arrow_bearingY(feng, fman, origin_x, origin_y, origin_x, screen_top,
                              color_bearingY, color_bearingY, 2.0, 12.0,
                              "bearingY " + std::to_string(bearingY).substr(0, 6));
    arrow_bearingY.set_position(0.7);
    arrow_bearingY.set_offset(15.0);
    arrow_bearingY.draw(ras, sl, ren_solid);

    // xMin (蓝) - 左边框垂直线
    ArrowLabel arrow_xMin(feng, fman, screen_left, screen_top, screen_left, screen_bottom,
                          color_x, color_x, 2.0, 12.0,
                          "xMin " + std::to_string(xMin).substr(0, 6));
    arrow_xMin.set_position(0.5);
    arrow_xMin.set_offset(-15.0);
    arrow_xMin.draw(ras, sl, ren_solid);

    // xMax (蓝) - 右边框垂直线
    ArrowLabel arrow_xMax(feng, fman, screen_right, screen_top, screen_right,
                          screen_bottom, color_x, color_x, 2.0, 12.0,
                          "xMax " + std::to_string(xMax).substr(0, 6));
    arrow_xMax.set_position(0.5);
    arrow_xMax.set_offset(15.0);
    arrow_xMax.draw(ras, sl, ren_solid);

    // ascender (橙) - 上边界水平线（标注为 ascender）
    ArrowLabel arrow_ascender(feng, fman, screen_left, screen_top, screen_right,
                              screen_top, color_y, color_y, 2.0, 12.0,
                              "ascender " + std::to_string(ascender).substr(0, 6));
    arrow_ascender.set_position(0.5);
    arrow_ascender.set_offset(15.0);
    arrow_ascender.draw(ras, sl, ren_solid);

    // descender (橙) - 下边界水平线（标注为 descender）
    ArrowLabel arrow_descender(feng, fman, screen_left, screen_bottom, screen_right,
                               screen_bottom, color_y, color_y, 2.0, 12.0,
                               "descender " + std::to_string(descender).substr(0, 6));
    arrow_descender.set_position(0.5);
    arrow_descender.set_offset(-15.0);
    arrow_descender.draw(ras, sl, ren_solid);

    // height (紫) - 右侧垂直线
    double h_x = screen_right + 50;
    ArrowLabel arrow_height(feng, fman, h_x, screen_bottom, h_x, screen_top, color_height,
                            color_height, 2.0, 12.0,
                            "height " + std::to_string(height).substr(0, 6));
    arrow_height.set_position(0.5);
    arrow_height.set_offset(15.0);
    arrow_height.draw(ras, sl, ren_solid);

    // width (青) - 下方水平线
    double w_y = screen_bottom + 50;
    ArrowLabel arrow_width(feng, fman, screen_left, w_y, screen_right, w_y, color_width,
                           color_width, 2.0, 12.0,
                           "width " + std::to_string(width).substr(0, 6));
    arrow_width.set_position(0.5);
    arrow_width.set_offset(15.0);
    arrow_width.draw(ras, sl, ren_solid);

    // advance (棕) - 下方偏右水平线
    double adv_x = origin_x + advance;
    double adv_y = origin_y + 70;
    ArrowLabel arrow_advance(feng, fman, origin_x, adv_y, adv_x, adv_y, color_advance,
                             color_advance, 2.0, 12.0,
                             "advance " + std::to_string(advance).substr(0, 6));
    arrow_advance.set_position(0.5);
    arrow_advance.set_offset(15.0);
    arrow_advance.draw(ras, sl, ren_solid);

    // 添加图例
    int legend_x = WIDTH - 180;
    int legend_y = 40;
    draw_label(feng, fman, ras, sl, ren_solid, legend_x, legend_y, agg::rgba8(0, 0, 0),
               "Metrics:");
    draw_label(feng, fman, ras, sl, ren_solid, legend_x + 10, legend_y + 20, color_axis,
               "axis");
    draw_label(feng, fman, ras, sl, ren_solid, legend_x + 10, legend_y + 40, color_origin,
               "origin");
    draw_label(feng, fman, ras, sl, ren_solid, legend_x + 10, legend_y + 60,
               color_bearingX, "bearingX");
    draw_label(feng, fman, ras, sl, ren_solid, legend_x + 10, legend_y + 80,
               color_bearingY, "bearingY");
    draw_label(feng, fman, ras, sl, ren_solid, legend_x + 10, legend_y + 100, color_x,
               "xMin/xMax");
    draw_label(feng, fman, ras, sl, ren_solid, legend_x + 10, legend_y + 120, color_y,
               "asc/desc");
    draw_label(feng, fman, ras, sl, ren_solid, legend_x + 10, legend_y + 140,
               color_height, "height");
    draw_label(feng, fman, ras, sl, ren_solid, legend_x + 10, legend_y + 160, color_width,
               "width");
    draw_label(feng, fman, ras, sl, ren_solid, legend_x + 10, legend_y + 180,
               color_advance, "advance");

    // 输出 PPM
    std::ofstream out("arrow_demo6.ppm", std::ios::binary);
    out << "P6\n" << WIDTH << " " << HEIGHT << "\n255\n";
    out.write(reinterpret_cast<char *>(&buffer[0]), buffer.size());
    out.close();

    std::cout << "已生成 glyph_metrics_clean.ppm\n";
    return 0;
}