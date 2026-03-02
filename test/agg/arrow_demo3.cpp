// g++ -o coordinate_system coordinate_system.cpp -lagg -lfreetype
// 运行后生成 coordinate_system.ppm
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

const unsigned WIDTH = 800;
const unsigned HEIGHT = 600;

// 绘制带线宽的直线（使用 path_storage + conv_stroke）
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

// 绘制箭头（在终点处绘制小三角形）
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

// 绘制文本（屏幕坐标，基线位置，无旋转）
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

// 绘制带刻度的坐标轴（水平或垂直）
void draw_axis_with_ticks(
    agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl,
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> &ren_solid,
    agg::font_engine_freetype_int32 &feng,
    agg::font_cache_manager<agg::font_engine_freetype_int32> &fman, double origin_x,
    double origin_y,                        // 原点屏幕坐标（对应逻辑0）
    double axis_length_px,                  // 轴的总长度（像素，对应逻辑范围长度）
    double logical_min, double logical_max, // 逻辑范围（如 -5 到 5）
    bool is_horizontal,                     // true=水平轴，false=垂直轴
    agg::rgba8 axis_color, agg::rgba8 tick_color, agg::rgba8 text_color)
{
    double logical_range = logical_max - logical_min;
    double unit_px = axis_length_px / logical_range; // 像素/逻辑单位

    // 绘制轴线（完整范围）
    double x_start, x_end, y_start, y_end;
    if (is_horizontal)
    {
        x_start = origin_x + logical_min * unit_px;
        x_end = origin_x + logical_max * unit_px;
        y_start = origin_y;
        y_end = origin_y;
    }
    else
    {
        x_start = origin_x;
        x_end = origin_x;
        // 垂直轴：逻辑y -> 屏幕y = origin_y - y * unit_px
        y_start = origin_y -
                  logical_min * unit_px; // 对应logical_min（可能是负，所以屏幕y更大）
        y_end = origin_y - logical_max * unit_px; // 对应logical_max（屏幕y更小）
    }
    draw_line_with_stroke(ras, sl, ren_solid, x_start, y_start, x_end, y_end, axis_color,
                          2.0);

    // 在正方向终点画箭头（指向逻辑增加方向）
    if (is_horizontal)
    {
        // 箭头指向右侧（逻辑增加）
        draw_arrow(ras, sl, ren_solid, x_end - 20, y_end, x_end, y_end, axis_color, 8.0);
    }
    else
    {
        // 箭头指向上方（逻辑增加，屏幕y减小）
        draw_arrow(ras, sl, ren_solid, x_end, y_end + 20, x_end, y_end, axis_color, 8.0);
    }

    // 绘制刻度线
    for (int logical_val = (int)logical_min; logical_val <= (int)logical_max;
         ++logical_val)
    {
        // 计算刻度中心屏幕坐标
        double tick_center_x, tick_center_y;
        if (is_horizontal)
        {
            tick_center_x = origin_x + logical_val * unit_px;
            tick_center_y = origin_y;
        }
        else
        {
            tick_center_x = origin_x;
            tick_center_y = origin_y - logical_val * unit_px;
        }

        // 确定刻度线长度
        double tick_len_short = 5.0;
        double tick_len_long = 10.0;
        double tick_len = tick_len_short;
        bool is_long = false;
        if (logical_val % 5 == 0)
        {
            tick_len = tick_len_long;
            is_long = true;
        }
        if (logical_val % 10 == 0)
        {
            tick_len = tick_len_long * 1.5;
        }

        // 绘制刻度线（垂直于轴线）
        double tick_x1, tick_y1, tick_x2, tick_y2;
        if (is_horizontal)
        {
            tick_x1 = tick_center_x;
            tick_y1 = tick_center_y - tick_len / 2;
            tick_x2 = tick_center_x;
            tick_y2 = tick_center_y + tick_len / 2;
        }
        else
        {
            tick_x1 = tick_center_x - tick_len / 2;
            tick_y1 = tick_center_y;
            tick_x2 = tick_center_x + tick_len / 2;
            tick_y2 = tick_center_y;
        }
        draw_line_with_stroke(ras, sl, ren_solid, tick_x1, tick_y1, tick_x2, tick_y2,
                              tick_color, 1.0);

        // 对于长刻度线，绘制数字标签
        if (is_long)
        {
            std::string num_str = std::to_string(logical_val);
            size_t dot_pos = num_str.find('.');
            if (dot_pos != std::string::npos)
            {
                num_str = num_str.substr(0, dot_pos);
            }
            double label_x, label_y;
            if (is_horizontal)
            {
                label_x = tick_center_x - 7;
                label_y = tick_center_y + tick_len / 2 + 15;
            }
            else
            {
                label_x = tick_center_x + tick_len / 2 + 5;
                label_y = tick_center_y - 7;
            }
            draw_label(feng, fman, ras, sl, ren_solid, label_x, label_y, text_color,
                       num_str);
        }
    }
}

// 绘制完整坐标系（包含水平和垂直轴）
void draw_coordinate_system(
    agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl,
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> &ren_solid,
    agg::font_engine_freetype_int32 &feng,
    agg::font_cache_manager<agg::font_engine_freetype_int32> &fman, double origin_x,
    double origin_y, double x_axis_length_px, double y_axis_length_px,
    double logical_x_min, double logical_x_max, double logical_y_min,
    double logical_y_max, agg::rgba8 axis_color, agg::rgba8 tick_color,
    agg::rgba8 text_color)
{
    // 水平轴
    draw_axis_with_ticks(ras, sl, ren_solid, feng, fman, origin_x, origin_y,
                         x_axis_length_px, logical_x_min, logical_x_max, true, axis_color,
                         tick_color, text_color);
    // 垂直轴
    draw_axis_with_ticks(ras, sl, ren_solid, feng, fman, origin_x, origin_y,
                         y_axis_length_px, logical_y_min, logical_y_max, false,
                         axis_color, tick_color, text_color);
}

int main()
{
    // 创建图像缓冲区（白色背景）
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
        "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/times.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf", "times.ttf"};
    bool font_loaded = false;
    for (const char *path : font_paths)
    {
        if (feng.load_font(path, 0, agg::glyph_ren_outline))
        {
            std::cout << "成功加载字体: " << path << std::endl;
            font_loaded = true;
            break;
        }
    }
    if (!font_loaded)
    {
        std::cerr << "警告：无法加载字体，文字将不会显示。\n";
    }

    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_u8 sl;

    agg::rgba8 axis_color(80, 80, 80);
    agg::rgba8 tick_color(120, 120, 120);
    agg::rgba8 text_color(0, 0, 0);

    double logical_min = -5.0;
    double logical_max = 5.0;
    double axis_length = 200.0;   // 1:1
    double axis_length_2 = 400.0; // 1:2

    // 左侧坐标系
    draw_coordinate_system(ras, sl, ren_solid, feng, fman, 150, 350, axis_length,
                           axis_length, logical_min, logical_max, logical_min,
                           logical_max, axis_color, tick_color, text_color);
    // 右侧坐标系
    draw_coordinate_system(ras, sl, ren_solid, feng, fman, 500, 350, axis_length_2,
                           axis_length_2, logical_min, logical_max, logical_min,
                           logical_max, axis_color, tick_color, text_color);

    if (font_loaded)
    {
        draw_label(feng, fman, ras, sl, ren_solid, 150, 300, agg::rgba8(0, 0, 0),
                   "1:1 scale");
        draw_label(feng, fman, ras, sl, ren_solid, 500, 300, agg::rgba8(0, 0, 0),
                   "1:2 scale");
    }

    std::ofstream out("arrow_demo3.ppm", std::ios::binary);
    out << "P6\n" << WIDTH << " " << HEIGHT << "\n255\n";
    out.write(reinterpret_cast<char *>(&buffer[0]), buffer.size());
    out.close();

    std::cout << "已生成 coordinate_system.ppm，可用 ImageMagick 查看。\n";
    return 0;
}