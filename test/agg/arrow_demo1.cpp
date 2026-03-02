// g++ -o arrow_demo arrow_demo.cpp -lagg -lfreetype
// 运行后生成 arrow_demo.ppm
#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <iostream>
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

// 计算字符串总宽度（用于居中）
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

// 绘制带箭头的线段，并在中点偏移位置绘制平行文字
void draw_arrow_with_label(
    agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl,
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> &ren_solid,
    agg::font_engine_freetype_int32 &feng,
    agg::font_cache_manager<agg::font_engine_freetype_int32> &fman, double x1, double y1,
    double x2, double y2, agg::rgba8 line_color, agg::rgba8 text_color, double line_width,
    double arrow_size, const std::string &text,
    double offset) // 正值在左侧（沿法线方向），负值在右侧
{
    // ---------- 绘制线段 ----------
    agg::path_storage line_path;
    line_path.move_to(x1, y1);
    line_path.line_to(x2, y2);
    agg::conv_stroke<agg::path_storage> stroke(line_path);
    stroke.width(line_width);
    ras.reset();
    ras.add_path(stroke);
    ren_solid.color(line_color);
    agg::render_scanlines(ras, sl, ren_solid);

    // ---------- 绘制箭头（填充三角形） ----------
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-6)
    {
        double angle = std::atan2(dy, dx);
        double xa1 = x2 + arrow_size * std::cos(angle + agg::pi * 5.0 / 6.0);
        double ya1 = y2 + arrow_size * std::sin(angle + agg::pi * 5.0 / 6.0);
        double xa2 = x2 + arrow_size * std::cos(angle - agg::pi * 5.0 / 6.0);
        double ya2 = y2 + arrow_size * std::sin(angle - agg::pi * 5.0 / 6.0);

        agg::path_storage arrow_path;
        arrow_path.move_to(x2, y2);
        arrow_path.line_to(xa1, ya1);
        arrow_path.line_to(xa2, ya2);
        arrow_path.close_polygon();

        ras.reset();
        ras.add_path(arrow_path);
        ren_solid.color(line_color);
        agg::render_scanlines(ras, sl, ren_solid);
    }

    // ---------- 绘制平行文字 ----------
    feng.height(16.0);
    feng.width(16.0);
    feng.flip_y(true); // 屏幕坐标系，y向下为正

    // 线段中点
    double mx = (x1 + x2) / 2.0;
    double my = (y1 + y2) / 2.0;

    // 线段角度
    double angle = std::atan2(dy, dx);

    // 法线方向（逆时针旋转90度）
    double nx = -std::sin(angle);
    double ny = std::cos(angle);

    // 文字中心点（偏移后）
    double cx = mx + nx * offset;
    double cy = my + ny * offset;

    // 文字总宽度
    double text_width = get_text_width(fman, text);

    // 构建总变换矩阵：将原点文字先平移 (-width/2, 0) 使中心对齐原点，然后旋转，然后平移到
    // (cx, cy)
    agg::trans_affine mtx_total;
    mtx_total.reset();
    mtx_total *= agg::trans_affine_translation(-text_width / 2.0, 0.0);
    mtx_total *= agg::trans_affine_rotation(angle);
    mtx_total *= agg::trans_affine_translation(cx, cy);

    // 逐个字符渲染
    double cur_x = 0.0; // 当前字符在水平方向上的累积偏移（相对于文字起点）
    for (char c : text)
    {
        const agg::glyph_cache *glyph = fman.glyph(c);
        if (!glyph || glyph->data_type != agg::glyph_data_outline)
        {
            if (glyph)
                cur_x += glyph->advance_x;
            continue;
        }

        // 获取字形路径，并应用曲线光滑
        fman.init_embedded_adaptors(glyph, 0, 0); // 字形置于原点
        typedef agg::conv_curve<
            agg::font_cache_manager<agg::font_engine_freetype_int32>::path_adaptor_type>
            curve_t;
        curve_t curve(fman.path_adaptor());

        // 构建当前字符的变换矩阵：先平移到 (cur_x, 0)，再应用总变换
        agg::trans_affine mtx_char;
        mtx_char.reset();
        mtx_char *= agg::trans_affine_translation(cur_x, 0.0);
        mtx_char *= mtx_total;

        typedef agg::conv_transform<curve_t, agg::trans_affine> transform_t;
        transform_t trans(curve, mtx_char);

        // 渲染
        ras.reset();
        ras.add_path(trans);
        ren_solid.color(text_color);
        agg::render_scanlines(ras, sl, ren_solid);

        // 更新x偏移
        cur_x += glyph->advance_x;
    }
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

    // 尝试加载字体（根据你的系统修改路径）
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

    // 绘制多个示例箭头
    // 1. 水平箭头，文字在上方
    draw_arrow_with_label(ras, sl, ren_solid, feng, fman, 100, 200, 300, 200,
                          agg::rgba8(255, 0, 0), // 红色线段
                          agg::rgba8(0, 0, 255), // 蓝色文字
                          2.0, 15.0, "Horizontal", 20.0);

    // 2. 垂直箭头，文字在右侧（法线方向向右）
    draw_arrow_with_label(ras, sl, ren_solid, feng, fman, 500, 400, 500, 200,
                          agg::rgba8(0, 255, 0),   // 绿色线段
                          agg::rgba8(255, 0, 255), // 紫色文字
                          2.0, 15.0, "Vertical", 20.0);

    // 3. 斜线箭头，文字在上方
    draw_arrow_with_label(ras, sl, ren_solid, feng, fman, 100, 500, 300, 400,
                          agg::rgba8(0, 0, 255),   // 蓝色线段
                          agg::rgba8(255, 165, 0), // 橙色文字
                          2.0, 15.0, "Slanted", 20.0);

    // 4. 反向斜线箭头，文字在下方（负偏移）
    draw_arrow_with_label(ras, sl, ren_solid, feng, fman, 600, 100, 700, 300,
                          agg::rgba8(255, 255, 0), // 黄色线段
                          agg::rgba8(128, 0, 128), // 紫色文字
                          2.0, 15.0, "Reverse", -20.0);

    // 输出 PPM
    std::ofstream out("arrow_demo1.ppm", std::ios::binary);
    out << "P6\n" << WIDTH << " " << HEIGHT << "\n255\n";
    out.write(reinterpret_cast<char *>(&buffer[0]), buffer.size());
    out.close();

    std::cout << "已生成 arrow_demo.ppm，可用 ImageMagick 查看。\n";
    return 0;
}